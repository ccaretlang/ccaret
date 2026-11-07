#include "lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    CcSourceManager sources;
    CcFileId id;
    CcLexer lexer;
} LexerFixture;

static void fixture_init(LexerFixture *fixture, const char *text)
{
    CcSourceError error;

    cc_source_manager_init(&fixture->sources);

    assert(cc_source_add_memory(&fixture->sources, "test",
                                (const unsigned char *)text, strlen(text),
                                &fixture->id, &error));
    assert(error.status == CC_SOURCE_OK);

    const CcSourceFile *file = cc_source_get(&fixture->sources, fixture->id);

    assert(file != NULL);
    cc_lexer_init(&fixture->lexer, file, fixture->id);
}

static void fixture_destroy(LexerFixture *fixture)
{
    cc_source_manager_destroy(&fixture->sources);
}

static void expect_token(const char *text, CcTokenKind kind)
{
    LexerFixture fixture;

    fixture_init(&fixture, text);

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == kind);
    assert(token.span.start == 0);
    assert(token.span.end == strlen(text));

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);

    fixture_destroy(&fixture);
}

static void expect_lex_error(const char *text, CcLexErrorKind error)
{
    LexerFixture fixture;

    fixture_init(&fixture, text);

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_INVALID);
    assert(fixture.lexer.error == error);

    fixture_destroy(&fixture);
}

static void test_empty_and_whitespace(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == 0 && token.span.end == 0);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);

    fixture_destroy(&fixture);

    fixture_init(&fixture, " \t\r\n\v\f");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == 6 && token.span.end == 6);

    fixture_destroy(&fixture);
}

static void test_identifiers_and_keywords(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "fn main if ifx var2 _if Fn");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_KW_FN);
    assert(token.span.file_id == fixture.id);
    assert(token.span.start == 0 && token.span.end == 2);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);
    assert(token.span.start == 3 && token.span.end == 7);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_IF);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);

    fixture_destroy(&fixture);
}

static void test_all_keywords(void)
{
    static const char *const text =
        "fn return let var const static "
        "pub type struct union enum "
        "if else match while for in loop break continue "
        "module import as extern "
        "unsafe volatile defer errdefer try "
        "true false some none";

    static const CcTokenKind expected[] = {
        CC_TOKEN_KW_FN,
        CC_TOKEN_KW_RETURN,
        CC_TOKEN_KW_LET,
        CC_TOKEN_KW_VAR,
        CC_TOKEN_KW_CONST,
        CC_TOKEN_KW_STATIC,
        CC_TOKEN_KW_PUB,
        CC_TOKEN_KW_TYPE,
        CC_TOKEN_KW_STRUCT,
        CC_TOKEN_KW_UNION,
        CC_TOKEN_KW_ENUM,
        CC_TOKEN_KW_IF,
        CC_TOKEN_KW_ELSE,
        CC_TOKEN_KW_MATCH,
        CC_TOKEN_KW_WHILE,
        CC_TOKEN_KW_FOR,
        CC_TOKEN_KW_IN,
        CC_TOKEN_KW_LOOP,
        CC_TOKEN_KW_BREAK,
        CC_TOKEN_KW_CONTINUE,
        CC_TOKEN_KW_MODULE,
        CC_TOKEN_KW_IMPORT,
        CC_TOKEN_KW_AS,
        CC_TOKEN_KW_EXTERN,
        CC_TOKEN_KW_UNSAFE,
        CC_TOKEN_KW_VOLATILE,
        CC_TOKEN_KW_DEFER,
        CC_TOKEN_KW_ERRDEFER,
        CC_TOKEN_KW_TRY,
        CC_TOKEN_KW_TRUE,
        CC_TOKEN_KW_FALSE,
        CC_TOKEN_KW_SOME,
        CC_TOKEN_KW_NONE
    };

    LexerFixture fixture;

    fixture_init(&fixture, text);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == expected[i]);
    }

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == strlen(text));
    assert(token.span.end == strlen(text));

    fixture_destroy(&fixture);
}

static void test_symbols(void)
{
    static const char *const text =
        "( ) { } [ ] , ; : . -> => ? .. ..= ... @ "
        "+ - * / % +% -% *% "
        "== != < <= > >= "
        "&& || ! "
        "& | ^ ~ << >> "
        "= += -= *= /= %= &= |= ^= <<= >>=";

    static const CcTokenKind expected[] = {
        CC_TOKEN_LPAREN,
        CC_TOKEN_RPAREN,
        CC_TOKEN_LBRACE,
        CC_TOKEN_RBRACE,
        CC_TOKEN_LBRACKET,
        CC_TOKEN_RBRACKET,
        CC_TOKEN_COMMA,
        CC_TOKEN_SEMICOLON,
        CC_TOKEN_COLON,
        CC_TOKEN_DOT,
        CC_TOKEN_ARROW,
        CC_TOKEN_FAT_ARROW,
        CC_TOKEN_QUESTION,
        CC_TOKEN_DOT_DOT,
        CC_TOKEN_DOT_DOT_EQUAL,
        CC_TOKEN_ELLIPSIS,
        CC_TOKEN_AT,
        CC_TOKEN_PLUS,
        CC_TOKEN_MINUS,
        CC_TOKEN_STAR,
        CC_TOKEN_SLASH,
        CC_TOKEN_PERCENT,
        CC_TOKEN_PLUS_PERCENT,
        CC_TOKEN_MINUS_PERCENT,
        CC_TOKEN_STAR_PERCENT,
        CC_TOKEN_EQ,
        CC_TOKEN_NE,
        CC_TOKEN_LT,
        CC_TOKEN_LE,
        CC_TOKEN_GT,
        CC_TOKEN_GE,
        CC_TOKEN_AND_AND,
        CC_TOKEN_OR_OR,
        CC_TOKEN_NOT,
        CC_TOKEN_AMP,
        CC_TOKEN_PIPE,
        CC_TOKEN_CARET,
        CC_TOKEN_TILDE,
        CC_TOKEN_SHIFT_LEFT,
        CC_TOKEN_SHIFT_RIGHT,
        CC_TOKEN_ASSIGN,
        CC_TOKEN_PLUS_ASSIGN,
        CC_TOKEN_MINUS_ASSIGN,
        CC_TOKEN_STAR_ASSIGN,
        CC_TOKEN_SLASH_ASSIGN,
        CC_TOKEN_PERCENT_ASSIGN,
        CC_TOKEN_AMP_ASSIGN,
        CC_TOKEN_PIPE_ASSIGN,
        CC_TOKEN_CARET_ASSIGN,
        CC_TOKEN_SHIFT_LEFT_ASSIGN,
        CC_TOKEN_SHIFT_RIGHT_ASSIGN
    };

    LexerFixture fixture;

    fixture_init(&fixture, text);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == expected[i]);
    }

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == strlen(text));
    assert(token.span.end == strlen(text));

    fixture_destroy(&fixture);
}

static void test_longest_match(void)
{
    static const char *const text = "a>>=b a<<=b a..=b a...b a->b a....b";

    static const struct {
        CcTokenKind kind;
        size_t start;
        size_t end;
    } expected[] = {
        { CC_TOKEN_IDENTIFIER, 0, 1 },
        { CC_TOKEN_SHIFT_RIGHT_ASSIGN, 1, 4 },
        { CC_TOKEN_IDENTIFIER, 4, 5 },
        { CC_TOKEN_IDENTIFIER, 6, 7 },
        { CC_TOKEN_SHIFT_LEFT_ASSIGN, 7, 10 },
        { CC_TOKEN_IDENTIFIER, 10, 11 },
        { CC_TOKEN_IDENTIFIER, 12, 13 },
        { CC_TOKEN_DOT_DOT_EQUAL, 13, 16 },
        { CC_TOKEN_IDENTIFIER, 16, 17 },
        { CC_TOKEN_IDENTIFIER, 18, 19 },
        { CC_TOKEN_ELLIPSIS, 19, 22 },
        { CC_TOKEN_IDENTIFIER, 22, 23 },
        { CC_TOKEN_IDENTIFIER, 24, 25 },
        { CC_TOKEN_ARROW, 25, 27 },
        { CC_TOKEN_IDENTIFIER, 27, 28 },
        { CC_TOKEN_IDENTIFIER, 29, 30 },
        { CC_TOKEN_ELLIPSIS, 30, 33 },
        { CC_TOKEN_DOT, 33, 34 },
        { CC_TOKEN_IDENTIFIER, 34, 35 },
        { CC_TOKEN_EOF, 35, 35 }
    };

    LexerFixture fixture;

    fixture_init(&fixture, text);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == expected[i].kind);
        assert(token.span.start == expected[i].start);
        assert(token.span.end == expected[i].end);
    }

    fixture_destroy(&fixture);
}

static void test_integer_literals(void)
{
    static const char *const text =
        "0 123 1_000_000 0xff 0xFF 0b1010 0B10 0o755 0O7 "
        "0x1_f 0b1_0 0o7_5";

    static const struct {
        size_t start;
        size_t end;
    } expected[] = {
        { 0, 1 },
        { 2, 5 },
        { 6, 15 },
        { 16, 20 },
        { 21, 25 },
        { 26, 32 },
        { 33, 37 },
        { 38, 43 },
        { 44, 47 },
        { 48, 53 },
        { 54, 59 },
        { 60, 65 }
    };

    LexerFixture fixture;

    fixture_init(&fixture, text);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == CC_TOKEN_INTEGER_LITERAL);
        assert(token.span.start == expected[i].start);
        assert(token.span.end == expected[i].end);
    }

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == strlen(text));
    assert(token.span.end == strlen(text));

    fixture_destroy(&fixture);
}

static void test_range_vs_number(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "1..10");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_INTEGER_LITERAL);
    assert(token.span.start == 0 && token.span.end == 1);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_DOT_DOT);
    assert(token.span.start == 1 && token.span.end == 3);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INTEGER_LITERAL);
    assert(token.span.start == 3 && token.span.end == 5);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);

    fixture_destroy(&fixture);
}

static void test_invalid_integers(void)
{
    static const char *const invalid[] = {
        "1_",
        "1__0",
        "0x",
        "0b2",
        "0o8",
        "0xg",
        "123abc",
        "0x_1",
        "0b1_",
        "0b102"
    };

    LexerFixture fixture;

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        fixture_init(&fixture, invalid[i]);

        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == CC_TOKEN_INVALID);
        assert(token.span.start == 0);
        assert(token.span.end == strlen(invalid[i]));

        fixture_destroy(&fixture);
    }
}

static void test_float_literals(void)
{
    static const char *const text =
        "3.14 0.5 1.0 1.5e-3 1.5E+3 1e3 2E-2 "
        "1_000.5 1.5_0 1.5e1_0";

    static const struct {
        size_t start;
        size_t end;
    } expected[] = {
        { 0, 4 },
        { 5, 8 },
        { 9, 12 },
        { 13, 19 },
        { 20, 26 },
        { 27, 30 },
        { 31, 35 },
        { 36, 43 },
        { 44, 49 },
        { 50, 57 }
    };

    LexerFixture fixture;

    fixture_init(&fixture, text);

    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == CC_TOKEN_FLOAT_LITERAL);
        assert(token.span.start == expected[i].start);
        assert(token.span.end == expected[i].end);
    }

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == strlen(text));
    assert(token.span.end == strlen(text));

    fixture_destroy(&fixture);
}

static void test_float_vs_range(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "1.5..10");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_FLOAT_LITERAL);
    assert(token.span.start == 0 && token.span.end == 3);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_DOT_DOT);
    assert(token.span.start == 3 && token.span.end == 5);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INTEGER_LITERAL);
    assert(token.span.start == 5 && token.span.end == 7);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);

    fixture_destroy(&fixture);
}

static void test_invalid_floats(void)
{
    static const char *const invalid[] = {
        "1e",
        "1e+",
        "1e-",
        "1.5e",
        "1.5e+",
        "1.5e_3",
        "1.5e3_",
        "1.5abc",
        "1e3abc",
        "1.5e+3x"
    };

    LexerFixture fixture;

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        fixture_init(&fixture, invalid[i]);

        CcToken token = cc_lexer_next(&fixture.lexer);

        assert(token.kind == CC_TOKEN_INVALID);
        assert(token.span.start == 0);
        assert(token.span.end == strlen(invalid[i]));

        fixture_destroy(&fixture);
    }
}

static void test_string_literals(void)
{
    static const char *const valid[] = {
        "\"\"",
        "\"hello\"",
        "\"a b\"",
        "\"\\n\\r\\t\\0\"",
        "\"\\x41\\x7F\"",
        "\"\\u{41}\\u{20AC}\"",
        "\"\\u{10FFFF}\"",
        "\"\\u{0}\""
    };

    static const char with_quote[] = { '"', '\\', '"', '"', 0 };
    static const char with_backslash[] = { '"', '\\', '\\', '"', 0 };

    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
        expect_token(valid[i], CC_TOKEN_STRING_LITERAL);

    expect_token(with_quote, CC_TOKEN_STRING_LITERAL);
    expect_token(with_backslash, CC_TOKEN_STRING_LITERAL);
}

static void test_c_string_literals(void)
{
    static const char *const valid[] = {
        "c\"\"",
        "c\"hello\"",
        "c\"\\n\"",
        "c\"\\x41\"",
        "c\"\\u{20AC}\""
    };

    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
        expect_token(valid[i], CC_TOKEN_C_STRING_LITERAL);

    expect_token("c", CC_TOKEN_IDENTIFIER);
}

static void test_byte_literals(void)
{
    static const char *const valid[] = {
        "'A'",
        "'\\n'",
        "'\\0'",
        "'\\x41'",
        "'\\xFF'",
        "'\\u{41}'",
        "'\\u{7F}'",
        "'\"'",
        "'\\\\'"
    };

    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
        expect_token(valid[i], CC_TOKEN_BYTE_LITERAL);
}

static void test_invalid_string_literals(void)
{
    static const char *const invalid[] = {
        "\"abc",
        "\"abc\\",
        "\"\\q\"",
        "\"\\x4\"",
        "\"\\xGG\"",
        "\"\\u\"",
        "\"\\u{}\"",
        "\"\\u{110000}\"",
        "\"\\u{D800}\"",
        "\"\\u{41\""
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        expect_token(invalid[i], CC_TOKEN_INVALID);

    expect_token("c\"\\q\"", CC_TOKEN_INVALID);
    expect_token("c\"abc", CC_TOKEN_INVALID);
}

static void test_invalid_byte_literals(void)
{
    static const char *const invalid[] = {
        "''",
        "'AB'",
        "'\\u{80}'",
        "'\\q'",
        "'\\x4'",
        "'A"
    };

    static const char multibyte[] = { '\'', (char)0xC3, (char)0xA9, '\'', 0 };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        expect_token(invalid[i], CC_TOKEN_INVALID);

    expect_token(multibyte, CC_TOKEN_INVALID);
}

static void test_unterminated_literals(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "\"abc\nfn");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_INVALID);
    assert(token.span.start == 0 && token.span.end == 4);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_FN);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "'A\nreturn");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INVALID);
    assert(token.span.start == 0 && token.span.end == 2);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_RETURN);

    fixture_destroy(&fixture);
}

static void test_line_comments(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "// comment\nfn");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_KW_FN);
    assert(token.span.start == 11 && token.span.end == 13);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == 13 && token.span.end == 13);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "// comment\r\nreturn");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_RETURN);
    assert(token.span.start == 12 && token.span.end == 18);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "// comment\rfn");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_FN);
    assert(token.span.start == 11 && token.span.end == 13);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "// no newline");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == 13 && token.span.end == 13);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "a//b\nc");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);
    assert(token.span.start == 0 && token.span.end == 1);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);
    assert(token.span.start == 5 && token.span.end == 6);

    fixture_destroy(&fixture);
}

static void test_block_comments(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "/* comment */ fn");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_KW_FN);
    assert(token.span.start == 14 && token.span.end == 16);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "/* a /* b */ c */ fn");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_FN);
    assert(token.span.start == 18 && token.span.end == 20);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "/*\nline\n*/let");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_LET);
    assert(token.span.start == 10 && token.span.end == 13);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "/**/x");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);
    assert(token.span.start == 4 && token.span.end == 5);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "/* // */x");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);
    assert(token.span.start == 8 && token.span.end == 9);

    fixture_destroy(&fixture);

    fixture_init(&fixture, "1/2");

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INTEGER_LITERAL);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_SLASH);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INTEGER_LITERAL);

    fixture_destroy(&fixture);
}

static void test_unterminated_comments(void)
{
    static const char *const invalid[] = {
        "/*",
        "/*/",
        "/* unterminated",
        "/* /* */"
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
        expect_token(invalid[i], CC_TOKEN_INVALID);

    LexerFixture fixture;

    fixture_init(&fixture, "a /*");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_IDENTIFIER);
    assert(token.span.start == 0 && token.span.end == 1);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INVALID);
    assert(token.span.start == 2 && token.span.end == 4);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);
    assert(token.span.start == 4 && token.span.end == 4);

    fixture_destroy(&fixture);
}

static void test_multiple_files(void)
{
    CcSourceManager sources;
    CcSourceError error;
    CcFileId first_id;
    CcFileId second_id;

    cc_source_manager_init(&sources);

    assert(cc_source_add_memory(&sources, "first",
                                (const unsigned char *)"fn", 2,
                                &first_id, &error));
    assert(cc_source_add_memory(&sources, "second",
                                (const unsigned char *)"return", 6,
                                &second_id, &error));
    assert(first_id != second_id);

    const CcSourceFile *first = cc_source_get(&sources, first_id);
    const CcSourceFile *second = cc_source_get(&sources, second_id);

    assert(first != NULL);
    assert(second != NULL);

    CcLexer lexer;

    cc_lexer_init(&lexer, first, first_id);

    CcToken token = cc_lexer_next(&lexer);

    assert(token.kind == CC_TOKEN_KW_FN);
    assert(token.span.file_id == first_id);

    cc_lexer_init(&lexer, second, second_id);

    token = cc_lexer_next(&lexer);
    assert(token.kind == CC_TOKEN_KW_RETURN);
    assert(token.span.file_id == second_id);

    cc_source_manager_destroy(&sources);
}

static void test_token_kind_names(void)
{
    assert(strcmp(cc_token_kind_name(CC_TOKEN_INVALID), "INVALID") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_EOF), "EOF") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_IDENTIFIER),
                  "IDENTIFIER") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_INTEGER_LITERAL),
                  "INTEGER_LITERAL") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_C_STRING_LITERAL),
                  "C_STRING_LITERAL") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_KW_FN), "KW_FN") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_KW_ERRDEFER),
                  "KW_ERRDEFER") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_SHIFT_LEFT_ASSIGN),
                  "SHIFT_LEFT_ASSIGN") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_DOT_DOT_EQUAL),
                  "DOT_DOT_EQUAL") == 0);
    assert(strcmp(cc_token_kind_name(CC_TOKEN_AT), "AT") == 0);
}

static void test_lex_error_reasons(void)
{
    expect_lex_error("#", CC_LEX_ERROR_UNKNOWN_CHARACTER);
    expect_lex_error("1_", CC_LEX_ERROR_INVALID_INTEGER);
    expect_lex_error("0x", CC_LEX_ERROR_INVALID_INTEGER);
    expect_lex_error("1e", CC_LEX_ERROR_INVALID_FLOAT);
    expect_lex_error("1.5e+", CC_LEX_ERROR_INVALID_FLOAT);
    expect_lex_error("\"\\q\"", CC_LEX_ERROR_INVALID_ESCAPE);
    expect_lex_error("\"abc", CC_LEX_ERROR_UNTERMINATED_STRING);
    expect_lex_error("'\\q'", CC_LEX_ERROR_INVALID_ESCAPE);
    expect_lex_error("'A", CC_LEX_ERROR_UNTERMINATED_BYTE_LITERAL);
    expect_lex_error("'AB'", CC_LEX_ERROR_INVALID_BYTE_LITERAL);
    expect_lex_error("''", CC_LEX_ERROR_INVALID_BYTE_LITERAL);
    expect_lex_error("/*", CC_LEX_ERROR_UNTERMINATED_COMMENT);

    LexerFixture fixture;

    fixture_init(&fixture, "1_ fn");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_INVALID);
    assert(fixture.lexer.error == CC_LEX_ERROR_INVALID_INTEGER);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_KW_FN);
    assert(fixture.lexer.error == CC_LEX_ERROR_NONE);

    fixture_destroy(&fixture);

    assert(strcmp(cc_lex_error_message(CC_LEX_ERROR_NONE),
                  "no lexical error") == 0);
    assert(strcmp(cc_lex_error_message(CC_LEX_ERROR_INVALID_ESCAPE),
                  "invalid escape sequence") == 0);
    assert(strcmp(cc_lex_error_message(CC_LEX_ERROR_UNTERMINATED_COMMENT),
                  "unterminated block comment") == 0);
}

static void test_invalid_arguments(void)
{
    CcToken token = cc_lexer_next(NULL);

    assert(token.kind == CC_TOKEN_INVALID);
    assert(token.span.file_id == CC_FILE_ID_INVALID);

    CcLexer lexer;

    cc_lexer_init(&lexer, NULL, CC_FILE_ID_INVALID);

    token = cc_lexer_next(&lexer);
    assert(token.kind == CC_TOKEN_INVALID);
    assert(token.span.file_id == CC_FILE_ID_INVALID);

    cc_lexer_init(NULL, NULL, CC_FILE_ID_INVALID);
}

static void test_invalid_character(void)
{
    LexerFixture fixture;

    fixture_init(&fixture, "a # b");

    CcToken token = cc_lexer_next(&fixture.lexer);

    assert(token.kind == CC_TOKEN_IDENTIFIER);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_INVALID);
    assert(token.span.start == 2 && token.span.end == 3);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_IDENTIFIER);

    token = cc_lexer_next(&fixture.lexer);
    assert(token.kind == CC_TOKEN_EOF);

    fixture_destroy(&fixture);
}

int main(void)
{
    test_empty_and_whitespace();
    test_line_comments();
    test_block_comments();
    test_unterminated_comments();
    test_identifiers_and_keywords();
    test_all_keywords();
    test_symbols();
    test_longest_match();
    test_integer_literals();
    test_range_vs_number();
    test_invalid_integers();
    test_float_literals();
    test_float_vs_range();
    test_invalid_floats();
    test_string_literals();
    test_c_string_literals();
    test_byte_literals();
    test_invalid_string_literals();
    test_invalid_byte_literals();
    test_unterminated_literals();
    test_multiple_files();
    test_token_kind_names();
    test_lex_error_reasons();
    test_invalid_arguments();
    test_invalid_character();

    puts("lexer: all tests passed");
    return 0;
}
