#include "baf.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *text;
    size_t length;
    int id;
} StringEntry;

typedef struct {
    StringEntry *items;
    size_t count;
    size_t capacity;
} StringPool;

typedef struct {
    const char *name;
    BafType type;
    char *llvm_name;
} CgVar;

typedef struct {
    CgVar *items;
    size_t count;
    size_t capacity;
} CgEnv;

typedef struct {
    BafType type;
    char *repr;
} CgValue;

typedef struct {
    char *break_label;
    char *continue_label;
} LoopFrame;

typedef struct {
    StrBuf body;
    StrBuf prologue;
    StringPool strings;
    LoopFrame loops[64];
    size_t loop_count;
    BafType current_return_type;
    bool in_entry;
    int next_temp;
    int next_label;
    int next_slot;
    int next_input_buffer;
    BafTarget target;
    const char *int_type;
    int int_align;
    int ptr_align;
} ModuleGen;

static char *format_alloc(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return baf_xstrdup("");
    }
    char *text = baf_xmalloc((size_t)needed + 1);
    vsnprintf(text, (size_t)needed + 1, fmt, args);
    va_end(args);
    return text;
}

/* Splices text into a StrBuf at an arbitrary offset. Used to lift allocas into
   the entry block after the rest of the function has already been emitted. */
static void sb_insert(StrBuf *buf, size_t position, const char *text) {
    size_t length = strlen(text);
    if (length == 0) return;
    if (position > buf->len) position = buf->len;
    size_t needed = buf->len + length + 1;
    if (needed > buf->cap) {
        size_t next = buf->cap ? buf->cap : 64;
        while (next < needed) next *= 2;
        buf->data = baf_xrealloc(buf->data, next);
        buf->cap = next;
    }
    memmove(buf->data + position + length, buf->data + position,
            buf->len - position + 1);
    memcpy(buf->data + position, text, length);
    buf->len += length;
    buf->data[buf->len] = '\0';
}

static void pool_free(StringPool *pool) {
    for (size_t i = 0; i < pool->count; i++) free(pool->items[i].text);
    free(pool->items);
    *pool = (StringPool){0};
}

static StringEntry *intern_string(StringPool *pool, const char *text) {
    for (size_t i = 0; i < pool->count; i++) {
        if (strcmp(pool->items[i].text, text) == 0) return &pool->items[i];
    }
    if (pool->count == pool->capacity) {
        size_t next = pool->capacity ? pool->capacity * 2 : 8;
        pool->items = baf_xrealloc(pool->items, next * sizeof(StringEntry));
        pool->capacity = next;
    }
    StringEntry entry = {
        .text = baf_xstrdup(text),
        .length = strlen(text),
        .id = (int)pool->count,
    };
    pool->items[pool->count++] = entry;
    return &pool->items[pool->count - 1];
}

static void env_free(CgEnv *env) {
    for (size_t i = 0; i < env->count; i++) free(env->items[i].llvm_name);
    free(env->items);
    *env = (CgEnv){0};
}

static void env_pop_to(CgEnv *env, size_t count) {
    while (env->count > count) {
        free(env->items[env->count - 1].llvm_name);
        env->count--;
    }
}

static void env_add(CgEnv *env, const char *name, BafType type,
                    const char *llvm_name) {
    if (env->count == env->capacity) {
        size_t next = env->capacity ? env->capacity * 2 : 8;
        env->items = baf_xrealloc(env->items, next * sizeof(CgVar));
        env->capacity = next;
    }
    env->items[env->count++] = (CgVar){
        .name = name,
        .type = type,
        .llvm_name = baf_xstrdup(llvm_name),
    };
}

static CgVar *env_find(CgEnv *env, const char *name) {
    for (size_t i = env->count; i > 0; i--) {
        if (strcmp(env->items[i - 1].name, name) == 0) {
            return &env->items[i - 1];
        }
    }
    return NULL;
}

const char *baf_target_name(BafTarget target) {
    switch (target) {
        case BAF_TARGET_HOSTED: return "hosted";
        case BAF_TARGET_I386_FREESTANDING: return "i386-freestanding";
    }
    return "<invalid>";
}

bool parse_baf_target(const char *name, BafTarget *target_out) {
    if (strcmp(name, "hosted") == 0) {
        *target_out = BAF_TARGET_HOSTED;
        return true;
    }
    if (strcmp(name, "i386-freestanding") == 0 || strcmp(name, "i386") == 0) {
        *target_out = BAF_TARGET_I386_FREESTANDING;
        return true;
    }
    return false;
}

static const char *llvm_type(ModuleGen *module, BafType type) {
    switch (type) {
        case TYPE_INT: return module->int_type;
        case TYPE_STRING: return "%baf.str";
        case TYPE_BOOL: return "i1";
        case TYPE_VOID: return "void";
        case TYPE_UNKNOWN: return "<unknown>";
    }
    return "<invalid>";
}

static int type_align(ModuleGen *module, BafType type) {
    switch (type) {
        case TYPE_INT: return module->int_align;
        case TYPE_STRING: return module->ptr_align;
        case TYPE_BOOL: return 1;
        case TYPE_UNKNOWN:
        case TYPE_VOID:
            return 1;
    }
    return 1;
}

static char *new_temp(ModuleGen *module) {
    return format_alloc("%%t%d", module->next_temp++);
}

static int new_label(ModuleGen *module) {
    return module->next_label++;
}

static char *new_slot(ModuleGen *module, const char *name) {
    return format_alloc("%%v.%s.%d", name, module->next_slot++);
}

/* LLVM requires every basic block to end with exactly one terminator. After
   emitting one mid-block (return, break, continue) we open a fresh unreachable
   block so that anything the source still has after it stays valid IR. */
static void start_dead_block(ModuleGen *module) {
    sb_printf(&module->body, "dead.%d:\n", new_label(module));
}

static void loop_push(ModuleGen *module, const char *break_label,
                      const char *continue_label) {
    if (module->loop_count >= sizeof(module->loops) / sizeof(module->loops[0])) {
        return;
    }
    module->loops[module->loop_count].break_label = baf_xstrdup(break_label);
    module->loops[module->loop_count].continue_label = baf_xstrdup(continue_label);
    module->loop_count++;
}

static void loop_pop(ModuleGen *module) {
    if (module->loop_count == 0) return;
    module->loop_count--;
    free(module->loops[module->loop_count].break_label);
    free(module->loops[module->loop_count].continue_label);
}

static void append_escaped_bytes(StrBuf *out, const char *text, size_t length) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch >= 32 && ch <= 126 && ch != '"' && ch != '\\') {
            sb_append_n(out, (const char *)&ch, 1);
        } else {
            char escape[3] = {'\\', hex[ch >> 4], hex[ch & 15]};
            sb_append_n(out, escape, 3);
        }
    }
}

static size_t call_parameter_count(Call *call) {
    if (call->resolved_function) return call->resolved_function->param_count;
    if (call->builtin == BUILTIN_PUTSC || call->builtin == BUILTIN_PUTL ||
        call->builtin == BUILTIN_INPT) {
        return call->arg_count;
    }
    switch (call->builtin) {
        case BUILTIN_PUTSC:
        case BUILTIN_PUTL:
        case BUILTIN_INPT:
            return call->arg_count;
        case BUILTIN_DISK_SELECT:
        case BUILTIN_DISK_READ:
        case BUILTIN_DISK_REM:
        case BUILTIN_DISK_EXISTS:
        case BUILTIN_DISK_SIZE:
        case BUILTIN_DISK_CREATE_DIR:
        case BUILTIN_DISK_GOTO_DIR:
        case BUILTIN_CONSOLE_SET_TEXT_COLOR:
        case BUILTIN_CONSOLE_SET_BACKGROUND_COLOR:
            return 1;
        case BUILTIN_DISK_HEX:
        case BUILTIN_DISK_WRITE:
        case BUILTIN_STR_CONCAT:
        case BUILTIN_STR_EQ:
        case BUILTIN_MATH_MIN:
        case BUILTIN_MATH_MAX:
            return 2;
        case BUILTIN_STR_SUB:
            return 3;
        case BUILTIN_STR_LENGTH:
        case BUILTIN_STR_FROM_INT:
        case BUILTIN_STR_FROM_BOOL:
        case BUILTIN_STR_TO_INT:
        case BUILTIN_MATH_ABS:
            return 1;
        case BUILTIN_CONSOLE_CLEAR:
        case BUILTIN_POWER_SHUTDOWN:
        case BUILTIN_POWER_REBOOT:
        case BUILTIN_DISK_SCAN:
        case BUILTIN_DISK_COUNT:
        case BUILTIN_DISK_LIST:
        case BUILTIN_DISK_FORMAT:
        case BUILTIN_DISK_FILES:
        case BUILTIN_DISK_INFO:
        case BUILTIN_DISK_GET_DIR:
        case BUILTIN_NONE:
            return 0;
    }
    return 0;
}

static CgValue codegen_expr(ModuleGen *module, CgEnv *env, Expr *expr);
static CgValue codegen_binary(ModuleGen *module, CgEnv *env, Expr *expr);

static void codegen_print_value(ModuleGen *module, const CgValue *value) {
    switch (value->type) {
        case TYPE_STRING:
            sb_printf(&module->body,
                      "  call void @baf.putl(%%baf.str %s)\n", value->repr);
            break;
        case TYPE_INT:
            sb_printf(&module->body,
                      "  call void @baf.put.int(%s %s)\n",
                      module->int_type, value->repr);
            break;
        case TYPE_BOOL:
            sb_printf(&module->body,
                      "  call void @baf.put.bool(i1 %s)\n", value->repr);
            break;
        case TYPE_UNKNOWN:
        case TYPE_VOID:
            break;
    }
}

static CgValue codegen_call(ModuleGen *module, CgEnv *env, Call *call) {
    size_t parameter_count = call_parameter_count(call);
    CgValue *ordered = baf_xmalloc((parameter_count ? parameter_count : 1) *
                                   sizeof(CgValue));
    memset(ordered, 0, (parameter_count ? parameter_count : 1) *
                           sizeof(CgValue));

    for (size_t i = 0; i < call->arg_count; i++) {
        int index = call->args[i].resolved_parameter;
        if (index < 0 || (size_t)index >= parameter_count) continue;
        ordered[index] = codegen_expr(module, env, call->args[i].value);
    }

    CgValue result = {.type = call->return_type, .repr = NULL};
    switch (call->builtin) {
        case BUILTIN_PUTSC:
            for (size_t i = 0; i < parameter_count; i++) {
                codegen_print_value(module, &ordered[i]);
            }
            sb_append(&module->body, "  call void @baf.put.newline()\n");
            break;
        case BUILTIN_PUTL:
            for (size_t i = 0; i < parameter_count; i++) {
                codegen_print_value(module, &ordered[i]);
            }
            break;
        case BUILTIN_INPT: {
            for (size_t i = 0; i < parameter_count; i++) {
                codegen_print_value(module, &ordered[i]);
            }
            int input_id = module->next_input_buffer++;
            char *buffer = new_temp(module);
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = getelementptr inbounds [256 x i8], ptr "
                      "@.baf.input.%d, %s 0, %s 0\n",
                      buffer, input_id, module->int_type, module->int_type);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.input.read(ptr %s, %s 256)\n",
                      temp, buffer, module->int_type);
            free(buffer);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_CONSOLE_CLEAR:
            sb_append(&module->body, "  call void @baf.console.clear()\n");
            break;
        case BUILTIN_CONSOLE_SET_TEXT_COLOR:
            sb_printf(&module->body,
                      "  call void @baf.console.set_text_color(%s %s)\n",
                      module->int_type, ordered[0].repr);
            break;
        case BUILTIN_CONSOLE_SET_BACKGROUND_COLOR:
            sb_printf(&module->body,
                      "  call void @baf.console.set_background_color(%s %s)\n",
                      module->int_type, ordered[0].repr);
            break;
        case BUILTIN_POWER_SHUTDOWN:
            sb_append(&module->body, "  call void @baf.power.shutdown()\n");
            break;
        case BUILTIN_POWER_REBOOT:
            sb_append(&module->body, "  call void @baf.power.reboot()\n");
            break;
        case BUILTIN_DISK_SCAN:
            sb_append(&module->body, "  call void @baf.disk.scan()\n");
            break;
        case BUILTIN_DISK_COUNT: {
            char *temp = new_temp(module);
            sb_printf(&module->body, "  %s = call %s @baf.disk.count()\n",
                      temp, module->int_type);
            result.type = TYPE_INT;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_LIST:
            sb_append(&module->body, "  call void @baf.disk.list()\n");
            break;
        case BUILTIN_DISK_HEX:
            sb_printf(&module->body,
                      "  call void @baf.disk.hex(%s %s, %s %s)\n",
                      module->int_type, ordered[0].repr,
                      module->int_type, ordered[1].repr);
            break;
        case BUILTIN_DISK_SELECT: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.disk.select(%s %s)\n",
                      temp, module->int_type, ordered[0].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_FORMAT: {
            char *temp = new_temp(module);
            sb_printf(&module->body, "  %s = call i1 @baf.disk.format()\n", temp);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_FILES:
            sb_append(&module->body, "  call void @baf.disk.files()\n");
            break;
        case BUILTIN_DISK_WRITE: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.disk.write(%%baf.str %s, %%baf.str %s)\n",
                      temp, ordered[0].repr, ordered[1].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_READ: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.disk.read(%%baf.str %s)\n",
                      temp, ordered[0].repr);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_REM: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.disk.rem(%%baf.str %s)\n",
                      temp, ordered[0].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_EXISTS: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.disk.exists(%%baf.str %s)\n",
                      temp, ordered[0].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_SIZE: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %s @baf.disk.size(%%baf.str %s)\n",
                      temp, module->int_type, ordered[0].repr);
            result.type = TYPE_INT;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_INFO:
            sb_append(&module->body, "  call void @baf.disk.info()\n");
            break;
        case BUILTIN_DISK_CREATE_DIR: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.disk.create_dir(%%baf.str %s)\n",
                      temp, ordered[0].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_GOTO_DIR: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.disk.goto_dir(%%baf.str %s)\n",
                      temp, ordered[0].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_DISK_GET_DIR: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.disk.get_dir()\n", temp);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_LENGTH: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = extractvalue %%baf.str %s, 1\n",
                      temp, ordered[0].repr);
            result.type = TYPE_INT;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_CONCAT: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.str.concat(%%baf.str %s, "
                      "%%baf.str %s)\n",
                      temp, ordered[0].repr, ordered[1].repr);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_SUB: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.str.sub(%%baf.str %s, %s %s, "
                      "%s %s)\n",
                      temp, ordered[0].repr, module->int_type, ordered[1].repr,
                      module->int_type, ordered[2].repr);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_FROM_INT: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.str.from_int(%s %s)\n",
                      temp, module->int_type, ordered[0].repr);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_FROM_BOOL: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %%baf.str @baf.str.from_bool(i1 %s)\n",
                      temp, ordered[0].repr);
            result.type = TYPE_STRING;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_TO_INT: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call %s @baf.str.to_int(%%baf.str %s)\n",
                      temp, module->int_type, ordered[0].repr);
            result.type = TYPE_INT;
            result.repr = temp;
            break;
        }
        case BUILTIN_STR_EQ: {
            char *temp = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.str.eq(%%baf.str %s, %%baf.str %s)\n",
                      temp, ordered[0].repr, ordered[1].repr);
            result.type = TYPE_BOOL;
            result.repr = temp;
            break;
        }
        case BUILTIN_MATH_ABS: {
            char *negated = new_temp(module);
            char *is_negative = new_temp(module);
            char *temp = new_temp(module);
            sb_printf(&module->body, "  %s = sub %s 0, %s\n", negated,
                      module->int_type, ordered[0].repr);
            sb_printf(&module->body, "  %s = icmp slt %s %s, 0\n", is_negative,
                      module->int_type, ordered[0].repr);
            sb_printf(&module->body, "  %s = select i1 %s, %s %s, %s %s\n",
                      temp, is_negative, module->int_type, negated,
                      module->int_type, ordered[0].repr);
            free(negated);
            free(is_negative);
            result.type = TYPE_INT;
            result.repr = temp;
            break;
        }
        case BUILTIN_MATH_MIN:
        case BUILTIN_MATH_MAX: {
            char *compare = new_temp(module);
            char *temp = new_temp(module);
            sb_printf(&module->body, "  %s = icmp %s %s %s, %s\n", compare,
                      call->builtin == BUILTIN_MATH_MIN ? "slt" : "sgt",
                      module->int_type, ordered[0].repr, ordered[1].repr);
            sb_printf(&module->body, "  %s = select i1 %s, %s %s, %s %s\n",
                      temp, compare, module->int_type, ordered[0].repr,
                      module->int_type, ordered[1].repr);
            free(compare);
            result.type = TYPE_INT;
            result.repr = temp;
            break;
        }
        case BUILTIN_NONE: {
            const char *return_type = llvm_type(module, call->return_type);
            char *temp = NULL;
            if (call->return_type != TYPE_VOID &&
                call->return_type != TYPE_UNKNOWN) {
                temp = new_temp(module);
                sb_printf(&module->body, "  %s = call %s @baf.%s(", temp,
                          return_type, call->callee);
            } else {
                sb_printf(&module->body, "  call void @baf.%s(", call->callee);
            }
            for (size_t i = 0; i < parameter_count; i++) {
                if (i) sb_append(&module->body, ", ");
                sb_printf(&module->body, "%s %s",
                          llvm_type(module, ordered[i].type), ordered[i].repr);
            }
            sb_append(&module->body, ")\n");
            result.repr = temp;
            break;
        }
    }

    for (size_t i = 0; i < parameter_count; i++) free(ordered[i].repr);
    free(ordered);
    return result;
}

/* Converts an int or bool value into a str so that '+' can concatenate it. */
static CgValue codegen_to_string(ModuleGen *module, CgValue value) {
    if (value.type == TYPE_STRING) return value;
    char *temp = new_temp(module);
    if (value.type == TYPE_BOOL) {
        sb_printf(&module->body,
                  "  %s = call %%baf.str @baf.str.from_bool(i1 %s)\n",
                  temp, value.repr);
    } else {
        sb_printf(&module->body,
                  "  %s = call %%baf.str @baf.str.from_int(%s %s)\n",
                  temp, module->int_type, value.repr);
    }
    free(value.repr);
    CgValue converted = {.type = TYPE_STRING, .repr = temp};
    return converted;
}

static CgValue codegen_binary(ModuleGen *module, CgEnv *env, Expr *expr) {
    BinaryOp op = expr->as.binary.op;
    CgValue value = {.type = expr->inferred_type, .repr = NULL};

    /* '&&' and '||' short circuit, so the right side is generated inside its
       own block and the result travels through a stack slot. */
    if (op == BINOP_AND || op == BINOP_OR) {
        char *slot = new_temp(module);
        sb_printf(&module->prologue, "  %s = alloca i1, align 1\n", slot);
        CgValue left = codegen_expr(module, env, expr->as.binary.left);
        sb_printf(&module->body, "  store i1 %s, ptr %s, align 1\n",
                  left.repr, slot);
        int rhs_label = new_label(module);
        int end_label = new_label(module);
        if (op == BINOP_AND) {
            sb_printf(&module->body,
                      "  br i1 %s, label %%logic.rhs.%d, label %%logic.end.%d\n",
                      left.repr, rhs_label, end_label);
        } else {
            sb_printf(&module->body,
                      "  br i1 %s, label %%logic.end.%d, label %%logic.rhs.%d\n",
                      left.repr, end_label, rhs_label);
        }
        free(left.repr);
        sb_printf(&module->body, "logic.rhs.%d:\n", rhs_label);
        CgValue right = codegen_expr(module, env, expr->as.binary.right);
        sb_printf(&module->body, "  store i1 %s, ptr %s, align 1\n",
                  right.repr, slot);
        free(right.repr);
        sb_printf(&module->body, "  br label %%logic.end.%d\n", end_label);
        sb_printf(&module->body, "logic.end.%d:\n", end_label);
        char *temp = new_temp(module);
        sb_printf(&module->body, "  %s = load i1, ptr %s, align 1\n", temp, slot);
        free(slot);
        value.type = TYPE_BOOL;
        value.repr = temp;
        return value;
    }

    CgValue left = codegen_expr(module, env, expr->as.binary.left);
    CgValue right = codegen_expr(module, env, expr->as.binary.right);

    if (op == BINOP_ADD &&
        (left.type == TYPE_STRING || right.type == TYPE_STRING)) {
        left = codegen_to_string(module, left);
        right = codegen_to_string(module, right);
        char *temp = new_temp(module);
        sb_printf(&module->body,
                  "  %s = call %%baf.str @baf.str.concat(%%baf.str %s, "
                  "%%baf.str %s)\n",
                  temp, left.repr, right.repr);
        free(left.repr);
        free(right.repr);
        value.type = TYPE_STRING;
        value.repr = temp;
        return value;
    }

    if (op == BINOP_EQ || op == BINOP_NE) {
        char *temp = new_temp(module);
        if (left.type == TYPE_STRING || right.type == TYPE_STRING) {
            char *equal = new_temp(module);
            sb_printf(&module->body,
                      "  %s = call i1 @baf.str.eq(%%baf.str %s, %%baf.str %s)\n",
                      equal, left.repr, right.repr);
            if (op == BINOP_EQ) {
                free(temp);
                temp = equal;
            } else {
                sb_printf(&module->body, "  %s = xor i1 %s, true\n", temp, equal);
                free(equal);
            }
        } else {
            const char *operand_type = left.type == TYPE_BOOL
                                           ? "i1"
                                           : module->int_type;
            sb_printf(&module->body, "  %s = icmp %s %s %s, %s\n", temp,
                      op == BINOP_EQ ? "eq" : "ne", operand_type, left.repr,
                      right.repr);
        }
        free(left.repr);
        free(right.repr);
        value.type = TYPE_BOOL;
        value.repr = temp;
        return value;
    }

    const char *instruction = NULL;
    const char *comparison = NULL;
    switch (op) {
        case BINOP_ADD: instruction = "add"; break;
        case BINOP_SUB: instruction = "sub"; break;
        case BINOP_MUL: instruction = "mul"; break;
        case BINOP_DIV: instruction = "sdiv"; break;
        case BINOP_MOD: instruction = "srem"; break;
        case BINOP_LT: comparison = "slt"; break;
        case BINOP_LE: comparison = "sle"; break;
        case BINOP_GT: comparison = "sgt"; break;
        case BINOP_GE: comparison = "sge"; break;
        default: instruction = "add"; break;
    }

    char *temp = new_temp(module);
    if (comparison) {
        sb_printf(&module->body, "  %s = icmp %s %s %s, %s\n", temp, comparison,
                  module->int_type, left.repr, right.repr);
        value.type = TYPE_BOOL;
    } else if (op == BINOP_DIV || op == BINOP_MOD) {
        /* Guard against division by zero, which is undefined behaviour in
           LLVM and would crash a freestanding kernel. n / 0 yields 0. */
        char *is_zero = new_temp(module);
        char *safe = new_temp(module);
        sb_printf(&module->body, "  %s = icmp eq %s %s, 0\n", is_zero,
                  module->int_type, right.repr);
        sb_printf(&module->body, "  %s = select i1 %s, %s 1, %s %s\n", safe,
                  is_zero, module->int_type, module->int_type, right.repr);
        char *raw = new_temp(module);
        sb_printf(&module->body, "  %s = %s %s %s, %s\n", raw, instruction,
                  module->int_type, left.repr, safe);
        sb_printf(&module->body, "  %s = select i1 %s, %s 0, %s %s\n", temp,
                  is_zero, module->int_type, module->int_type, raw);
        free(is_zero);
        free(safe);
        free(raw);
        value.type = TYPE_INT;
    } else {
        sb_printf(&module->body, "  %s = %s %s %s, %s\n", temp, instruction,
                  module->int_type, left.repr, right.repr);
        value.type = TYPE_INT;
    }
    free(left.repr);
    free(right.repr);
    value.repr = temp;
    return value;
}

static CgValue codegen_expr(ModuleGen *module, CgEnv *env, Expr *expr) {
    CgValue value = {.type = expr->inferred_type, .repr = NULL};
    switch (expr->kind) {
        case EXPR_INTEGER:
            value.type = TYPE_INT;
            value.repr = format_alloc("%lld", (long long)expr->as.integer);
            return value;

        case EXPR_STRING: {
            value.type = TYPE_STRING;
            StringEntry *entry = intern_string(&module->strings, expr->as.string);
            value.repr = format_alloc(
                "{ ptr getelementptr inbounds ([%zu x i8], ptr @.baf.str.%d, "
                "%s 0, %s 0), %s %zu }",
                entry->length + 1, entry->id,
                module->int_type, module->int_type,
                module->int_type, entry->length);
            return value;
        }

        case EXPR_BOOL:
            value.type = TYPE_BOOL;
            value.repr = baf_xstrdup(expr->as.boolean ? "true" : "false");
            return value;

        case EXPR_NAME: {
            CgVar *var = env_find(env, expr->as.name);
            if (!var) {
                value.repr = baf_xstrdup("zeroinitializer");
                return value;
            }
            value.type = var->type;
            char *temp = new_temp(module);
            sb_printf(&module->body, "  %s = load %s, ptr %s, align %d\n",
                      temp, llvm_type(module, var->type), var->llvm_name,
                      type_align(module, var->type));
            value.repr = temp;
            return value;
        }

        case EXPR_BINARY:
            return codegen_binary(module, env, expr);

        case EXPR_UNARY: {
            CgValue operand = codegen_expr(module, env, expr->as.unary.operand);
            char *temp = new_temp(module);
            if (expr->as.unary.op == UNOP_NOT) {
                sb_printf(&module->body, "  %s = xor i1 %s, true\n", temp,
                          operand.repr);
                value.type = TYPE_BOOL;
            } else {
                sb_printf(&module->body, "  %s = sub %s 0, %s\n", temp,
                          module->int_type, operand.repr);
                value.type = TYPE_INT;
            }
            free(operand.repr);
            value.repr = temp;
            return value;
        }

        case EXPR_CALL:
            return codegen_call(module, env, expr->as.call);
    }
    value.repr = baf_xstrdup("zeroinitializer");
    return value;
}

static void codegen_block(ModuleGen *module, CgEnv *env, Block *block);

static void codegen_switch(ModuleGen *module, CgEnv *env, Stmt *stmt) {
    CgValue subject = codegen_expr(module, env, stmt->as.switch_stmt.subject);
    size_t count = stmt->as.switch_stmt.case_count;
    int *case_labels = baf_xmalloc((count ? count : 1) * sizeof(int));
    int end_label = new_label(module);
    int default_label = -1;

    for (size_t i = 0; i < count; i++) {
        case_labels[i] = new_label(module);
        if (stmt->as.switch_stmt.cases[i].is_default) {
            default_label = case_labels[i];
        }
    }

    size_t nondefault_seen = 0;
    size_t nondefault_total = 0;
    for (size_t i = 0; i < count; i++) {
        if (!stmt->as.switch_stmt.cases[i].is_default) nondefault_total++;
    }

    for (size_t i = 0; i < count; i++) {
        SwitchCase *switch_case = &stmt->as.switch_stmt.cases[i];
        if (switch_case->is_default) continue;
        CgValue case_value = codegen_expr(module, env, switch_case->value);
        char *compare = new_temp(module);
        if (subject.type == TYPE_STRING) {
            sb_printf(&module->body,
                      "  %s = call i1 @baf.str.eq(%%baf.str %s, %%baf.str %s)\n",
                      compare, subject.repr, case_value.repr);
        } else {
            sb_printf(&module->body, "  %s = icmp eq %s %s, %s\n", compare,
                      llvm_type(module, subject.type), subject.repr,
                      case_value.repr);
        }
        nondefault_seen++;
        int miss_label;
        bool last = nondefault_seen == nondefault_total;
        if (last) {
            miss_label = default_label >= 0 ? default_label : end_label;
        } else {
            miss_label = new_label(module);
        }
        if (last) {
            if (default_label >= 0) {
                sb_printf(&module->body,
                          "  br i1 %s, label %%switch.case.%d, "
                          "label %%switch.case.%d\n",
                          compare, case_labels[i], default_label);
            } else {
                sb_printf(&module->body,
                          "  br i1 %s, label %%switch.case.%d, "
                          "label %%switch.end.%d\n",
                          compare, case_labels[i], end_label);
            }
        } else {
            sb_printf(&module->body,
                      "  br i1 %s, label %%switch.case.%d, "
                      "label %%switch.test.%d\n",
                      compare, case_labels[i], miss_label);
            sb_printf(&module->body, "switch.test.%d:\n", miss_label);
        }
        free(compare);
        free(case_value.repr);
    }

    if (nondefault_total == 0) {
        sb_printf(&module->body, "  br label %%%s.%d\n",
                  default_label >= 0 ? "switch.case" : "switch.end",
                  default_label >= 0 ? default_label : end_label);
    }

    for (size_t i = 0; i < count; i++) {
        sb_printf(&module->body, "switch.case.%d:\n", case_labels[i]);
        size_t scope_start = env->count;
        codegen_block(module, env, stmt->as.switch_stmt.cases[i].body);
        env_pop_to(env, scope_start);
        sb_printf(&module->body, "  br label %%switch.end.%d\n", end_label);
    }
    sb_printf(&module->body, "switch.end.%d:\n", end_label);

    free(subject.repr);
    free(case_labels);
}

static void codegen_block(ModuleGen *module, CgEnv *env, Block *block) {
    for (size_t i = 0; i < block->count; i++) {
        Stmt *stmt = block->items[i];
        switch (stmt->kind) {
            case STMT_VAR: {
                char *slot = new_slot(module, stmt->as.var.name);
                const char *type = llvm_type(module, stmt->as.var.type);
                int align = type_align(module, stmt->as.var.type);
                sb_printf(&module->prologue, "  %s = alloca %s, align %d\n",
                          slot, type, align);
                if (stmt->as.var.initializer) {
                    CgValue init = codegen_expr(module, env,
                                                stmt->as.var.initializer);
                    sb_printf(&module->body,
                              "  store %s %s, ptr %s, align %d\n",
                              type, init.repr, slot, align);
                    free(init.repr);
                } else {
                    sb_printf(&module->body,
                              "  store %s zeroinitializer, ptr %s, align %d\n",
                              type, slot, align);
                }
                env_add(env, stmt->as.var.name, stmt->as.var.type, slot);
                free(slot);
                break;
            }

            case STMT_ASSIGN: {
                CgVar *var = env_find(env, stmt->as.assign.name);
                CgValue rhs = codegen_expr(module, env, stmt->as.assign.value);
                sb_printf(&module->body,
                          "  store %s %s, ptr %s, align %d\n",
                          llvm_type(module, var->type), rhs.repr, var->llvm_name,
                          type_align(module, var->type));
                free(rhs.repr);
                break;
            }

            case STMT_CALL: {
                CgValue ignored = codegen_call(module, env, stmt->as.call);
                free(ignored.repr);
                break;
            }

            case STMT_WHILE: {
                int cond_label = new_label(module);
                int body_label = new_label(module);
                int end_label = new_label(module);
                sb_printf(&module->body, "  br label %%while.cond.%d\n",
                          cond_label);
                sb_printf(&module->body, "while.cond.%d:\n", cond_label);
                CgValue condition = codegen_expr(module, env,
                                                  stmt->as.while_stmt.condition);
                sb_printf(&module->body,
                          "  br i1 %s, label %%while.body.%d, "
                          "label %%while.end.%d\n",
                          condition.repr, body_label, end_label);
                free(condition.repr);
                sb_printf(&module->body, "while.body.%d:\n", body_label);
                size_t scope_start = env->count;
                char *break_target = format_alloc("while.end.%d", end_label);
                char *continue_target = format_alloc("while.cond.%d", cond_label);
                loop_push(module, break_target, continue_target);
                free(break_target);
                free(continue_target);
                codegen_block(module, env, stmt->as.while_stmt.body);
                loop_pop(module);
                env_pop_to(env, scope_start);
                sb_printf(&module->body, "  br label %%while.cond.%d\n",
                          cond_label);
                sb_printf(&module->body, "while.end.%d:\n", end_label);
                break;
            }

            case STMT_FOR: {
                size_t scope_start = env->count;
                if (stmt->as.for_stmt.init) {
                    Block single = {.items = &stmt->as.for_stmt.init, .count = 1,
                                    .capacity = 1};
                    codegen_block(module, env, &single);
                }
                int cond_label = new_label(module);
                int body_label = new_label(module);
                int step_label = new_label(module);
                int end_label = new_label(module);
                sb_printf(&module->body, "  br label %%for.cond.%d\n", cond_label);
                sb_printf(&module->body, "for.cond.%d:\n", cond_label);
                if (stmt->as.for_stmt.condition) {
                    CgValue condition = codegen_expr(module, env,
                                                     stmt->as.for_stmt.condition);
                    sb_printf(&module->body,
                              "  br i1 %s, label %%for.body.%d, "
                              "label %%for.end.%d\n",
                              condition.repr, body_label, end_label);
                    free(condition.repr);
                } else {
                    sb_printf(&module->body, "  br label %%for.body.%d\n",
                              body_label);
                }
                sb_printf(&module->body, "for.body.%d:\n", body_label);
                size_t body_scope = env->count;
                char *break_target = format_alloc("for.end.%d", end_label);
                char *continue_target = format_alloc("for.step.%d", step_label);
                loop_push(module, break_target, continue_target);
                free(break_target);
                free(continue_target);
                codegen_block(module, env, stmt->as.for_stmt.body);
                loop_pop(module);
                env_pop_to(env, body_scope);
                sb_printf(&module->body, "  br label %%for.step.%d\n", step_label);
                sb_printf(&module->body, "for.step.%d:\n", step_label);
                if (stmt->as.for_stmt.step) {
                    Block single = {.items = &stmt->as.for_stmt.step, .count = 1,
                                    .capacity = 1};
                    codegen_block(module, env, &single);
                }
                sb_printf(&module->body, "  br label %%for.cond.%d\n", cond_label);
                sb_printf(&module->body, "for.end.%d:\n", end_label);
                env_pop_to(env, scope_start);
                break;
            }

            case STMT_IF: {
                int end_label = new_label(module);
                size_t branch_count = stmt->as.if_stmt.branch_count;
                for (size_t b = 0; b < branch_count; b++) {
                    IfBranch *branch = &stmt->as.if_stmt.branches[b];
                    size_t scope_start = env->count;
                    if (!branch->condition) {
                        codegen_block(module, env, branch->body);
                        env_pop_to(env, scope_start);
                        break;
                    }
                    int then_label = new_label(module);
                    int next_label = new_label(module);
                    CgValue condition = codegen_expr(module, env,
                                                     branch->condition);
                    sb_printf(&module->body,
                              "  br i1 %s, label %%if.then.%d, "
                              "label %%if.next.%d\n",
                              condition.repr, then_label, next_label);
                    free(condition.repr);
                    sb_printf(&module->body, "if.then.%d:\n", then_label);
                    codegen_block(module, env, branch->body);
                    env_pop_to(env, scope_start);
                    sb_printf(&module->body, "  br label %%if.end.%d\n",
                              end_label);
                    sb_printf(&module->body, "if.next.%d:\n", next_label);
                }
                sb_printf(&module->body, "  br label %%if.end.%d\n", end_label);
                sb_printf(&module->body, "if.end.%d:\n", end_label);
                break;
            }

            case STMT_RETURN: {
                if (stmt->as.return_value) {
                    CgValue result = codegen_expr(module, env,
                                                  stmt->as.return_value);
                    sb_printf(&module->body, "  ret %s %s\n",
                              llvm_type(module, module->current_return_type),
                              result.repr);
                    free(result.repr);
                } else if (module->in_entry) {
                    sb_append(&module->body,
                              module->target == BAF_TARGET_I386_FREESTANDING
                                  ? "  ret void\n"
                                  : "  ret i32 0\n");
                } else if (module->current_return_type == TYPE_VOID) {
                    sb_append(&module->body, "  ret void\n");
                } else {
                    sb_printf(&module->body, "  ret %s zeroinitializer\n",
                              llvm_type(module, module->current_return_type));
                }
                start_dead_block(module);
                break;
            }

            case STMT_BREAK:
            case STMT_CONTINUE: {
                if (module->loop_count == 0) break;
                LoopFrame *frame = &module->loops[module->loop_count - 1];
                sb_printf(&module->body, "  br label %%%s\n",
                          stmt->kind == STMT_BREAK ? frame->break_label
                                                   : frame->continue_label);
                start_dead_block(module);
                break;
            }

            case STMT_SWITCH:
                codegen_switch(module, env, stmt);
                break;
        }
    }
}

static void codegen_function(ModuleGen *module, Function *function) {
    module->next_temp = 0;
    module->next_label = 0;
    module->next_slot = 0;
    module->loop_count = 0;
    module->in_entry = false;
    module->current_return_type = function->return_type;
    sb_free(&module->prologue);
    sb_init(&module->prologue);

    sb_printf(&module->body, "define %s%s @baf.%s(",
              function->is_static ? "internal " : "",
              llvm_type(module, function->return_type), function->name);
    for (size_t i = 0; i < function->param_count; i++) {
        if (i) sb_append(&module->body, ", ");
        sb_printf(&module->body, "%s %%p.%s",
                  llvm_type(module, function->params[i].type),
                  function->params[i].name);
    }
    sb_append(&module->body, ") {\nentry:\n");
    size_t prologue_mark = module->body.len;

    CgEnv env = {0};
    for (size_t i = 0; i < function->param_count; i++) {
        Parameter *param = &function->params[i];
        char *slot = new_slot(module, param->name);
        int align = type_align(module, param->type);
        sb_printf(&module->prologue, "  %s = alloca %s, align %d\n",
                  slot, llvm_type(module, param->type), align);
        sb_printf(&module->body,
                  "  store %s %%p.%s, ptr %s, align %d\n",
                  llvm_type(module, param->type), param->name, slot, align);
        env_add(&env, param->name, param->type, slot);
        free(slot);
    }

    codegen_block(module, &env, function->body);
    if (function->return_type == TYPE_VOID) {
        sb_append(&module->body, "  ret void\n}\n\n");
    } else {
        /* Unreachable in a well-typed program: the analyser has already proved
           that every path returns. It keeps the IR valid all the same. */
        sb_printf(&module->body, "  ret %s zeroinitializer\n}\n\n",
                  llvm_type(module, function->return_type));
    }
    if (module->prologue.data) {
        sb_insert(&module->body, prologue_mark, module->prologue.data);
    }
    env_free(&env);
}

static void codegen_entry(ModuleGen *module, Block *begin_block) {
    module->next_temp = 0;
    module->next_label = 0;
    module->next_slot = 0;
    module->loop_count = 0;
    module->in_entry = true;
    module->current_return_type = TYPE_VOID;
    sb_free(&module->prologue);
    sb_init(&module->prologue);

    if (module->target == BAF_TARGET_I386_FREESTANDING) {
        sb_append(&module->body, "define void @baf.begin() {\nentry:\n");
    } else {
        sb_append(&module->body, "define i32 @main() {\nentry:\n");
    }
    size_t prologue_mark = module->body.len;
    CgEnv env = {0};
    codegen_block(module, &env, begin_block);
    if (module->target == BAF_TARGET_I386_FREESTANDING) {
        sb_append(&module->body, "  ret void\n}\n");
    } else {
        sb_append(&module->body, "  ret i32 0\n}\n");
    }
    if (module->prologue.data) {
        sb_insert(&module->body, prologue_mark, module->prologue.data);
    }
    env_free(&env);
    module->in_entry = false;
}

static void emit_string_globals(StrBuf *out, StringPool *pool) {
    for (size_t i = 0; i < pool->count; i++) {
        StringEntry *entry = &pool->items[i];
        sb_printf(out,
                  "@.baf.str.%d = private unnamed_addr constant [%zu x i8] c\"",
                  entry->id, entry->length + 1);
        append_escaped_bytes(out, entry->text, entry->length);
        sb_append(out, "\\00\", align 1\n");
    }
    if (pool->count) sb_append(out, "\n");
}

static void emit_input_globals(StrBuf *out, int count) {
    for (int i = 0; i < count; i++) {
        sb_printf(out,
                  "@.baf.input.%d = internal global [256 x i8] "
                  "zeroinitializer, align 1\n",
                  i);
    }
    if (count > 0) sb_append(out, "\n");
}

static void emit_source_filename(StrBuf *out, const char *path) {
    sb_append(out, "source_filename = \"");
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        if (*p == '"' || *p == '\\') {
            sb_printf(out, "\\%02X", *p);
        } else {
            sb_append_n(out, (const char *)p, 1);
        }
    }
    sb_append(out, "\"\n\n");
}

bool emit_llvm_ir(Program *program, const char *source_path,
                  const char *output_path, BafTarget target,
                  Diagnostics *diag) {
    ModuleGen module;
    memset(&module, 0, sizeof(module));
    module.target = target;
    module.int_type = target == BAF_TARGET_I386_FREESTANDING ? "i32" : "i64";
    module.int_align = target == BAF_TARGET_I386_FREESTANDING ? 4 : 8;
    module.ptr_align = target == BAF_TARGET_I386_FREESTANDING ? 4 : 8;
    sb_init(&module.body);
    sb_init(&module.prologue);

    for (size_t i = 0; i < program->function_count; i++) {
        codegen_function(&module, program->functions[i]);
    }
    codegen_entry(&module, program->begin_block);

    StrBuf output;
    sb_init(&output);
    sb_append(&output, "; Generated by BackAndForth stage-zero compiler\n");
    emit_source_filename(&output, source_path);
    if (target == BAF_TARGET_I386_FREESTANDING) {
        sb_append(&output, "target triple = \"i386-unknown-none\"\n\n");
    }
    sb_printf(&output, "%%baf.str = type { ptr, %s }\n\n", module.int_type);
    sb_append(&output, "declare void @baf.putsc(%baf.str)\n");
    sb_append(&output, "declare void @baf.putl(%baf.str)\n");
    sb_printf(&output, "declare void @baf.put.int(%s)\n", module.int_type);
    sb_append(&output, "declare void @baf.put.bool(i1)\n");
    sb_append(&output, "declare void @baf.put.newline()\n");
    sb_printf(&output, "declare %%baf.str @baf.input.read(ptr, %s)\n",
              module.int_type);
    sb_append(&output, "declare i1 @baf.str.eq(%baf.str, %baf.str)\n");
    sb_append(&output, "declare void @baf.console.clear()\n");
    sb_printf(&output, "declare void @baf.console.set_text_color(%s)\n", module.int_type);
    sb_printf(&output, "declare void @baf.console.set_background_color(%s)\n", module.int_type);
    sb_append(&output, "declare void @baf.power.shutdown()\n");
    sb_append(&output, "declare void @baf.power.reboot()\n");
    sb_append(&output, "declare void @baf.disk.scan()\n");
    sb_printf(&output, "declare %s @baf.disk.count()\n", module.int_type);
    sb_append(&output, "declare void @baf.disk.list()\n");
    sb_printf(&output, "declare void @baf.disk.hex(%s, %s)\n",
              module.int_type, module.int_type);
    sb_printf(&output, "declare i1 @baf.disk.select(%s)\n", module.int_type);
    sb_append(&output, "declare i1 @baf.disk.format()\n");
    sb_append(&output, "declare void @baf.disk.files()\n");
    sb_append(&output, "declare i1 @baf.disk.write(%baf.str, %baf.str)\n");
    sb_append(&output, "declare %baf.str @baf.disk.read(%baf.str)\n");
    sb_append(&output, "declare i1 @baf.disk.rem(%baf.str)\n");
    sb_append(&output, "declare i1 @baf.disk.exists(%baf.str)\n");
    sb_printf(&output, "declare %s @baf.disk.size(%%baf.str)\n", module.int_type);
    sb_append(&output, "declare void @baf.disk.info()\n");
    sb_append(&output, "declare i1 @baf.disk.create_dir(%baf.str)\n");
    sb_append(&output, "declare i1 @baf.disk.goto_dir(%baf.str)\n");
    sb_append(&output, "declare %baf.str @baf.disk.get_dir()\n");
    sb_append(&output, "declare %baf.str @baf.str.concat(%baf.str, %baf.str)\n");
    sb_printf(&output, "declare %%baf.str @baf.str.sub(%%baf.str, %s, %s)\n",
              module.int_type, module.int_type);
    sb_printf(&output, "declare %%baf.str @baf.str.from_int(%s)\n",
              module.int_type);
    sb_append(&output, "declare %baf.str @baf.str.from_bool(i1)\n");
    sb_printf(&output, "declare %s @baf.str.to_int(%%baf.str)\n\n",
              module.int_type);
    emit_string_globals(&output, &module.strings);
    emit_input_globals(&output, module.next_input_buffer);
    sb_append(&output, module.body.data ? module.body.data : "");

    FILE *file = fopen(output_path, "wb");
    if (!file) {
        diag_error(diag, 1, 1, "cannot create '%s': %s", output_path,
                   strerror(errno));
        sb_free(&output);
        sb_free(&module.body);
        pool_free(&module.strings);
        return false;
    }
    size_t written = fwrite(output.data, 1, output.len, file);
    if (written != output.len || fclose(file) != 0) {
        diag_error(diag, 1, 1, "failed while writing '%s'", output_path);
        sb_free(&output);
        sb_free(&module.body);
        pool_free(&module.strings);
        return false;
    }

    sb_free(&output);
    sb_free(&module.body);
    sb_free(&module.prologue);
    pool_free(&module.strings);
    return true;
}
