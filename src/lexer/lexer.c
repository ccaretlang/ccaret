#include "lexer/lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char *text;
    CcTokenKind kind;
} CcKeyword;

static const CcKeyword keywords[] = {
    { "fn", CC_TOKEN_KW_FN },
    { "return", CC_TOKEN_KW_RETURN },
    { "let", CC_TOKEN_KW_LET },
    { "var", CC_TOKEN_KW_VAR },
    { "const", CC_TOKEN_KW_CONST },
    { "static", CC_TOKEN_KW_STATIC },
    { "pub", CC_TOKEN_KW_PUB },
    { "type", CC_TOKEN_KW_TYPE },
    { "struct", CC_TOKEN_KW_STRUCT },
    { "union", CC_TOKEN_KW_UNION },
    { "enum", CC_TOKEN_KW_ENUM },
    { "if", CC_TOKEN_KW_IF },
    { "else", CC_TOKEN_KW_ELSE },
    { "match", CC_TOKEN_KW_MATCH },
    { "while", CC_TOKEN_KW_WHILE },
    { "for", CC_TOKEN_KW_FOR },
    { "in", CC_TOKEN_KW_IN },
    { "loop", CC_TOKEN_KW_LOOP },
    { "break", CC_TOKEN_KW_BREAK },
    { "continue", CC_TOKEN_KW_CONTINUE },
    { "module", CC_TOKEN_KW_MODULE },
    { "import", CC_TOKEN_KW_IMPORT },
    { "as", CC_TOKEN_KW_AS },
    { "extern", CC_TOKEN_KW_EXTERN },
    { "unsafe", CC_TOKEN_KW_UNSAFE },
    { "volatile", CC_TOKEN_KW_VOLATILE },
    { "defer", CC_TOKEN_KW_DEFER },
    { "errdefer", CC_TOKEN_KW_ERRDEFER },
    { "try", CC_TOKEN_KW_TRY },
    { "true", CC_TOKEN_KW_TRUE },
    { "false", CC_TOKEN_KW_FALSE },
    { "some", CC_TOKEN_KW_SOME },
    { "none", CC_TOKEN_KW_NONE },
};

static CcToken make_token(CcTokenKind kind, CcFileId file_id,
                          uint32_t start, uint32_t end)
{
    CcToken token = {
        kind,
        { file_id, start, end }
    };

    return token;
}

static bool is_identifier_start(unsigned char c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool is_identifier_continue(unsigned char c)
{
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

static bool is_whitespace(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\v' || c == '\f';
}

static CcTokenKind keyword_kind(const unsigned char *text, size_t length)
{
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strlen(keywords[i].text) == length &&
            memcmp(keywords[i].text, text, length) == 0)
            return keywords[i].kind;
    }

    return CC_TOKEN_IDENTIFIER;
}

void cc_lexer_init(CcLexer *lexer, const CcSourceFile *file, CcFileId file_id)
{
    if (lexer == NULL) return;

    lexer->file = file;
    lexer->file_id = file_id;
    lexer->offset = 0;
}

CcToken cc_lexer_next(CcLexer *lexer)
{
    if (lexer == NULL || lexer->file == NULL)
        return make_token(CC_TOKEN_INVALID, CC_FILE_ID_INVALID, 0, 0);

    const CcSourceFile *file = lexer->file;

    while (lexer->offset < file->length &&
           is_whitespace(file->bytes[lexer->offset]))
        lexer->offset++;

    uint32_t start = (uint32_t)lexer->offset;

    if (lexer->offset == file->length)
        return make_token(CC_TOKEN_EOF, lexer->file_id, start, start);

    unsigned char first = file->bytes[lexer->offset];

    if (is_identifier_start(first)) {
        lexer->offset++;

        while (lexer->offset < file->length &&
               is_identifier_continue(file->bytes[lexer->offset]))
            lexer->offset++;

        uint32_t end = (uint32_t)lexer->offset;

        return make_token(
            keyword_kind(file->bytes + start, end - start),
            lexer->file_id,
            start,
            end
        );
    }

    lexer->offset++;

    return make_token(
        CC_TOKEN_INVALID,
        lexer->file_id,
        start,
        (uint32_t)lexer->offset
    );
}
