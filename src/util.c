#include "baf.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void *baf_xmalloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) {
        fprintf(stderr, "bafc: out of memory\n");
        exit(2);
    }
    return ptr;
}

void *baf_xrealloc(void *ptr, size_t size) {
    void *next = realloc(ptr, size == 0 ? 1 : size);
    if (!next) {
        fprintf(stderr, "bafc: out of memory\n");
        exit(2);
    }
    return next;
}

char *baf_xstrdup(const char *text) {
    return baf_xstrndup(text, strlen(text));
}

char *baf_xstrndup(const char *text, size_t length) {
    char *copy = baf_xmalloc(length + 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *baf_read_file(const char *path, size_t *length_out) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "bafc: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "bafc: cannot seek '%s'\n", path);
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "bafc: cannot read '%s'\n", path);
        fclose(file);
        return NULL;
    }

    char *data = baf_xmalloc((size_t)length + 1);
    size_t read_count = fread(data, 1, (size_t)length, file);
    if (read_count != (size_t)length) {
        fprintf(stderr, "bafc: short read from '%s'\n", path);
        free(data);
        fclose(file);
        return NULL;
    }
    data[length] = '\0';
    fclose(file);
    if (length_out) {
        *length_out = (size_t)length;
    }
    return data;
}

void sb_init(StrBuf *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void sb_reserve(StrBuf *buf, size_t needed) {
    if (needed <= buf->cap) {
        return;
    }
    size_t next = buf->cap ? buf->cap : 256;
    while (next < needed) {
        next *= 2;
    }
    buf->data = baf_xrealloc(buf->data, next);
    buf->cap = next;
}

void sb_append_n(StrBuf *buf, const char *text, size_t n) {
    sb_reserve(buf, buf->len + n + 1);
    memcpy(buf->data + buf->len, text, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
}

void sb_append(StrBuf *buf, const char *text) {
    sb_append_n(buf, text, strlen(text));
}

void sb_printf(StrBuf *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return;
    }
    sb_reserve(buf, buf->len + (size_t)needed + 1);
    vsnprintf(buf->data + buf->len, (size_t)needed + 1, fmt, args);
    buf->len += (size_t)needed;
    va_end(args);
}

void sb_free(StrBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void diag_error(Diagnostics *diag, int line, int column, const char *fmt, ...) {
    fprintf(stderr, "%s:%d:%d: error: ", diag->path, line, column);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    diag->errors++;
}

const char *baf_type_name(BafType type) {
    switch (type) {
        case TYPE_UNKNOWN: return "unknown";
        case TYPE_VOID: return "void";
        case TYPE_INT: return "int";
        case TYPE_STRING: return "str";
        case TYPE_BOOL: return "bool";
    }
    return "invalid";
}

const char *baf_binop_name(BinaryOp op) {
    switch (op) {
        case BINOP_ADD: return "+";
        case BINOP_SUB: return "-";
        case BINOP_MUL: return "*";
        case BINOP_DIV: return "/";
        case BINOP_MOD: return "%";
        case BINOP_EQ: return "==";
        case BINOP_NE: return "!=";
        case BINOP_LT: return "<";
        case BINOP_LE: return "<=";
        case BINOP_GT: return ">";
        case BINOP_GE: return ">=";
        case BINOP_AND: return "&&";
        case BINOP_OR: return "||";
    }
    return "?";
}
