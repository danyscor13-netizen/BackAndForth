#include "baf.h"

#include <stdlib.h>
#include <string.h>

static void print_usage(FILE *stream) {
    fprintf(stream,
            "usage: bafc <source.baf> [-o output.ll] [--check] [--target TARGET]\n"
            "\n"
            "Compiles BackAndForth source into textual LLVM IR.\n"
            "Targets: hosted (default), i386-freestanding.\n");
}

static char *default_output_path(const char *source_path) {
    const char *slash = strrchr(source_path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(source_path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    const char *base = slash ? slash + 1 : source_path;
    const char *dot = strrchr(base, '.');
    size_t prefix = dot ? (size_t)(dot - source_path) : strlen(source_path);
    char *output = baf_xmalloc(prefix + 4);
    memcpy(output, source_path, prefix);
    memcpy(output + prefix, ".ll", 4);
    return output;
}

int main(int argc, char **argv) {
    const char *source_path = NULL;
    const char *output_path_arg = NULL;
    bool check_only = false;
    BafTarget target = BAF_TARGET_HOSTED;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            puts("BackAndForth compiler 0.7.1");
            return 0;
        }
        if (strcmp(argv[i], "--check") == 0) {
            check_only = true;
            continue;
        }
        if (strcmp(argv[i], "--target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "bafc: '--target' requires a target name\n");
                return 2;
            }
            if (!parse_baf_target(argv[++i], &target)) {
                fprintf(stderr, "bafc: unknown target '%s'\n", argv[i]);
                fprintf(stderr, "bafc: available targets: hosted, i386-freestanding\n");
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "bafc: '-o' requires a path\n");
                return 2;
            }
            output_path_arg = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "bafc: unknown option '%s'\n", argv[i]);
            print_usage(stderr);
            return 2;
        }
        if (source_path) {
            fprintf(stderr, "bafc: only one root source file is supported\n");
            return 2;
        }
        source_path = argv[i];
    }

    if (!source_path) {
        print_usage(stderr);
        return 2;
    }

    Diagnostics diag = {.path = source_path, .errors = 0};
    char *source = baf_expand_includes(source_path, &diag);
    if (!source) return 1;

    TokenArray tokens = {0};
    Program *program = NULL;
    char *owned_output_path = NULL;
    int result = 1;

    if (!lex_source(source, &tokens, &diag)) goto cleanup;
    program = parse_program(&tokens, &diag);
    if (diag.errors) goto cleanup;
    if (!analyze_program(program, &diag)) goto cleanup;

    if (check_only) {
        printf("%s: OK\n", source_path);
        result = 0;
        goto cleanup;
    }

    const char *output_path = output_path_arg;
    if (!output_path) {
        owned_output_path = default_output_path(source_path);
        output_path = owned_output_path;
    }
    if (!emit_llvm_ir(program, source_path, output_path, target, &diag)) goto cleanup;
    printf("wrote %s\n", output_path);
    result = 0;

cleanup:
    free(owned_output_path);
    program_free(program);
    token_array_free(&tokens);
    free(source);
    return result;
}
