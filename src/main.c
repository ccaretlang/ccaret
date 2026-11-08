#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semantic/semantic.h"
#include "source/source.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

    fprintf(stderr, "ccaret: unknown command: %s\n", argv[1]);
    return 1;
}
