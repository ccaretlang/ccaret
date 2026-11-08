#include "backend/c/emitter.h"
#include "ir/ir.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic.h"
#include "source/source.h"

#include <stdbool.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define CCARET_VERSION "0.1.0-dev"

static void print_help(void)
{
    puts("C^ (C Caret) Compiler");
    puts("");
    puts("Usage: ccaret <command> [options]");
    puts("");
    puts("Commands:");
    puts("  tokens <file>  Print the token stream");
    puts("  check <file>   Parse and type-check a C^ file");
    puts("  emit-ir <file> [-o <file>]    Write verified typed IR");
    puts("  emit-c <file> -o <file.c>     Emit C17 from verified IR");
    puts("  build <file> -o <executable> Compile C^ subset with GCC");
    puts("  --help         Show this help");
    puts("  --version      Show compiler version");
}

static void print_source_error(const char *path, const CcSourceError *error)
{
    fprintf(stderr, "ccaret: %s: %s", path,
            cc_source_status_string(error->status));

    if (error->status == CC_SOURCE_IO_ERROR && error->os_error != 0) {
        fprintf(stderr, ": %s", strerror(error->os_error));
    } else if (error->status == CC_SOURCE_INVALID_UTF8 ||
               error->status == CC_SOURCE_EMBEDDED_NUL) {
        fprintf(stderr, " at byte %zu", error->offset);
    }

    fputc('\n', stderr);
}

static void print_escaped(const unsigned char *bytes, size_t length)
{
    putchar('"');

    for (size_t i = 0; i < length; i++) {
        unsigned char c = bytes[i];

        switch (c) {
        case '\\': fputs("\\\\", stdout); break;
        case '"': fputs("\\\"", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (c >= 0x20 && c < 0x7F)
                putchar(c);
            else
                printf("\\x%02X", c);
            break;
        }
    }

    putchar('"');
}

static int run_tokens(const char *path)
{
    CcSourceManager sources;
    CcSourceError error = {0};
    CcFileId file_id;
    bool had_error = false;

    cc_source_manager_init(&sources);

    if (!cc_source_add_file(&sources, path, &file_id, &error)) {
        print_source_error(path, &error);
        cc_source_manager_destroy(&sources);
        return 1;
    }

    const CcSourceFile *file = cc_source_get(&sources, file_id);

    if (file == NULL) {
        fprintf(stderr, "ccaret: %s: could not retrieve source file\n", path);
        cc_source_manager_destroy(&sources);
        return 1;
    }

    CcLexer lexer;

    cc_lexer_init(&lexer, file, file_id);

    for (;;) {
        CcToken token = cc_lexer_next(&lexer);
        size_t line = 0;
        size_t column = 0;

        if (!cc_source_location(file, token.span.start, &line, &column)) {
            fprintf(stderr, "ccaret: %s: invalid token position\n", path);
            cc_source_manager_destroy(&sources);
            return 1;
        }

        printf("%zu:%zu: %s ", line, column,
               cc_token_kind_name(token.kind));
        print_escaped(file->bytes + token.span.start,
                      (size_t)(token.span.end - token.span.start));
        putchar('\n');

        if (token.kind == CC_TOKEN_INVALID) {
            fprintf(stderr, "%s:%zu:%zu: error: %s\n", path, line, column,
                    cc_lex_error_message(lexer.error));
            had_error = true;
        }

        if (token.kind == CC_TOKEN_EOF) break;
    }

    cc_source_manager_destroy(&sources);
    return had_error ? 1 : 0;
}

static void print_diagnostic(const char *path, const CcSourceFile *file,
                             CcSourceSpan span, const char *stage,
                             const char *message)
{
    size_t line = 1;
    size_t column = 1;

    if (span.file_id != CC_FILE_ID_INVALID)
        cc_source_location(file, span.start, &line, &column);

    fprintf(stderr, "%s:%zu:%zu: error: %s: %s\n",
            path, line, column, stage, message);
}

static int run_check(const char *path)
{
    CcSourceManager sources;
    CcSourceError source_error = {0};
    CcFileId file_id;

    cc_source_manager_init(&sources);

    if (!cc_source_add_file(&sources, path, &file_id, &source_error)) {
        print_source_error(path, &source_error);
        cc_source_manager_destroy(&sources);
        return 1;
    }

    const CcSourceFile *file = cc_source_get(&sources, file_id);
    CcLexer lexer;
    CcArena arena;
    CcParser parser;
    int status = 0;

    cc_lexer_init(&lexer, file, file_id);
    cc_arena_init(&arena);
    cc_parser_init(&parser, &lexer, &arena);
    CcFunctionDeclaration *functions = cc_parser_parse_program(&parser);

    if (parser.failed) {
        if (parser.current.kind == CC_TOKEN_INVALID &&
            lexer.error != CC_LEX_ERROR_NONE)
            print_diagnostic(path, file, parser.current.span, "lexer",
                             cc_lex_error_message(lexer.error));
        else
            print_diagnostic(path, file, parser.error_span, "parser",
                             parser.error);
        status = 1;
    } else {
        CcSemanticError semantic_error;

        if (!cc_semantic_check(file, functions, &arena, &semantic_error)) {
            print_diagnostic(path, file, semantic_error.span, "semantic",
                             semantic_error.message);
            status = 1;
        }
    }

    cc_arena_destroy(&arena);
    cc_source_manager_destroy(&sources);
    return status;
}

static const char *safe_gcc_path(const char *path, char **allocated)
{
    *allocated = NULL;
    if (path[0] != '-' && path[0] != '@') return path;
    size_t length = strlen(path);
    if (length > SIZE_MAX - 3) return NULL;
    *allocated = malloc(length + 3);
    if (*allocated == NULL) return NULL;
    memcpy(*allocated, "./", 2);
    memcpy(*allocated + 2, path, length + 1);
    return *allocated;
}

static int run_gcc(const char *generated, const char *output)
{
    char *source_allocated = NULL;
    char *output_allocated = NULL;
    /* Protect both input and output from GCC option/response-file parsing. */
    const char *input = safe_gcc_path(generated, &source_allocated);
    const char *destination = safe_gcc_path(output, &output_allocated);
    if (input == NULL || destination == NULL) {
        free(source_allocated);
        free(output_allocated);
        fprintf(stderr, "ccaret: out of memory or path too long\n");
        return 1;
    }
    char *const arguments[] = {
        "gcc", "-std=c17", "-O0", "-g", "-Wall", "-Wextra",
        "-Wpedantic", "-Werror", "-o", (char *)destination,
        (char *)input, NULL
    };
    pid_t pid;
    int code = posix_spawnp(&pid, "gcc", NULL, NULL, arguments, environ);
    free(source_allocated);
    free(output_allocated);

    if (code != 0) {
        fprintf(stderr, "ccaret: could not start GCC: %s\n",
                strerror(code));
        return 1;
    }
    int status = 0;
    for (;;) {
        if (waitpid(pid, &status, 0) >= 0) break;
        if (errno != EINTR) {
            fprintf(stderr, "ccaret: GCC process wait failed: %s\n",
                    strerror(errno));
            return 1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "ccaret: GCC compilation failed\n");
        return 1;
    }
    return 0;
}

static int run_codegen(const char *command, const char *path,
                       const char *output_path)
{
    CcSourceManager sources;
    CcSourceError source_error = {0};
    CcFileId file_id;
    CcArena arena;
    CcLexer lexer;
    CcParser parser;
    CcSemanticError semantic_error;
    CcIrError ir_error;
    CcIrModule *module = NULL;
    char *generated = NULL;
    FILE *output = NULL;
    int result = 1;

    cc_source_manager_init(&sources);
    cc_arena_init(&arena);
    if (!cc_source_add_file(&sources, path, &file_id, &source_error)) {
        print_source_error(path, &source_error);
        goto cleanup;
    }
    const CcSourceFile *file = cc_source_get(&sources, file_id);

    cc_lexer_init(&lexer, file, file_id);
    cc_parser_init(&parser, &lexer, &arena);
    CcFunctionDeclaration *program = cc_parser_parse_program(&parser);
    if (parser.failed) {
        if (parser.current.kind == CC_TOKEN_INVALID &&
            lexer.error != CC_LEX_ERROR_NONE)
            print_diagnostic(path, file, parser.current.span, "lexer",
                             cc_lex_error_message(lexer.error));
        else
            print_diagnostic(path, file, parser.error_span, "parser",
                             parser.error);
        goto cleanup;
    }
    if (!cc_semantic_check(file, program, &arena, &semantic_error)) {
        print_diagnostic(path, file, semantic_error.span, "semantic",
                         semantic_error.message);
        goto cleanup;
    }
    if (!cc_ir_lower(file, program, &arena, &module, &ir_error) ||
        !cc_ir_verify(module, &ir_error)) {
        print_diagnostic(path, file, ir_error.span, "IR", ir_error.message);
        goto cleanup;
    }
    if (strcmp(command, "build") == 0) {
        if (!cc_c_has_main(module)) {
            fprintf(stderr,
                    "ccaret: build requires fn main() -> i32\n");
            goto cleanup;
        }
        size_t size = strlen(output_path);
        if (size > SIZE_MAX - 3) {
            fprintf(stderr, "ccaret: output path too long\n");
            goto cleanup;
        }
        generated = malloc(size + 3);
        if (generated == NULL) {
            fprintf(stderr, "ccaret: out of memory\n");
            goto cleanup;
        }
        memcpy(generated, output_path, size);
        memcpy(generated + size, ".c", 3);
    }

    const char *destination = generated != NULL ? generated : output_path;
    output = destination == NULL ? stdout : fopen(destination, "wb");
    if (output == NULL) {
        fprintf(stderr, "ccaret: could not open output %s: %s\n",
                destination, strerror(errno));
        goto cleanup;
    }
    bool ok = strcmp(command, "emit-ir") == 0
        ? cc_ir_print(output, module)
        : cc_c_emit(output, module, &ir_error);
    if (output != stdout) {
        if (fclose(output) != 0) ok = false;
        output = NULL;
    }
    if (!ok) {
        if (ir_error.message != NULL)
            print_diagnostic(path, file, ir_error.span, "IR", ir_error.message);
        else fprintf(stderr, "ccaret: output write failed\n");
        if (destination != NULL) remove(destination);
        goto cleanup;
    }
    if (generated != NULL)
        result = run_gcc(generated, output_path);
    else result = 0;

cleanup:
    free(generated);
    cc_arena_destroy(&arena);
    cc_source_manager_destroy(&sources);
    return result;
}

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("ccaret %s\n", CCARET_VERSION);
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "tokens") == 0) {
        if (argc != 3) {
            fprintf(stderr, "usage: ccaret tokens <file>\n");
            return 1;
        }

        return run_tokens(argv[2]);
    }

    if (argc >= 2 && strcmp(argv[1], "check") == 0) {
        if (argc != 3) {
            fprintf(stderr, "usage: ccaret check <file>\n");
            return 1;
        }

        return run_check(argv[2]);
    }

    if (argc >= 2 &&
        (strcmp(argv[1], "emit-ir") == 0 ||
         strcmp(argv[1], "emit-c") == 0 ||
         strcmp(argv[1], "build") == 0)) {
        bool ir = strcmp(argv[1], "emit-ir") == 0;
        if (!((ir && argc == 3) ||
              (argc == 5 && strcmp(argv[3], "-o") == 0 &&
               argv[4][0] != '\0'))) {
            fprintf(stderr, "usage: ccaret %s <file>%s\n", argv[1],
                    ir ? " [-o <file>]" : " -o <file>");
            return 1;
        }
        return run_codegen(argv[1], argv[2], argc == 5 ? argv[4] : NULL);
    }

    fprintf(stderr, "ccaret: unknown command: %s\n", argv[1]);
    return 1;
}
