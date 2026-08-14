#define _XOPEN_SOURCE 700

#include "baf.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} PathArray;

typedef struct {
    PathArray expanded;
    PathArray stack;
    Diagnostics *root_diag;
} IncludeContext;

static void path_array_free(PathArray *array) {
    for (size_t i = 0; i < array->count; i++) free(array->items[i]);
    free(array->items);
    *array = (PathArray){0};
}

static bool path_array_contains(const PathArray *array, const char *path) {
    for (size_t i = 0; i < array->count; i++) {
        if (strcmp(array->items[i], path) == 0) return true;
    }
    return false;
}

static void path_array_push(PathArray *array, const char *path) {
    if (array->count == array->capacity) {
        size_t next = array->capacity ? array->capacity * 2 : 8;
        array->items = baf_xrealloc(array->items, next * sizeof(char *));
        array->capacity = next;
    }
    array->items[array->count++] = baf_xstrdup(path);
}

static void path_array_pop(PathArray *array) {
    if (array->count == 0) return;
    free(array->items[array->count - 1]);
    array->count--;
}

static char *canonical_path(const char *path) {
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) return NULL;
    return baf_xstrdup(resolved);
}

static char *parent_directory(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return baf_xstrdup(".");
    if (slash == path) return baf_xstrdup("/");
    return baf_xstrndup(path, (size_t)(slash - path));
}

static char *join_path(const char *base, const char *child) {
    if (child[0] == '/') return baf_xstrdup(child);
    size_t base_len = strlen(base);
    size_t child_len = strlen(child);
    bool slash = base_len > 0 && base[base_len - 1] != '/';
    char *result = baf_xmalloc(base_len + (slash ? 1u : 0u) + child_len + 1u);
    memcpy(result, base, base_len);
    size_t at = base_len;
    if (slash) result[at++] = '/';
    memcpy(result + at, child, child_len + 1u);
    return result;
}

static void include_error(IncludeContext *ctx, const char *path, int line,
                          int column, const char *message, const char *detail) {
    Diagnostics local = {.path = path, .errors = 0};
    if (detail) {
        diag_error(&local, line, column, "%s '%s'", message, detail);
    } else {
        diag_error(&local, line, column, "%s", message);
    }
    ctx->root_diag->errors += local.errors;
}

static bool parse_include_line(IncludeContext *ctx, const char *path,
                               const char *line, size_t length, int line_number,
                               bool *is_directive, char **target_out) {
    *is_directive = false;
    *target_out = NULL;

    size_t i = 0;
    while (i < length && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i >= length || line[i] != '[') return true;

    size_t marker = i;
    i++;
    while (i < length && isspace((unsigned char)line[i]) && line[i] != '\n') i++;

    static const char keyword[] = "include";
    if (i + sizeof(keyword) - 1u > length ||
        strncmp(line + i, keyword, sizeof(keyword) - 1u) != 0) {
        return true;
    }
    i += sizeof(keyword) - 1u;
    if (i < length && (isalnum((unsigned char)line[i]) || line[i] == '_')) {
        return true;
    }

    *is_directive = true;
    while (i < length && isspace((unsigned char)line[i]) && line[i] != '\n') i++;
    if (i >= length || line[i] != '"') {
        include_error(ctx, path, line_number, (int)marker + 1,
                      "expected a quoted path in include directive", NULL);
        return false;
    }
    i++;
    size_t start = i;
    while (i < length && line[i] != '"') {
        if (line[i] == '\n' || line[i] == '\r') break;
        i++;
    }
    if (i >= length || line[i] != '"') {
        include_error(ctx, path, line_number, (int)marker + 1,
                      "unterminated include path", NULL);
        return false;
    }
    if (i == start) {
        include_error(ctx, path, line_number, (int)marker + 1,
                      "include path cannot be empty", NULL);
        return false;
    }
    *target_out = baf_xstrndup(line + start, i - start);
    i++;
    while (i < length && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i >= length || line[i] != ']') {
        include_error(ctx, path, line_number, (int)marker + 1,
                      "expected ']' after include path", NULL);
        free(*target_out);
        *target_out = NULL;
        return false;
    }
    i++;
    while (i < length && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i < length && line[i] == '/' && i + 1u < length && line[i + 1u] == '/') {
        return true;
    }
    if (i != length) {
        include_error(ctx, path, line_number, (int)i + 1,
                      "unexpected text after include directive", NULL);
        free(*target_out);
        *target_out = NULL;
        return false;
    }
    return true;
}

static int compare_strings(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static bool expand_path(IncludeContext *ctx, const char *requested_path,
                        StrBuf *output, bool is_root);

static bool expand_directory(IncludeContext *ctx, const char *directory,
                             StrBuf *output) {
    DIR *dir = opendir(directory);
    if (!dir) {
        include_error(ctx, ctx->stack.items[ctx->stack.count - 1], 1, 1,
                      "cannot open include directory", directory);
        return false;
    }

    PathArray files = {0};
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        size_t length = strlen(entry->d_name);
        if (length < 4u || strcmp(entry->d_name + length - 4u, ".baf") != 0) continue;
        char *joined = join_path(directory, entry->d_name);
        struct stat status;
        if (stat(joined, &status) == 0 && S_ISREG(status.st_mode)) {
            path_array_push(&files, joined);
        }
        free(joined);
    }
    closedir(dir);

    qsort(files.items, files.count, sizeof(char *), compare_strings);
    bool ok = true;
    for (size_t i = 0; i < files.count; i++) {
        if (!expand_path(ctx, files.items[i], output, false)) {
            ok = false;
            break;
        }
    }
    path_array_free(&files);
    return ok;
}

static bool expand_file(IncludeContext *ctx, const char *canonical,
                        StrBuf *output, bool is_root) {
    if (path_array_contains(&ctx->stack, canonical)) {
        StrBuf chain;
        sb_init(&chain);
        for (size_t i = 0; i < ctx->stack.count; i++) {
            if (i) sb_append(&chain, " -> ");
            sb_append(&chain, ctx->stack.items[i]);
        }
        sb_append(&chain, " -> ");
        sb_append(&chain, canonical);
        include_error(ctx, canonical, 1, 1, "circular include detected:", chain.data);
        sb_free(&chain);
        return false;
    }
    if (!is_root && path_array_contains(&ctx->expanded, canonical)) return true;

    path_array_push(&ctx->stack, canonical);
    if (!is_root) path_array_push(&ctx->expanded, canonical);

    size_t source_length = 0;
    char *source = baf_read_file(canonical, &source_length);
    if (!source) {
        ctx->root_diag->errors++;
        path_array_pop(&ctx->stack);
        return false;
    }

    char *directory = parent_directory(canonical);
    size_t start = 0;
    int line_number = 1;
    bool ok = true;

    while (start < source_length) {
        size_t end = start;
        while (end < source_length && source[end] != '\n') end++;
        size_t line_length = end - start;
        bool directive = false;
        char *target = NULL;

        if (!parse_include_line(ctx, canonical, source + start, line_length,
                                line_number, &directive, &target)) {
            ok = false;
            free(target);
            break;
        }

        if (directive) {
            char *joined = join_path(directory, target);
            if (!expand_path(ctx, joined, output, false)) ok = false;
            free(joined);
            free(target);
            if (!ok) break;
        } else {
            sb_append_n(output, source + start, line_length);
            sb_append(output, "\n");
        }

        start = end < source_length ? end + 1u : end;
        line_number++;
    }

    free(directory);
    free(source);
    path_array_pop(&ctx->stack);
    return ok;
}

static bool expand_path(IncludeContext *ctx, const char *requested_path,
                        StrBuf *output, bool is_root) {
    char *canonical = canonical_path(requested_path);
    if (!canonical) {
        const char *origin = ctx->stack.count
                                 ? ctx->stack.items[ctx->stack.count - 1]
                                 : requested_path;
        Diagnostics local = {.path = origin, .errors = 0};
        diag_error(&local, 1, 1, "cannot resolve include '%s': %s",
                   requested_path, strerror(errno));
        ctx->root_diag->errors += local.errors;
        return false;
    }

    struct stat status;
    if (stat(canonical, &status) != 0) {
        include_error(ctx, canonical, 1, 1, "cannot stat include", canonical);
        free(canonical);
        return false;
    }

    bool ok;
    if (S_ISDIR(status.st_mode)) {
        path_array_push(&ctx->stack, canonical);
        ok = expand_directory(ctx, canonical, output);
        path_array_pop(&ctx->stack);
    } else if (S_ISREG(status.st_mode)) {
        ok = expand_file(ctx, canonical, output, is_root);
    } else {
        include_error(ctx, canonical, 1, 1,
                      "include path is neither a file nor a directory", canonical);
        ok = false;
    }
    free(canonical);
    return ok;
}

char *baf_expand_includes(const char *source_path, Diagnostics *diag) {
    IncludeContext ctx = {.root_diag = diag};
    StrBuf output;
    sb_init(&output);

    bool ok = expand_path(&ctx, source_path, &output, true);
    path_array_free(&ctx.expanded);
    path_array_free(&ctx.stack);

    if (!ok || diag->errors) {
        sb_free(&output);
        return NULL;
    }
    if (!output.data) return baf_xstrdup("");
    return output.data;
}
