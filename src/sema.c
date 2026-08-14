#include "baf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    BafType type;
    BafType *external_type;
    Token token;
} Binding;

typedef struct {
    Binding *items;
    size_t count;
    size_t capacity;
} Environment;

typedef struct {
    const char *name;
    BuiltinKind kind;
    BafType return_type;
    size_t parameter_count;
    const char *parameter_names[3];
    BafType parameter_types[3];
    bool variadic;
} BuiltinSpec;

static const BuiltinSpec BUILTINS[] = {
    {"putsc", BUILTIN_PUTSC, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, true},
    {"putl", BUILTIN_PUTL, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, true},
    {"inpt", BUILTIN_INPT, TYPE_STRING, 0, {NULL}, {TYPE_UNKNOWN}, true},
    {"clearc", BUILTIN_CONSOLE_CLEAR, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Console.Clear", BUILTIN_CONSOLE_CLEAR, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Console.SetTextColor", BUILTIN_CONSOLE_SET_TEXT_COLOR, TYPE_VOID, 1, {"color"}, {TYPE_INT}, false},
    {"Console.SetTextBackgroundColor", BUILTIN_CONSOLE_SET_BACKGROUND_COLOR, TYPE_VOID, 1, {"color"}, {TYPE_INT}, false},
    {"console.setTextColor", BUILTIN_CONSOLE_SET_TEXT_COLOR, TYPE_VOID, 1, {"color"}, {TYPE_INT}, false},
    {"console.setTextBackgroundColor", BUILTIN_CONSOLE_SET_BACKGROUND_COLOR, TYPE_VOID, 1, {"color"}, {TYPE_INT}, false},
    {"Power.Shutdown", BUILTIN_POWER_SHUTDOWN, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Power.Reboot", BUILTIN_POWER_REBOOT, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.Scan", BUILTIN_DISK_SCAN, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.Count", BUILTIN_DISK_COUNT, TYPE_INT, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.List", BUILTIN_DISK_LIST, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.Hex", BUILTIN_DISK_HEX, TYPE_VOID, 2, {"disk", "lba"}, {TYPE_INT, TYPE_INT}, false},
    {"Disk.Select", BUILTIN_DISK_SELECT, TYPE_BOOL, 1, {"disk"}, {TYPE_INT}, false},
    {"Disk.Format", BUILTIN_DISK_FORMAT, TYPE_BOOL, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.Files", BUILTIN_DISK_FILES, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.Write", BUILTIN_DISK_WRITE, TYPE_BOOL, 2, {"name", "content"}, {TYPE_STRING, TYPE_STRING}, false},
    {"Disk.Read", BUILTIN_DISK_READ, TYPE_STRING, 1, {"name"}, {TYPE_STRING}, false},
    {"Disk.Rem", BUILTIN_DISK_REM, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"Disk.Exists", BUILTIN_DISK_EXISTS, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"Disk.Size", BUILTIN_DISK_SIZE, TYPE_INT, 1, {"name"}, {TYPE_STRING}, false},
    {"Disk.Info", BUILTIN_DISK_INFO, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Disk.CreateDir", BUILTIN_DISK_CREATE_DIR, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"Disk.GotoDir", BUILTIN_DISK_GOTO_DIR, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"Disk.GetDir", BUILTIN_DISK_GET_DIR, TYPE_STRING, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"disk.select", BUILTIN_DISK_SELECT, TYPE_BOOL, 1, {"disk"}, {TYPE_INT}, false},
    {"disk.format", BUILTIN_DISK_FORMAT, TYPE_BOOL, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"disk.files", BUILTIN_DISK_FILES, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"disk.write", BUILTIN_DISK_WRITE, TYPE_BOOL, 2, {"name", "content"}, {TYPE_STRING, TYPE_STRING}, false},
    {"disk.read", BUILTIN_DISK_READ, TYPE_STRING, 1, {"name"}, {TYPE_STRING}, false},
    {"disk.rem", BUILTIN_DISK_REM, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"disk.exists", BUILTIN_DISK_EXISTS, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"disk.size", BUILTIN_DISK_SIZE, TYPE_INT, 1, {"name"}, {TYPE_STRING}, false},
    {"disk.info", BUILTIN_DISK_INFO, TYPE_VOID, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"disk.createDir", BUILTIN_DISK_CREATE_DIR, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"disk.gotoDir", BUILTIN_DISK_GOTO_DIR, TYPE_BOOL, 1, {"name"}, {TYPE_STRING}, false},
    {"disk.getDir", BUILTIN_DISK_GET_DIR, TYPE_STRING, 0, {NULL}, {TYPE_UNKNOWN}, false},
    {"Str.Length", BUILTIN_STR_LENGTH, TYPE_INT, 1, {"text"}, {TYPE_STRING}, false},
    {"Str.Concat", BUILTIN_STR_CONCAT, TYPE_STRING, 2, {"left", "right"}, {TYPE_STRING, TYPE_STRING}, false},
    {"Str.Sub", BUILTIN_STR_SUB, TYPE_STRING, 3, {"text", "start", "count"}, {TYPE_STRING, TYPE_INT, TYPE_INT}, false},
    {"Str.FromInt", BUILTIN_STR_FROM_INT, TYPE_STRING, 1, {"value"}, {TYPE_INT}, false},
    {"Str.FromBool", BUILTIN_STR_FROM_BOOL, TYPE_STRING, 1, {"value"}, {TYPE_BOOL}, false},
    {"Str.ToInt", BUILTIN_STR_TO_INT, TYPE_INT, 1, {"text"}, {TYPE_STRING}, false},
    {"Str.Equals", BUILTIN_STR_EQ, TYPE_BOOL, 2, {"left", "right"}, {TYPE_STRING, TYPE_STRING}, false},
    {"str.length", BUILTIN_STR_LENGTH, TYPE_INT, 1, {"text"}, {TYPE_STRING}, false},
    {"str.concat", BUILTIN_STR_CONCAT, TYPE_STRING, 2, {"left", "right"}, {TYPE_STRING, TYPE_STRING}, false},
    {"str.sub", BUILTIN_STR_SUB, TYPE_STRING, 3, {"text", "start", "count"}, {TYPE_STRING, TYPE_INT, TYPE_INT}, false},
    {"str.fromInt", BUILTIN_STR_FROM_INT, TYPE_STRING, 1, {"value"}, {TYPE_INT}, false},
    {"str.fromBool", BUILTIN_STR_FROM_BOOL, TYPE_STRING, 1, {"value"}, {TYPE_BOOL}, false},
    {"str.toInt", BUILTIN_STR_TO_INT, TYPE_INT, 1, {"text"}, {TYPE_STRING}, false},
    {"str.equals", BUILTIN_STR_EQ, TYPE_BOOL, 2, {"left", "right"}, {TYPE_STRING, TYPE_STRING}, false},
    {"Math.Abs", BUILTIN_MATH_ABS, TYPE_INT, 1, {"value"}, {TYPE_INT}, false},
    {"Math.Min", BUILTIN_MATH_MIN, TYPE_INT, 2, {"left", "right"}, {TYPE_INT, TYPE_INT}, false},
    {"Math.Max", BUILTIN_MATH_MAX, TYPE_INT, 2, {"left", "right"}, {TYPE_INT, TYPE_INT}, false},
    {"math.abs", BUILTIN_MATH_ABS, TYPE_INT, 1, {"value"}, {TYPE_INT}, false},
    {"math.min", BUILTIN_MATH_MIN, TYPE_INT, 2, {"left", "right"}, {TYPE_INT, TYPE_INT}, false},
    {"math.max", BUILTIN_MATH_MAX, TYPE_INT, 2, {"left", "right"}, {TYPE_INT, TYPE_INT}, false},
};

static const BuiltinSpec *find_builtin(const char *name) {
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return &BUILTINS[i];
    }
    return NULL;
}

static void env_free(Environment *env) {
    free(env->items);
    *env = (Environment){0};
}

static BafType *binding_type(Binding *binding) {
    return binding->external_type ? binding->external_type : &binding->type;
}

static Binding *env_find(Environment *env, const char *name) {
    for (size_t i = env->count; i > 0; i--) {
        if (strcmp(env->items[i - 1].name, name) == 0) {
            return &env->items[i - 1];
        }
    }
    return NULL;
}

static void env_add(Environment *env, const char *name, BafType type,
                    BafType *external_type, Token token) {
    if (env->count == env->capacity) {
        size_t next = env->capacity ? env->capacity * 2 : 8;
        env->items = baf_xrealloc(env->items, next * sizeof(Binding));
        env->capacity = next;
    }
    env->items[env->count++] = (Binding){
        .name = name,
        .type = type,
        .external_type = external_type,
        .token = token,
    };
}

static Function *find_function(Program *program, const char *name) {
    for (size_t i = 0; i < program->function_count; i++) {
        if (strcmp(program->functions[i]->name, name) == 0) {
            return program->functions[i];
        }
    }
    return NULL;
}

static int parameter_index(Function *function, const char *name) {
    for (size_t i = 0; i < function->param_count; i++) {
        if (strcmp(function->params[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int builtin_parameter_index(const BuiltinSpec *builtin, const char *name) {
    for (size_t i = 0; i < builtin->parameter_count; i++) {
        if (strcmp(builtin->parameter_names[i], name) == 0) return (int)i;
    }
    return -1;
}

/* The function whose body is currently being inferred or validated, and how
   many enclosing loops that body has opened. Both are plain file statics
   because the compiler analyses one program at a time on one thread. */
static Function *g_current_function = NULL;
static int g_loop_depth = 0;

static void resolve_expr(Program *program, Expr *expr, Diagnostics *diag);
static void resolve_block_calls(Program *program, Block *block, Diagnostics *diag);

static void resolve_call(Program *program, Call *call, Diagnostics *diag) {
    const BuiltinSpec *builtin = find_builtin(call->callee);
    Function *target = NULL;
    size_t parameter_count = 0;

    if (builtin) {
        call->builtin = builtin->kind;
        call->return_type = builtin->return_type;
        parameter_count = builtin->variadic ? call->arg_count
                                            : builtin->parameter_count;
    } else {
        target = find_function(program, call->callee);
        if (!target) {
            diag_error(diag, call->token.line, call->token.column,
                       "unknown function '%s'", call->callee);
            return;
        }
        call->resolved_function = target;
        call->return_type = target->return_type;
        parameter_count = target->param_count;
    }

    if (builtin && builtin->variadic) {
        for (size_t i = 0; i < call->arg_count; i++) {
            CallArg *arg = &call->args[i];
            resolve_expr(program, arg->value, diag);
            if (arg->name) {
                diag_error(diag, arg->token.line, arg->token.column,
                           "variadic function '%s' does not accept named arguments",
                           call->callee);
            }
            arg->resolved_parameter = (int)i;
        }
        return;
    }

    if (call->arg_count != parameter_count) {
        diag_error(diag, call->token.line, call->token.column,
                   "function '%s' expects %zu argument%s, but %zu %s provided",
                   call->callee, parameter_count,
                   parameter_count == 1 ? "" : "s", call->arg_count,
                   call->arg_count == 1 ? "was" : "were");
    }

    bool *used = baf_xmalloc((parameter_count ? parameter_count : 1) * sizeof(bool));
    memset(used, 0, (parameter_count ? parameter_count : 1) * sizeof(bool));
    size_t next_positional = 0;

    for (size_t i = 0; i < call->arg_count; i++) {
        CallArg *arg = &call->args[i];
        resolve_expr(program, arg->value, diag);
        int index = -1;
        if (arg->name) {
            index = builtin ? builtin_parameter_index(builtin, arg->name)
                            : parameter_index(target, arg->name);
            if (index < 0) {
                diag_error(diag, arg->token.line, arg->token.column,
                           "function '%s' has no parameter named '%s'",
                           call->callee, arg->name);
                continue;
            }
        } else {
            while (next_positional < parameter_count && used[next_positional]) {
                next_positional++;
            }
            if (next_positional < parameter_count) index = (int)next_positional++;
        }

        if (index < 0 || (size_t)index >= parameter_count) {
            diag_error(diag, arg->token.line, arg->token.column,
                       "too many arguments supplied to '%s'", call->callee);
            continue;
        }
        if (used[index]) {
            const char *parameter_name = builtin
                                             ? builtin->parameter_names[index]
                                             : target->params[index].name;
            diag_error(diag, arg->token.line, arg->token.column,
                       "parameter '%s' is supplied more than once", parameter_name);
            continue;
        }
        used[index] = true;
        arg->resolved_parameter = index;
    }

    for (size_t i = 0; i < parameter_count; i++) {
        if (!used[i]) {
            const char *parameter_name = builtin
                                             ? builtin->parameter_names[i]
                                             : target->params[i].name;
            diag_error(diag, call->token.line, call->token.column,
                       "missing argument for parameter '%s' in call to '%s'",
                       parameter_name, call->callee);
        }
    }
    free(used);
}

static void resolve_expr(Program *program, Expr *expr, Diagnostics *diag) {
    if (!expr) return;
    switch (expr->kind) {
        case EXPR_BINARY:
            resolve_expr(program, expr->as.binary.left, diag);
            resolve_expr(program, expr->as.binary.right, diag);
            break;
        case EXPR_UNARY:
            resolve_expr(program, expr->as.unary.operand, diag);
            break;
        case EXPR_CALL:
            resolve_call(program, expr->as.call, diag);
            break;
        case EXPR_INTEGER:
        case EXPR_STRING:
        case EXPR_BOOL:
        case EXPR_NAME:
            break;
    }
}

static void resolve_block_calls(Program *program, Block *block, Diagnostics *diag) {
    for (size_t i = 0; i < block->count; i++) {
        Stmt *stmt = block->items[i];
        switch (stmt->kind) {
            case STMT_VAR: resolve_expr(program, stmt->as.var.initializer, diag); break;
            case STMT_ASSIGN: resolve_expr(program, stmt->as.assign.value, diag); break;
            case STMT_CALL: resolve_call(program, stmt->as.call, diag); break;
            case STMT_WHILE:
                resolve_expr(program, stmt->as.while_stmt.condition, diag);
                resolve_block_calls(program, stmt->as.while_stmt.body, diag);
                break;
            case STMT_SWITCH:
                resolve_expr(program, stmt->as.switch_stmt.subject, diag);
                for (size_t c = 0; c < stmt->as.switch_stmt.case_count; c++) {
                    resolve_expr(program, stmt->as.switch_stmt.cases[c].value, diag);
                    resolve_block_calls(program, stmt->as.switch_stmt.cases[c].body,
                                        diag);
                }
                break;
            case STMT_FOR:
                if (stmt->as.for_stmt.init) {
                    Block single = {.items = &stmt->as.for_stmt.init, .count = 1,
                                    .capacity = 1};
                    resolve_block_calls(program, &single, diag);
                }
                resolve_expr(program, stmt->as.for_stmt.condition, diag);
                if (stmt->as.for_stmt.step) {
                    Block single = {.items = &stmt->as.for_stmt.step, .count = 1,
                                    .capacity = 1};
                    resolve_block_calls(program, &single, diag);
                }
                resolve_block_calls(program, stmt->as.for_stmt.body, diag);
                break;
            case STMT_IF:
                for (size_t b = 0; b < stmt->as.if_stmt.branch_count; b++) {
                    resolve_expr(program, stmt->as.if_stmt.branches[b].condition,
                                 diag);
                    resolve_block_calls(program, stmt->as.if_stmt.branches[b].body,
                                        diag);
                }
                break;
            case STMT_RETURN:
                resolve_expr(program, stmt->as.return_value, diag);
                break;
            case STMT_BREAK:
            case STMT_CONTINUE:
                break;
        }
    }
}

static void validate_declarations(Program *program, Diagnostics *diag) {
    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        if (find_builtin(function->name) || strcmp(function->name, "main") == 0) {
            diag_error(diag, function->token.line, function->token.column,
                       "'%s' is reserved and cannot be declared as a function",
                       function->name);
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(program->functions[j]->name, function->name) == 0) {
                diag_error(diag, function->token.line, function->token.column,
                           "function '%s' is already declared", function->name);
            }
        }
        for (size_t p = 0; p < function->param_count; p++) {
            for (size_t q = 0; q < p; q++) {
                if (strcmp(function->params[p].name,
                           function->params[q].name) == 0) {
                    diag_error(diag, function->params[p].token.line,
                               function->params[p].token.column,
                               "duplicate parameter '%s'",
                               function->params[p].name);
                }
            }
        }
    }
}

static BafType expected_call_parameter(Call *call, int index) {
    if (index < 0) return TYPE_UNKNOWN;
    const BuiltinSpec *builtin = find_builtin(call->callee);
    if (builtin && builtin->variadic) return TYPE_UNKNOWN;
    if (builtin && (size_t)index < builtin->parameter_count) {
        return builtin->parameter_types[index];
    }
    if (call->resolved_function &&
        (size_t)index < call->resolved_function->param_count) {
        return call->resolved_function->params[index].type;
    }
    return TYPE_UNKNOWN;
}

static BafType infer_expr(Expr *expr, Environment *env, BafType expected,
                          bool *changed);

static void infer_call(Call *call, Environment *env, bool *changed) {
    if (call->resolved_function &&
        call->return_type != call->resolved_function->return_type) {
        call->return_type = call->resolved_function->return_type;
        *changed = true;
    }
    for (size_t i = 0; i < call->arg_count; i++) {
        CallArg *arg = &call->args[i];
        BafType expected_type = expected_call_parameter(call,
                                                        arg->resolved_parameter);
        BafType actual = infer_expr(arg->value, env, expected_type, changed);
        if (call->resolved_function && arg->resolved_parameter >= 0) {
            BafType *slot = &call->resolved_function
                                 ->params[arg->resolved_parameter].type;
            if (*slot == TYPE_UNKNOWN && actual != TYPE_UNKNOWN) {
                *slot = actual;
                *changed = true;
                infer_expr(arg->value, env, actual, changed);
            }
        }
    }
}

static BafType infer_expr(Expr *expr, Environment *env, BafType expected,
                          bool *changed) {
    if (!expr) return TYPE_UNKNOWN;
    BafType actual = TYPE_UNKNOWN;

    switch (expr->kind) {
        case EXPR_INTEGER: actual = TYPE_INT; break;
        case EXPR_STRING: actual = TYPE_STRING; break;
        case EXPR_BOOL: actual = TYPE_BOOL; break;
        case EXPR_NAME: {
            Binding *binding = env_find(env, expr->as.name);
            if (binding) {
                BafType *slot = binding_type(binding);
                if (*slot == TYPE_UNKNOWN && expected != TYPE_UNKNOWN) {
                    *slot = expected;
                    *changed = true;
                }
                actual = *slot;
            }
            break;
        }
        case EXPR_BINARY: {
            BinaryOp op = expr->as.binary.op;
            if (op == BINOP_AND || op == BINOP_OR) {
                infer_expr(expr->as.binary.left, env, TYPE_BOOL, changed);
                infer_expr(expr->as.binary.right, env, TYPE_BOOL, changed);
                actual = TYPE_BOOL;
            } else if (op == BINOP_EQ || op == BINOP_NE) {
                BafType left = infer_expr(expr->as.binary.left, env,
                                          TYPE_UNKNOWN, changed);
                BafType right = infer_expr(expr->as.binary.right, env,
                                           TYPE_UNKNOWN, changed);
                if (left == TYPE_UNKNOWN && right != TYPE_UNKNOWN) {
                    infer_expr(expr->as.binary.left, env, right, changed);
                } else if (right == TYPE_UNKNOWN && left != TYPE_UNKNOWN) {
                    infer_expr(expr->as.binary.right, env, left, changed);
                }
                actual = TYPE_BOOL;
            } else if (op == BINOP_ADD) {
                /* '+' is integer addition unless one side is a string, in
                   which case it concatenates. */
                BafType left = infer_expr(expr->as.binary.left, env,
                                          TYPE_UNKNOWN, changed);
                BafType right = infer_expr(expr->as.binary.right, env,
                                           TYPE_UNKNOWN, changed);
                if (left == TYPE_STRING || right == TYPE_STRING) {
                    actual = TYPE_STRING;
                } else {
                    if (left == TYPE_UNKNOWN) {
                        infer_expr(expr->as.binary.left, env, TYPE_INT, changed);
                    }
                    if (right == TYPE_UNKNOWN) {
                        infer_expr(expr->as.binary.right, env, TYPE_INT, changed);
                    }
                    actual = TYPE_INT;
                }
            } else if (op == BINOP_LT || op == BINOP_LE || op == BINOP_GT ||
                       op == BINOP_GE) {
                infer_expr(expr->as.binary.left, env, TYPE_INT, changed);
                infer_expr(expr->as.binary.right, env, TYPE_INT, changed);
                actual = TYPE_BOOL;
            } else {
                infer_expr(expr->as.binary.left, env, TYPE_INT, changed);
                infer_expr(expr->as.binary.right, env, TYPE_INT, changed);
                actual = TYPE_INT;
            }
            break;
        }
        case EXPR_UNARY:
            if (expr->as.unary.op == UNOP_NOT) {
                infer_expr(expr->as.unary.operand, env, TYPE_BOOL, changed);
                actual = TYPE_BOOL;
            } else {
                infer_expr(expr->as.unary.operand, env, TYPE_INT, changed);
                actual = TYPE_INT;
            }
            break;
        case EXPR_CALL:
            infer_call(expr->as.call, env, changed);
            actual = expr->as.call->return_type;
            break;
    }

    expr->inferred_type = actual;
    return actual;
}

static void infer_block(Function *function, Block *block, Environment *env,
                        bool *changed) {
    size_t scope_start = env->count;
    if (function && scope_start == 0) {
        for (size_t i = 0; i < function->param_count; i++) {
            env_add(env, function->params[i].name, TYPE_UNKNOWN,
                    &function->params[i].type, function->params[i].token);
        }
        scope_start = env->count;
    }

    for (size_t i = 0; i < block->count; i++) {
        Stmt *stmt = block->items[i];
        switch (stmt->kind) {
            case STMT_VAR:
                if (stmt->as.var.initializer) {
                    infer_expr(stmt->as.var.initializer, env, stmt->as.var.type,
                               changed);
                }
                env_add(env, stmt->as.var.name, stmt->as.var.type, NULL,
                        stmt->token);
                break;
            case STMT_ASSIGN: {
                Binding *binding = env_find(env, stmt->as.assign.name);
                BafType expected = binding ? *binding_type(binding) : TYPE_UNKNOWN;
                infer_expr(stmt->as.assign.value, env, expected, changed);
                break;
            }
            case STMT_CALL:
                infer_call(stmt->as.call, env, changed);
                break;
            case STMT_WHILE: {
                infer_expr(stmt->as.while_stmt.condition, env, TYPE_BOOL, changed);
                size_t nested_start = env->count;
                infer_block(NULL, stmt->as.while_stmt.body, env, changed);
                env->count = nested_start;
                break;
            }
            case STMT_FOR: {
                size_t nested_start = env->count;
                if (stmt->as.for_stmt.init) {
                    Block single = {.items = &stmt->as.for_stmt.init, .count = 1,
                                    .capacity = 1};
                    infer_block(NULL, &single, env, changed);
                }
                infer_expr(stmt->as.for_stmt.condition, env, TYPE_BOOL, changed);
                infer_block(NULL, stmt->as.for_stmt.body, env, changed);
                if (stmt->as.for_stmt.step) {
                    Block single = {.items = &stmt->as.for_stmt.step, .count = 1,
                                    .capacity = 1};
                    infer_block(NULL, &single, env, changed);
                }
                env->count = nested_start;
                break;
            }
            case STMT_IF: {
                for (size_t b = 0; b < stmt->as.if_stmt.branch_count; b++) {
                    infer_expr(stmt->as.if_stmt.branches[b].condition, env,
                               TYPE_BOOL, changed);
                    size_t nested_start = env->count;
                    infer_block(NULL, stmt->as.if_stmt.branches[b].body, env,
                                changed);
                    env->count = nested_start;
                }
                break;
            }
            case STMT_RETURN: {
                BafType expected = TYPE_UNKNOWN;
                if (g_current_function) {
                    if (g_current_function->explicit_return_type) {
                        expected = g_current_function->return_type;
                    } else if (g_current_function->return_type != TYPE_VOID) {
                        expected = g_current_function->return_type;
                    }
                }
                BafType actual = infer_expr(stmt->as.return_value, env, expected,
                                            changed);
                /* An unannotated function takes the type of what it returns. */
                if (g_current_function && !g_current_function->explicit_return_type &&
                    g_current_function->return_type == TYPE_VOID &&
                    actual != TYPE_UNKNOWN && actual != TYPE_VOID) {
                    g_current_function->return_type = actual;
                    *changed = true;
                }
                break;
            }
            case STMT_BREAK:
            case STMT_CONTINUE:
                break;
            case STMT_SWITCH: {
                BafType subject = infer_expr(stmt->as.switch_stmt.subject, env,
                                             TYPE_UNKNOWN, changed);
                for (size_t c = 0; c < stmt->as.switch_stmt.case_count; c++) {
                    SwitchCase *switch_case = &stmt->as.switch_stmt.cases[c];
                    if (!switch_case->is_default) {
                        BafType case_type = infer_expr(switch_case->value, env,
                                                       subject, changed);
                        if (subject == TYPE_UNKNOWN && case_type != TYPE_UNKNOWN) {
                            infer_expr(stmt->as.switch_stmt.subject, env, case_type,
                                       changed);
                            subject = case_type;
                        }
                    }
                    size_t nested_start = env->count;
                    infer_block(NULL, switch_case->body, env, changed);
                    env->count = nested_start;
                }
                break;
            }
        }
    }
    (void)scope_start;
}

static BafType validate_expr(Expr *expr, Environment *env, BafType expected,
                             Diagnostics *diag);

static void validate_call(Call *call, Environment *env, bool used_as_expression,
                          Diagnostics *diag) {
    if (call->resolved_function) {
        call->return_type = call->resolved_function->return_type;
    }
    if (used_as_expression && call->return_type == TYPE_VOID) {
        diag_error(diag, call->token.line, call->token.column,
                   "function '%s' does not return a value", call->callee);
    }
    const BuiltinSpec *builtin = find_builtin(call->callee);
    for (size_t i = 0; i < call->arg_count; i++) {
        CallArg *arg = &call->args[i];
        BafType expected = expected_call_parameter(call, arg->resolved_parameter);
        BafType actual = validate_expr(arg->value, env, expected, diag);
        if (builtin && builtin->variadic && actual != TYPE_UNKNOWN &&
            actual != TYPE_STRING && actual != TYPE_INT && actual != TYPE_BOOL) {
            diag_error(diag, arg->token.line, arg->token.column,
                       "function '%s' can print only str, int, or bool values",
                       call->callee);
        }
    }
}

static BafType validate_expr(Expr *expr, Environment *env, BafType expected,
                             Diagnostics *diag) {
    if (!expr) return TYPE_UNKNOWN;
    BafType actual = TYPE_UNKNOWN;

    switch (expr->kind) {
        case EXPR_INTEGER: actual = TYPE_INT; break;
        case EXPR_STRING: actual = TYPE_STRING; break;
        case EXPR_BOOL: actual = TYPE_BOOL; break;
        case EXPR_NAME: {
            Binding *binding = env_find(env, expr->as.name);
            if (!binding) {
                diag_error(diag, expr->token.line, expr->token.column,
                           "unknown name '%s'", expr->as.name);
            } else {
                actual = *binding_type(binding);
            }
            break;
        }
        case EXPR_BINARY: {
            BinaryOp op = expr->as.binary.op;
            if (op == BINOP_AND || op == BINOP_OR) {
                validate_expr(expr->as.binary.left, env, TYPE_BOOL, diag);
                validate_expr(expr->as.binary.right, env, TYPE_BOOL, diag);
                actual = TYPE_BOOL;
            } else if (op == BINOP_EQ || op == BINOP_NE) {
                BafType left = validate_expr(expr->as.binary.left, env,
                                             TYPE_UNKNOWN, diag);
                BafType right = validate_expr(expr->as.binary.right, env,
                                              TYPE_UNKNOWN, diag);
                if (left != TYPE_UNKNOWN && right != TYPE_UNKNOWN &&
                    left != right) {
                    diag_error(diag, expr->token.line, expr->token.column,
                               "cannot compare %s with %s",
                               baf_type_name(left), baf_type_name(right));
                } else if (left == TYPE_VOID || right == TYPE_VOID) {
                    diag_error(diag, expr->token.line, expr->token.column,
                               "cannot compare values of type void");
                }
                actual = TYPE_BOOL;
            } else if (op == BINOP_ADD) {
                BafType left = validate_expr(expr->as.binary.left, env,
                                             TYPE_UNKNOWN, diag);
                BafType right = validate_expr(expr->as.binary.right, env,
                                              TYPE_UNKNOWN, diag);
                if (left == TYPE_STRING || right == TYPE_STRING) {
                    /* Concatenation: the non-string side is converted, but it
                       still has to be a printable value. */
                    BafType other = left == TYPE_STRING ? right : left;
                    if (other != TYPE_STRING && other != TYPE_INT &&
                        other != TYPE_BOOL && other != TYPE_UNKNOWN) {
                        diag_error(diag, expr->token.line, expr->token.column,
                                   "cannot concatenate str with %s",
                                   baf_type_name(other));
                    }
                    actual = TYPE_STRING;
                } else {
                    if (left != TYPE_UNKNOWN && left != TYPE_INT) {
                        diag_error(diag, expr->token.line, expr->token.column,
                                   "'+' expects int or str operands, but the "
                                   "left side is %s", baf_type_name(left));
                    }
                    if (right != TYPE_UNKNOWN && right != TYPE_INT) {
                        diag_error(diag, expr->token.line, expr->token.column,
                                   "'+' expects int or str operands, but the "
                                   "right side is %s", baf_type_name(right));
                    }
                    actual = TYPE_INT;
                }
            } else if (op == BINOP_LT || op == BINOP_LE || op == BINOP_GT ||
                       op == BINOP_GE) {
                validate_expr(expr->as.binary.left, env, TYPE_INT, diag);
                validate_expr(expr->as.binary.right, env, TYPE_INT, diag);
                actual = TYPE_BOOL;
            } else {
                validate_expr(expr->as.binary.left, env, TYPE_INT, diag);
                validate_expr(expr->as.binary.right, env, TYPE_INT, diag);
                actual = TYPE_INT;
            }
            break;
        }
        case EXPR_UNARY:
            if (expr->as.unary.op == UNOP_NOT) {
                validate_expr(expr->as.unary.operand, env, TYPE_BOOL, diag);
                actual = TYPE_BOOL;
            } else {
                validate_expr(expr->as.unary.operand, env, TYPE_INT, diag);
                actual = TYPE_INT;
            }
            break;
        case EXPR_CALL:
            validate_call(expr->as.call, env, true, diag);
            actual = expr->as.call->return_type;
            break;
    }

    expr->inferred_type = actual;
    if (expected != TYPE_UNKNOWN && actual != TYPE_UNKNOWN && actual != expected) {
        diag_error(diag, expr->token.line, expr->token.column,
                   "expected %s expression, but found %s",
                   baf_type_name(expected), baf_type_name(actual));
    }
    return actual;
}

static bool valid_switch_type(BafType type) {
    return type == TYPE_STRING || type == TYPE_INT || type == TYPE_BOOL;
}

static void validate_block(Function *function, Block *block, Environment *env,
                           Diagnostics *diag) {
    size_t function_params = env->count;
    if (function && env->count == 0) {
        for (size_t i = 0; i < function->param_count; i++) {
            env_add(env, function->params[i].name, TYPE_UNKNOWN,
                    &function->params[i].type, function->params[i].token);
        }
        function_params = env->count;
    }

    for (size_t i = 0; i < block->count; i++) {
        Stmt *stmt = block->items[i];
        switch (stmt->kind) {
            case STMT_VAR:
                if (env_find(env, stmt->as.var.name)) {
                    diag_error(diag, stmt->token.line, stmt->token.column,
                               "name '%s' is already declared in this scope",
                               stmt->as.var.name);
                }
                if (stmt->as.var.initializer) {
                    validate_expr(stmt->as.var.initializer, env,
                                  stmt->as.var.type, diag);
                }
                env_add(env, stmt->as.var.name, stmt->as.var.type, NULL,
                        stmt->token);
                break;
            case STMT_ASSIGN: {
                Binding *binding = env_find(env, stmt->as.assign.name);
                if (!binding) {
                    diag_error(diag, stmt->token.line, stmt->token.column,
                               "cannot assign to unknown name '%s'",
                               stmt->as.assign.name);
                    validate_expr(stmt->as.assign.value, env, TYPE_UNKNOWN, diag);
                } else {
                    validate_expr(stmt->as.assign.value, env,
                                  *binding_type(binding), diag);
                }
                break;
            }
            case STMT_CALL:
                validate_call(stmt->as.call, env, false, diag);
                break;
            case STMT_WHILE: {
                validate_expr(stmt->as.while_stmt.condition, env, TYPE_BOOL, diag);
                size_t nested_start = env->count;
                g_loop_depth++;
                validate_block(NULL, stmt->as.while_stmt.body, env, diag);
                g_loop_depth--;
                env->count = nested_start;
                break;
            }
            case STMT_FOR: {
                size_t nested_start = env->count;
                if (stmt->as.for_stmt.init) {
                    Block single = {.items = &stmt->as.for_stmt.init, .count = 1,
                                    .capacity = 1};
                    validate_block(NULL, &single, env, diag);
                }
                if (stmt->as.for_stmt.condition) {
                    validate_expr(stmt->as.for_stmt.condition, env, TYPE_BOOL,
                                  diag);
                }
                g_loop_depth++;
                validate_block(NULL, stmt->as.for_stmt.body, env, diag);
                g_loop_depth--;
                if (stmt->as.for_stmt.step) {
                    Block single = {.items = &stmt->as.for_stmt.step, .count = 1,
                                    .capacity = 1};
                    validate_block(NULL, &single, env, diag);
                }
                env->count = nested_start;
                break;
            }
            case STMT_IF: {
                for (size_t b = 0; b < stmt->as.if_stmt.branch_count; b++) {
                    IfBranch *branch = &stmt->as.if_stmt.branches[b];
                    if (branch->condition) {
                        validate_expr(branch->condition, env, TYPE_BOOL, diag);
                    }
                    size_t nested_start = env->count;
                    validate_block(NULL, branch->body, env, diag);
                    env->count = nested_start;
                }
                break;
            }
            case STMT_RETURN: {
                BafType expected = g_current_function
                                       ? g_current_function->return_type
                                       : TYPE_VOID;
                if (!g_current_function) {
                    if (stmt->as.return_value) {
                        diag_error(diag, stmt->token.line, stmt->token.column,
                                   "the begin block cannot return a value");
                        validate_expr(stmt->as.return_value, env, TYPE_UNKNOWN,
                                      diag);
                    }
                    break;
                }
                if (expected == TYPE_VOID) {
                    if (stmt->as.return_value) {
                        diag_error(diag, stmt->token.line, stmt->token.column,
                                   "function '%s' returns void, so 'return' "
                                   "cannot take a value",
                                   g_current_function->name);
                        validate_expr(stmt->as.return_value, env, TYPE_UNKNOWN,
                                      diag);
                    }
                } else if (!stmt->as.return_value) {
                    diag_error(diag, stmt->token.line, stmt->token.column,
                               "function '%s' must return a %s value",
                               g_current_function->name,
                               baf_type_name(expected));
                } else {
                    validate_expr(stmt->as.return_value, env, expected, diag);
                }
                break;
            }
            case STMT_BREAK:
            case STMT_CONTINUE:
                if (g_loop_depth == 0) {
                    diag_error(diag, stmt->token.line, stmt->token.column,
                               "'%s' can only appear inside a while or for loop",
                               stmt->kind == STMT_BREAK ? "break" : "continue");
                }
                break;
            case STMT_SWITCH: {
                BafType subject = validate_expr(stmt->as.switch_stmt.subject, env,
                                                TYPE_UNKNOWN, diag);
                if (subject != TYPE_UNKNOWN && !valid_switch_type(subject)) {
                    diag_error(diag, stmt->token.line, stmt->token.column,
                               "switch supports only str, int, or bool values");
                }
                for (size_t c = 0; c < stmt->as.switch_stmt.case_count; c++) {
                    SwitchCase *switch_case = &stmt->as.switch_stmt.cases[c];
                    if (!switch_case->is_default) {
                        validate_expr(switch_case->value, env, subject, diag);
                        if (switch_case->value &&
                            switch_case->value->kind != EXPR_STRING &&
                            switch_case->value->kind != EXPR_INTEGER &&
                            switch_case->value->kind != EXPR_BOOL) {
                            diag_error(diag, switch_case->token.line,
                                       switch_case->token.column,
                                       "case values must be literals in BackAndForth 0.6");
                        }
                    }
                    size_t nested_start = env->count;
                    validate_block(NULL, switch_case->body, env, diag);
                    env->count = nested_start;
                }
                break;
            }
        }
    }
    (void)function_params;
}

/* Conservative "does every path leave through a return?" check. Loops never
   count as returning, because their body may run zero times. */
static bool block_always_returns(Block *block) {
    if (!block) return false;
    for (size_t i = 0; i < block->count; i++) {
        Stmt *stmt = block->items[i];
        if (stmt->kind == STMT_RETURN) return true;
        if (stmt->kind == STMT_IF) {
            bool has_else = false;
            bool all = true;
            for (size_t b = 0; b < stmt->as.if_stmt.branch_count; b++) {
                IfBranch *branch = &stmt->as.if_stmt.branches[b];
                if (!branch->condition) has_else = true;
                if (!block_always_returns(branch->body)) all = false;
            }
            if (has_else && all) return true;
        }
        if (stmt->kind == STMT_SWITCH) {
            bool has_default = false;
            bool all = true;
            for (size_t c = 0; c < stmt->as.switch_stmt.case_count; c++) {
                if (stmt->as.switch_stmt.cases[c].is_default) has_default = true;
                if (!block_always_returns(stmt->as.switch_stmt.cases[c].body)) {
                    all = false;
                }
            }
            if (has_default && all) return true;
        }
    }
    return false;
}

bool analyze_program(Program *program, Diagnostics *diag) {
    validate_declarations(program, diag);
    for (size_t i = 0; i < program->function_count; i++) {
        resolve_block_calls(program, program->functions[i]->body, diag);
    }
    resolve_block_calls(program, program->begin_block, diag);
    if (diag->errors) return false;

    for (int iteration = 0; iteration < 64; iteration++) {
        bool changed = false;
        for (size_t i = 0; i < program->function_count; i++) {
            Environment env = {0};
            g_current_function = program->functions[i];
            infer_block(program->functions[i], program->functions[i]->body,
                        &env, &changed);
            g_current_function = NULL;
            env_free(&env);
        }
        Environment begin_env = {0};
        infer_block(NULL, program->begin_block, &begin_env, &changed);
        env_free(&begin_env);
        if (!changed) break;
    }

    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        for (size_t p = 0; p < function->param_count; p++) {
            if (function->params[p].type == TYPE_UNKNOWN) {
                diag_error(diag, function->params[p].token.line,
                           function->params[p].token.column,
                           "cannot infer the type of parameter '%s'; annotate it "
                           "as '%s: int', '%s: str', or '%s: bool'",
                           function->params[p].name, function->params[p].name,
                           function->params[p].name, function->params[p].name);
            }
        }
    }

    for (size_t i = 0; i < program->function_count; i++) {
        Function *function = program->functions[i];
        Environment env = {0};
        g_current_function = function;
        g_loop_depth = 0;
        validate_block(function, function->body, &env, diag);
        g_current_function = NULL;
        env_free(&env);
        if (function->return_type != TYPE_VOID &&
            !block_always_returns(function->body)) {
            diag_error(diag, function->token.line, function->token.column,
                       "function '%s' returns %s, but some paths reach the end "
                       "of its body without a return",
                       function->name, baf_type_name(function->return_type));
        }
    }
    Environment begin_env = {0};
    validate_block(NULL, program->begin_block, &begin_env, diag);
    env_free(&begin_env);
    return diag->errors == 0;
}
