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

static int peek(const CcSourceFile *file, size_t offset)
{
    if (offset >= file->length) return -1;

    return file->bytes[offset];
}

static CcTokenKind scan_symbol(const CcSourceFile *file, size_t start,
                               size_t *out_width)
{
    unsigned char first = file->bytes[start];
    int second = peek(file, start + 1);
    int third = peek(file, start + 2);

    *out_width = 1;

    switch (first) {
    case '+':
        if (second == '%') {
            *out_width = 2;
            return CC_TOKEN_PLUS_PERCENT;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_PLUS_ASSIGN;
        }
        return CC_TOKEN_PLUS;
    case '-':
        if (second == '%') {
            *out_width = 2;
            return CC_TOKEN_MINUS_PERCENT;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_MINUS_ASSIGN;
        }
        if (second == '>') {
            *out_width = 2;
            return CC_TOKEN_ARROW;
        }
        return CC_TOKEN_MINUS;
    case '*':
        if (second == '%') {
            *out_width = 2;
            return CC_TOKEN_STAR_PERCENT;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_STAR_ASSIGN;
        }
        return CC_TOKEN_STAR;
    case '/':
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_SLASH_ASSIGN;
        }
        return CC_TOKEN_SLASH;
    case '%':
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_PERCENT_ASSIGN;
        }
        return CC_TOKEN_PERCENT;
    case '=':
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_EQ;
        }
        if (second == '>') {
            *out_width = 2;
            return CC_TOKEN_FAT_ARROW;
        }
        return CC_TOKEN_ASSIGN;
    case '!':
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_NE;
        }
        return CC_TOKEN_NOT;
    case '<':
        if (second == '<') {
            if (third == '=') {
                *out_width = 3;
                return CC_TOKEN_SHIFT_LEFT_ASSIGN;
            }
            *out_width = 2;
            return CC_TOKEN_SHIFT_LEFT;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_LE;
        }
        return CC_TOKEN_LT;
    case '>':
        if (second == '>') {
            if (third == '=') {
                *out_width = 3;
                return CC_TOKEN_SHIFT_RIGHT_ASSIGN;
            }
            *out_width = 2;
            return CC_TOKEN_SHIFT_RIGHT;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_GE;
        }
        return CC_TOKEN_GT;
    case '&':
        if (second == '&') {
            *out_width = 2;
            return CC_TOKEN_AND_AND;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_AMP_ASSIGN;
        }
        return CC_TOKEN_AMP;
    case '|':
        if (second == '|') {
            *out_width = 2;
            return CC_TOKEN_OR_OR;
        }
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_PIPE_ASSIGN;
        }
        return CC_TOKEN_PIPE;
    case '^':
        if (second == '=') {
            *out_width = 2;
            return CC_TOKEN_CARET_ASSIGN;
        }
        return CC_TOKEN_CARET;
    case '~':
        return CC_TOKEN_TILDE;
    case '(':
        return CC_TOKEN_LPAREN;
    case ')':
        return CC_TOKEN_RPAREN;
    case '{':
        return CC_TOKEN_LBRACE;
    case '}':
        return CC_TOKEN_RBRACE;
    case '[':
        return CC_TOKEN_LBRACKET;
    case ']':
        return CC_TOKEN_RBRACKET;
    case ',':
        return CC_TOKEN_COMMA;
    case ';':
        return CC_TOKEN_SEMICOLON;
    case ':':
        return CC_TOKEN_COLON;
    case '.':
        if (second == '.') {
            if (third == '.') {
                *out_width = 3;
                return CC_TOKEN_ELLIPSIS;
            }
            if (third == '=') {
                *out_width = 3;
                return CC_TOKEN_DOT_DOT_EQUAL;
            }
            *out_width = 2;
            return CC_TOKEN_DOT_DOT;
        }
        return CC_TOKEN_DOT;
    case '?':
        return CC_TOKEN_QUESTION;
    case '@':
        return CC_TOKEN_AT;
    default:
        return CC_TOKEN_INVALID;
    }
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

    size_t width = 0;
    CcTokenKind kind = scan_symbol(file, lexer->offset, &width);

    lexer->offset += width;

    return make_token(
        kind,
        lexer->file_id,
        start,
        (uint32_t)lexer->offset
    );
}
