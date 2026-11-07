#include "lexer/lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const char *text;
    CcTokenKind kind;
} CcKeyword;

typedef struct {
    bool valid;
    size_t byte_count;
} CcEscape;

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

static bool is_decimal_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}

static bool is_identifier_continue(unsigned char c)
{
    return is_identifier_start(c) || is_decimal_digit(c);
}

static bool is_base_digit(unsigned char c, unsigned int base)
{
    if (is_decimal_digit(c))
        return (unsigned int)(c - '0') < base;

    if (base == 16)
        return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');

    return false;
}

static bool is_hex_digit(unsigned char c)
{
    return is_base_digit(c, 16);
}

static unsigned int hex_value(unsigned char c)
{
    if (is_decimal_digit(c)) return (unsigned int)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned int)(c - 'a') + 10;

    return (unsigned int)(c - 'A') + 10;
}

static size_t utf8_length(unsigned int codepoint)
{
    if (codepoint <= 0x7FU) return 1;
    if (codepoint <= 0x7FFU) return 2;
    if (codepoint <= 0xFFFFU) return 3;

    return 4;
}

static bool is_whitespace(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\v' || c == '\f';
}

static void skip_line_comment(const CcSourceFile *file, size_t *offset)
{
    while (*offset < file->length) {
        unsigned char c = file->bytes[*offset];

        if (c == '\n' || c == '\r') break;

        (*offset)++;
    }
}

static bool skip_block_comment(const CcSourceFile *file, size_t *offset)
{
    size_t i = *offset + 2;
    size_t depth = 1;

    while (i < file->length) {
        if (i + 1 < file->length &&
            file->bytes[i] == '/' && file->bytes[i + 1] == '*') {
            depth++;
            i += 2;
            continue;
        }

        if (i + 1 < file->length &&
            file->bytes[i] == '*' && file->bytes[i + 1] == '/') {
            depth--;
            i += 2;

            if (depth == 0) {
                *offset = i;
                return true;
            }

            continue;
        }

        i++;
    }

    *offset = i;
    return false;
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

static bool scan_digit_sequence(const CcSourceFile *file, size_t *offset,
                                unsigned int base, bool *out_saw_digit)
{
    bool valid = true;
    bool saw_digit = false;
    bool last_was_underscore = false;

    while (*offset < file->length) {
        unsigned char c = file->bytes[*offset];

        if (is_base_digit(c, base)) {
            saw_digit = true;
            last_was_underscore = false;
            (*offset)++;
            continue;
        }

        if (c == '_') {
            if (!saw_digit || last_was_underscore ||
                *offset + 1 >= file->length ||
                !is_base_digit(file->bytes[*offset + 1], base))
                valid = false;

            last_was_underscore = true;
            (*offset)++;
            continue;
        }

        break;
    }

    *out_saw_digit = saw_digit;
    return valid;
}

static CcToken scan_number(CcLexer *lexer)
{
    const CcSourceFile *file = lexer->file;
    size_t start = lexer->offset;
    size_t offset = start;
    unsigned int base = 10;
    size_t prefix_length = 0;

    if (file->bytes[offset] == '0' && offset + 1 < file->length) {
        unsigned char marker = file->bytes[offset + 1];

        if (marker == 'x' || marker == 'X') {
            base = 16;
            prefix_length = 2;
        } else if (marker == 'b' || marker == 'B') {
            base = 2;
            prefix_length = 2;
        } else if (marker == 'o' || marker == 'O') {
            base = 8;
            prefix_length = 2;
        }
    }

    offset += prefix_length;

    bool valid = true;
    bool saw_digit = false;
    bool is_float = false;

    if (!scan_digit_sequence(file, &offset, base, &saw_digit))
        valid = false;

    if (!saw_digit) valid = false;

    if (base == 10) {
        if (offset + 1 < file->length &&
            file->bytes[offset] == '.' &&
            is_decimal_digit(file->bytes[offset + 1])) {
            is_float = true;
            offset++;

            bool fractional_digit = false;

            if (!scan_digit_sequence(file, &offset, 10, &fractional_digit))
                valid = false;

            if (!fractional_digit) valid = false;
        }

        if (offset < file->length &&
            (file->bytes[offset] == 'e' || file->bytes[offset] == 'E')) {
            is_float = true;
            offset++;

            if (offset < file->length &&
                (file->bytes[offset] == '+' || file->bytes[offset] == '-'))
                offset++;

            bool exponent_digit = false;

            if (!scan_digit_sequence(file, &offset, 10, &exponent_digit))
                valid = false;

            if (!exponent_digit) valid = false;
        }
    }

    while (offset < file->length &&
           is_identifier_continue(file->bytes[offset])) {
        valid = false;
        offset++;
    }

    lexer->offset = offset;

    CcTokenKind kind = CC_TOKEN_INTEGER_LITERAL;

    if (!valid) {
        kind = CC_TOKEN_INVALID;
        lexer->error = is_float
            ? CC_LEX_ERROR_INVALID_FLOAT
            : CC_LEX_ERROR_INVALID_INTEGER;
    } else if (is_float) {
        kind = CC_TOKEN_FLOAT_LITERAL;
    }

    return make_token(
        kind,
        lexer->file_id,
        (uint32_t)start,
        (uint32_t)offset
    );
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

static CcEscape scan_escape(const CcSourceFile *file, size_t *offset)
{
    CcEscape escape = { false, 0 };
    size_t i = *offset + 1;

    if (i >= file->length || file->bytes[i] == '\n' || file->bytes[i] == '\r') {
        *offset = i;
        return escape;
    }

    unsigned char c = file->bytes[i];

    switch (c) {
    case 'n':
    case 'r':
    case 't':
    case '\\':
    case '"':
    case '0':
        escape.valid = true;
        escape.byte_count = 1;
        i++;
        break;
    case 'x': {
        i++;

        size_t digit_count = 0;

        while (i < file->length && digit_count < 2 &&
               is_hex_digit(file->bytes[i])) {
            digit_count++;
            i++;
        }

        if (digit_count == 2) {
            escape.valid = true;
            escape.byte_count = 1;
        }
        break;
    }
    case 'u': {
        i++;
        if (i >= file->length || file->bytes[i] != '{') break;
        i++;

        unsigned int value = 0;
        size_t digit_count = 0;
        bool too_many_digits = false;

        while (i < file->length && is_hex_digit(file->bytes[i])) {
            if (digit_count < 6)
                value = value * 16U + hex_value(file->bytes[i]);
            else
                too_many_digits = true;
            digit_count++;
            i++;
        }

        if (i >= file->length || file->bytes[i] != '}') break;
        i++;

        if (digit_count == 0 || too_many_digits) break;
        if (value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) break;

        escape.valid = true;
        escape.byte_count = utf8_length(value);
        break;
    }
    default:
        i++;
        break;
    }

    *offset = i;
    return escape;
}

static CcToken scan_string(CcLexer *lexer, size_t prefix_length)
{
    const CcSourceFile *file = lexer->file;
    size_t start = lexer->offset;
    size_t offset = start + prefix_length + 1;
    CcLexErrorKind error = CC_LEX_ERROR_NONE;
    bool terminated = false;

    while (offset < file->length) {
        unsigned char c = file->bytes[offset];

        if (c == '"') {
            offset++;
            terminated = true;
            break;
        }

        if (c == '\n' || c == '\r')
            break;

        if (c == '\\') {
            CcEscape escape = scan_escape(file, &offset);

            if (!escape.valid && error == CC_LEX_ERROR_NONE)
                error = CC_LEX_ERROR_INVALID_ESCAPE;
            continue;
        }

        offset++;
    }

    if (!terminated && error == CC_LEX_ERROR_NONE)
        error = CC_LEX_ERROR_UNTERMINATED_STRING;

    lexer->offset = offset;

    CcTokenKind kind = prefix_length == 0
        ? CC_TOKEN_STRING_LITERAL
        : CC_TOKEN_C_STRING_LITERAL;

    if (error != CC_LEX_ERROR_NONE) {
        kind = CC_TOKEN_INVALID;
        lexer->error = error;
    }

    return make_token(
        kind,
        lexer->file_id,
        (uint32_t)start,
        (uint32_t)offset
    );
}

static CcToken scan_byte_literal(CcLexer *lexer)
{
    const CcSourceFile *file = lexer->file;
    size_t start = lexer->offset;
    size_t offset = start + 1;
    size_t byte_count = 0;
    CcLexErrorKind error = CC_LEX_ERROR_NONE;
    bool terminated = false;

    while (offset < file->length) {
        unsigned char c = file->bytes[offset];

        if (c == '\'') {
            offset++;
            terminated = true;
            break;
        }

        if (c == '\n' || c == '\r')
            break;

        if (c == '\\') {
            CcEscape escape = scan_escape(file, &offset);

            if (!escape.valid) {
                if (error == CC_LEX_ERROR_NONE)
                    error = CC_LEX_ERROR_INVALID_ESCAPE;
            } else {
                byte_count += escape.byte_count;
            }
            continue;
        }

        byte_count++;
        offset++;
    }

    if (!terminated) {
        if (error == CC_LEX_ERROR_NONE)
            error = CC_LEX_ERROR_UNTERMINATED_BYTE_LITERAL;
    } else if (byte_count != 1) {
        if (error == CC_LEX_ERROR_NONE)
            error = CC_LEX_ERROR_INVALID_BYTE_LITERAL;
    }

    lexer->offset = offset;

    CcTokenKind kind = CC_TOKEN_BYTE_LITERAL;

    if (error != CC_LEX_ERROR_NONE) {
        kind = CC_TOKEN_INVALID;
        lexer->error = error;
    }

    return make_token(
        kind,
        lexer->file_id,
        (uint32_t)start,
        (uint32_t)offset
    );
}

void cc_lexer_init(CcLexer *lexer, const CcSourceFile *file, CcFileId file_id)
{
    if (lexer == NULL) return;

    lexer->file = file;
    lexer->file_id = file_id;
    lexer->offset = 0;
    lexer->error = CC_LEX_ERROR_NONE;
}

CcToken cc_lexer_next(CcLexer *lexer)
{
    if (lexer == NULL || lexer->file == NULL)
        return make_token(CC_TOKEN_INVALID, CC_FILE_ID_INVALID, 0, 0);

    lexer->error = CC_LEX_ERROR_NONE;

    const CcSourceFile *file = lexer->file;

    for (;;) {
        while (lexer->offset < file->length &&
               is_whitespace(file->bytes[lexer->offset]))
            lexer->offset++;

        if (lexer->offset + 1 >= file->length) break;

        unsigned char c = file->bytes[lexer->offset];

        if (c == '/' && file->bytes[lexer->offset + 1] == '/') {
            skip_line_comment(file, &lexer->offset);
            continue;
        }

        if (c == '/' && file->bytes[lexer->offset + 1] == '*') {
            size_t comment_start = lexer->offset;

            if (!skip_block_comment(file, &lexer->offset)) {
                lexer->error = CC_LEX_ERROR_UNTERMINATED_COMMENT;
                return make_token(
                    CC_TOKEN_INVALID,
                    lexer->file_id,
                    (uint32_t)comment_start,
                    (uint32_t)lexer->offset
                );
            }

            continue;
        }

        break;
    }

    uint32_t start = (uint32_t)lexer->offset;

    if (lexer->offset == file->length)
        return make_token(CC_TOKEN_EOF, lexer->file_id, start, start);

    unsigned char first = file->bytes[lexer->offset];

    if (first == 'c' && lexer->offset + 1 < file->length &&
        file->bytes[lexer->offset + 1] == '"')
        return scan_string(lexer, 1);

    if (first == '"')
        return scan_string(lexer, 0);

    if (first == '\'')
        return scan_byte_literal(lexer);

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

    if (is_decimal_digit(first))
        return scan_number(lexer);

    size_t width = 0;
    CcTokenKind kind = scan_symbol(file, lexer->offset, &width);

    if (kind == CC_TOKEN_INVALID)
        lexer->error = CC_LEX_ERROR_UNKNOWN_CHARACTER;

    lexer->offset += width;

    return make_token(
        kind,
        lexer->file_id,
        start,
        (uint32_t)lexer->offset
    );
}

const char *cc_token_kind_name(CcTokenKind kind)
{
    switch (kind) {
    case CC_TOKEN_INVALID: return "INVALID";
    case CC_TOKEN_EOF: return "EOF";
    case CC_TOKEN_IDENTIFIER: return "IDENTIFIER";
    case CC_TOKEN_INTEGER_LITERAL: return "INTEGER_LITERAL";
    case CC_TOKEN_FLOAT_LITERAL: return "FLOAT_LITERAL";
    case CC_TOKEN_STRING_LITERAL: return "STRING_LITERAL";
    case CC_TOKEN_C_STRING_LITERAL: return "C_STRING_LITERAL";
    case CC_TOKEN_BYTE_LITERAL: return "BYTE_LITERAL";
    case CC_TOKEN_KW_FN: return "KW_FN";
    case CC_TOKEN_KW_RETURN: return "KW_RETURN";
    case CC_TOKEN_KW_LET: return "KW_LET";
    case CC_TOKEN_KW_VAR: return "KW_VAR";
    case CC_TOKEN_KW_CONST: return "KW_CONST";
    case CC_TOKEN_KW_STATIC: return "KW_STATIC";
    case CC_TOKEN_KW_PUB: return "KW_PUB";
    case CC_TOKEN_KW_TYPE: return "KW_TYPE";
    case CC_TOKEN_KW_STRUCT: return "KW_STRUCT";
    case CC_TOKEN_KW_UNION: return "KW_UNION";
    case CC_TOKEN_KW_ENUM: return "KW_ENUM";
    case CC_TOKEN_KW_IF: return "KW_IF";
    case CC_TOKEN_KW_ELSE: return "KW_ELSE";
    case CC_TOKEN_KW_MATCH: return "KW_MATCH";
    case CC_TOKEN_KW_WHILE: return "KW_WHILE";
    case CC_TOKEN_KW_FOR: return "KW_FOR";
    case CC_TOKEN_KW_IN: return "KW_IN";
    case CC_TOKEN_KW_LOOP: return "KW_LOOP";
    case CC_TOKEN_KW_BREAK: return "KW_BREAK";
    case CC_TOKEN_KW_CONTINUE: return "KW_CONTINUE";
    case CC_TOKEN_KW_MODULE: return "KW_MODULE";
    case CC_TOKEN_KW_IMPORT: return "KW_IMPORT";
    case CC_TOKEN_KW_AS: return "KW_AS";
    case CC_TOKEN_KW_EXTERN: return "KW_EXTERN";
    case CC_TOKEN_KW_UNSAFE: return "KW_UNSAFE";
    case CC_TOKEN_KW_VOLATILE: return "KW_VOLATILE";
    case CC_TOKEN_KW_DEFER: return "KW_DEFER";
    case CC_TOKEN_KW_ERRDEFER: return "KW_ERRDEFER";
    case CC_TOKEN_KW_TRY: return "KW_TRY";
    case CC_TOKEN_KW_TRUE: return "KW_TRUE";
    case CC_TOKEN_KW_FALSE: return "KW_FALSE";
    case CC_TOKEN_KW_SOME: return "KW_SOME";
    case CC_TOKEN_KW_NONE: return "KW_NONE";
    case CC_TOKEN_PLUS: return "PLUS";
    case CC_TOKEN_MINUS: return "MINUS";
    case CC_TOKEN_STAR: return "STAR";
    case CC_TOKEN_SLASH: return "SLASH";
    case CC_TOKEN_PERCENT: return "PERCENT";
    case CC_TOKEN_PLUS_PERCENT: return "PLUS_PERCENT";
    case CC_TOKEN_MINUS_PERCENT: return "MINUS_PERCENT";
    case CC_TOKEN_STAR_PERCENT: return "STAR_PERCENT";
    case CC_TOKEN_EQ: return "EQ";
    case CC_TOKEN_NE: return "NE";
    case CC_TOKEN_LT: return "LT";
    case CC_TOKEN_LE: return "LE";
    case CC_TOKEN_GT: return "GT";
    case CC_TOKEN_GE: return "GE";
    case CC_TOKEN_AND_AND: return "AND_AND";
    case CC_TOKEN_OR_OR: return "OR_OR";
    case CC_TOKEN_NOT: return "NOT";
    case CC_TOKEN_AMP: return "AMP";
    case CC_TOKEN_PIPE: return "PIPE";
    case CC_TOKEN_CARET: return "CARET";
    case CC_TOKEN_TILDE: return "TILDE";
    case CC_TOKEN_SHIFT_LEFT: return "SHIFT_LEFT";
    case CC_TOKEN_SHIFT_RIGHT: return "SHIFT_RIGHT";
    case CC_TOKEN_ASSIGN: return "ASSIGN";
    case CC_TOKEN_PLUS_ASSIGN: return "PLUS_ASSIGN";
    case CC_TOKEN_MINUS_ASSIGN: return "MINUS_ASSIGN";
    case CC_TOKEN_STAR_ASSIGN: return "STAR_ASSIGN";
    case CC_TOKEN_SLASH_ASSIGN: return "SLASH_ASSIGN";
    case CC_TOKEN_PERCENT_ASSIGN: return "PERCENT_ASSIGN";
    case CC_TOKEN_AMP_ASSIGN: return "AMP_ASSIGN";
    case CC_TOKEN_PIPE_ASSIGN: return "PIPE_ASSIGN";
    case CC_TOKEN_CARET_ASSIGN: return "CARET_ASSIGN";
    case CC_TOKEN_SHIFT_LEFT_ASSIGN: return "SHIFT_LEFT_ASSIGN";
    case CC_TOKEN_SHIFT_RIGHT_ASSIGN: return "SHIFT_RIGHT_ASSIGN";
    case CC_TOKEN_LPAREN: return "LPAREN";
    case CC_TOKEN_RPAREN: return "RPAREN";
    case CC_TOKEN_LBRACE: return "LBRACE";
    case CC_TOKEN_RBRACE: return "RBRACE";
    case CC_TOKEN_LBRACKET: return "LBRACKET";
    case CC_TOKEN_RBRACKET: return "RBRACKET";
    case CC_TOKEN_COMMA: return "COMMA";
    case CC_TOKEN_SEMICOLON: return "SEMICOLON";
    case CC_TOKEN_COLON: return "COLON";
    case CC_TOKEN_DOT: return "DOT";
    case CC_TOKEN_ARROW: return "ARROW";
    case CC_TOKEN_FAT_ARROW: return "FAT_ARROW";
    case CC_TOKEN_QUESTION: return "QUESTION";
    case CC_TOKEN_DOT_DOT: return "DOT_DOT";
    case CC_TOKEN_DOT_DOT_EQUAL: return "DOT_DOT_EQUAL";
    case CC_TOKEN_ELLIPSIS: return "ELLIPSIS";
    case CC_TOKEN_AT: return "AT";
    }

    return "UNKNOWN";
}

const char *cc_lex_error_message(CcLexErrorKind error)
{
    switch (error) {
    case CC_LEX_ERROR_NONE: return "no lexical error";
    case CC_LEX_ERROR_UNKNOWN_CHARACTER: return "unknown character";
    case CC_LEX_ERROR_INVALID_INTEGER: return "malformed integer literal";
    case CC_LEX_ERROR_INVALID_FLOAT: return "malformed floating literal";
    case CC_LEX_ERROR_INVALID_ESCAPE: return "invalid escape sequence";
    case CC_LEX_ERROR_UNTERMINATED_STRING:
        return "unterminated string literal";
    case CC_LEX_ERROR_UNTERMINATED_BYTE_LITERAL:
        return "unterminated byte literal";
    case CC_LEX_ERROR_INVALID_BYTE_LITERAL:
        return "byte literal must encode exactly one byte";
    case CC_LEX_ERROR_UNTERMINATED_COMMENT:
        return "unterminated block comment";
    }

    return "unknown lexical error";
}
