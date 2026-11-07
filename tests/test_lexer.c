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
    test_identifiers_and_keywords();
    test_all_keywords();
    test_invalid_arguments();
    test_invalid_character();

    puts("lexer identifiers: all tests passed");
    return 0;
}
