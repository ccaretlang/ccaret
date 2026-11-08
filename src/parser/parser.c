#include "parser/parser.h"

#include <limits.h>
#include <stddef.h>

static void report_error(CcParser *parser, const char *message)
{
    if (parser->failed || parser->error != NULL) return;

    parser->failed = true;
    parser->error = message;
    parser->error_span = parser->current.span;
}

static void report_oom(CcParser *parser)
{
    report_error(parser, "out of memory");
}

static void advance(CcParser *parser)
{
    parser->current = cc_lexer_next(parser->lexer);
}

static bool accept(CcParser *parser, CcTokenKind kind, CcToken *out_token)
{
    if (parser->failed) return false;

    if (parser->current.kind != kind) return false;

    if (out_token != NULL) *out_token = parser->current;

    advance(parser);
    return true;
}

static bool expect(CcParser *parser, CcTokenKind kind, const char *message,
                   CcToken *out_token)
{
    if (accept(parser, kind, out_token)) return true;

    if (!parser->failed) report_error(parser, message);

    return false;
}

static void *allocate(CcParser *parser, size_t size)
{
    void *memory = cc_arena_alloc(parser->arena, size);

    if (memory == NULL) report_oom(parser);

    return memory;
}

static bool integer_value(const CcParser *parser, CcToken token,
                          unsigned long long *out_value, bool *out_overflow)
{
    const CcSourceFile *file = parser->lexer->file;
    const unsigned char *text = file->bytes + token.span.start;
    size_t length = token.span.end - token.span.start;
    unsigned int base = 10;
    size_t index = 0;

    if (length >= 2 && text[0] == '0') {
        switch (text[1]) {
        case 'x': case 'X': base = 16; index = 2; break;
        case 'b': case 'B': base = 2; index = 2; break;
        case 'o': case 'O': base = 8; index = 2; break;
        default: break;
        }
    }

    unsigned long long value = 0;
    bool saw_digit = false;
    bool overflow = false;

    for (; index < length; index++) {
        unsigned char c = text[index];

        if (c == '_') continue;

        unsigned int digit;

        if (c >= '0' && c <= '9')
            digit = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (unsigned int)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F')
            digit = (unsigned int)(c - 'A') + 10;
        else
            return false;

        if (digit >= base) return false;

        saw_digit = true;

        if (!overflow) {
            if (value > (ULLONG_MAX - digit) / base)
                overflow = true;
            else
                value = value * base + digit;
        }
    }

    if (!saw_digit) return false;

    *out_value = value;
    *out_overflow = overflow;
    return true;
}

static CcExpression *parse_expression(CcParser *parser)
{
    CcToken token = parser->current;

    if (token.kind == CC_TOKEN_INTEGER_LITERAL) {
        unsigned long long value = 0;
        bool overflow = false;

        if (!integer_value(parser, token, &value, &overflow)) {
            report_error(parser, "malformed integer literal");
            return NULL;
        }

        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_INTEGER_LITERAL;
        expression->span = token.span;
        expression->integer_literal.value = value;
        expression->integer_literal.overflow = overflow;

        advance(parser);
        return expression;
    }

    if (token.kind == CC_TOKEN_IDENTIFIER) {
        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_IDENTIFIER;
        expression->span = token.span;
        expression->identifier_name = token.span;

        advance(parser);
        return expression;
    }

    report_error(parser, "expected expression");
    return NULL;
}

static CcType *parse_type(CcParser *parser)
{
    CcToken name;

    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected a type", &name))
        return NULL;

    CcType *type = allocate(parser, sizeof(*type));

    if (type == NULL) return NULL;

    type->kind = CC_AST_TYPE_NAMED;
    type->span = name.span;

    return type;
}

static CcParameter *parse_parameter(CcParser *parser)
{
    CcToken name;

    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected parameter name", &name))
        return NULL;

    if (!expect(parser, CC_TOKEN_COLON, "expected `:` after parameter name",
                NULL))
        return NULL;

    CcType *type = parse_type(parser);

    if (parser->failed || type == NULL) return NULL;

    CcParameter *parameter = allocate(parser, sizeof(*parameter));

    if (parameter == NULL) return NULL;

    parameter->span.file_id = name.span.file_id;
    parameter->span.start = name.span.start;
    parameter->span.end = type->span.end;
    parameter->name = name.span;
    parameter->type = type;
    parameter->next = NULL;

    return parameter;
}

static CcStatement *parse_return_statement(CcParser *parser)
{
    CcToken keyword = parser->current;

    advance(parser);

    CcExpression *expression = NULL;
    CcToken end_token = keyword;

    if (!accept(parser, CC_TOKEN_SEMICOLON, &end_token)) {
        expression = parse_expression(parser);

        if (parser->failed || expression == NULL) return NULL;

        if (!expect(parser, CC_TOKEN_SEMICOLON, "expected `;`", &end_token))
            return NULL;
    }

    CcStatement *statement = allocate(parser, sizeof(*statement));

    if (statement == NULL) return NULL;

    statement->kind = CC_AST_STATEMENT_RETURN;
    statement->span.file_id = keyword.span.file_id;
    statement->span.start = keyword.span.start;
    statement->span.end = end_token.span.end;
    statement->next = NULL;
    statement->return_expression = expression;

    return statement;
}

static CcBlock *parse_block(CcParser *parser)
{
    CcToken lbrace;

    if (!expect(parser, CC_TOKEN_LBRACE, "expected `{`", &lbrace)) return NULL;

    CcStatement *first = NULL;
    CcStatement **link = &first;
    CcToken rbrace = lbrace;

    while (!parser->failed) {
        if (parser->current.kind == CC_TOKEN_RBRACE) {
            rbrace = parser->current;
            advance(parser);
            break;
        }

        if (parser->current.kind == CC_TOKEN_EOF ||
            parser->current.kind == CC_TOKEN_INVALID) {
            report_error(parser, "expected `}`");
            break;
        }

        if (parser->current.kind == CC_TOKEN_KW_RETURN) {
            CcStatement *statement = parse_return_statement(parser);

            if (parser->failed || statement == NULL) break;

            *link = statement;
            link = &statement->next;
            continue;
        }

        report_error(parser, "expected statement");
        break;
    }

    if (parser->failed) return NULL;

    CcBlock *block = allocate(parser, sizeof(*block));

    if (block == NULL) return NULL;

    block->span.file_id = lbrace.span.file_id;
    block->span.start = lbrace.span.start;
    block->span.end = rbrace.span.end;
    block->first = first;

    return block;
}

static CcFunctionDeclaration *parse_function(CcParser *parser)
{
    uint32_t start = parser->current.span.start;
    CcToken keyword;

    if (!expect(parser, CC_TOKEN_KW_FN, "expected function declaration",
                &keyword))
        return NULL;

    CcToken name;

    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected function name", &name))
        return NULL;

    if (!expect(parser, CC_TOKEN_LPAREN, "expected `(`", NULL)) return NULL;

    CcParameter *parameters = NULL;
    CcParameter **param_link = &parameters;

    if (parser->current.kind != CC_TOKEN_RPAREN) {
        for (;;) {
            CcParameter *parameter = parse_parameter(parser);

            if (parser->failed || parameter == NULL) return NULL;

            *param_link = parameter;
            param_link = &parameter->next;

            if (parser->current.kind != CC_TOKEN_COMMA) break;

            advance(parser);
        }
    }

    if (!expect(parser, CC_TOKEN_RPAREN, "expected `)`", NULL)) return NULL;

    CcType *return_type = NULL;

    if (parser->current.kind == CC_TOKEN_ARROW) {
        advance(parser);

        return_type = parse_type(parser);

        if (parser->failed || return_type == NULL) return NULL;
    }

    CcBlock *body = parse_block(parser);

    if (parser->failed || body == NULL) return NULL;

    CcFunctionDeclaration *function = allocate(parser, sizeof(*function));

    if (function == NULL) return NULL;

    function->span.file_id = keyword.span.file_id;
    function->span.start = start;
    function->span.end = body->span.end;
    function->name = name.span;
    function->parameters = parameters;
    function->return_type = return_type;
    function->body = body;
    function->next = NULL;

    return function;
}

void cc_parser_init(CcParser *parser, CcLexer *lexer, CcArena *arena)
{
    if (parser == NULL) return;

    parser->lexer = lexer;
    parser->arena = arena;
    parser->failed = false;
    parser->error = NULL;
    parser->error_span.file_id = CC_FILE_ID_INVALID;
    parser->error_span.start = 0;
    parser->error_span.end = 0;
    parser->current.kind = CC_TOKEN_INVALID;
    parser->current.span = parser->error_span;

    if (lexer != NULL) parser->current = cc_lexer_next(lexer);
}

CcFunctionDeclaration *cc_parser_parse_program(CcParser *parser)
{
    if (parser == NULL) return NULL;

    if (parser->lexer == NULL || parser->arena == NULL) {
        if (parser->error == NULL && !parser->failed) {
            parser->failed = true;
            parser->error = "invalid lexer or arena";
        }

        return NULL;
    }

    CcFunctionDeclaration *first = NULL;
    CcFunctionDeclaration **link = &first;

    while (!parser->failed && parser->current.kind != CC_TOKEN_EOF) {
        if (parser->current.kind != CC_TOKEN_KW_FN) {
            report_error(parser, "expected function declaration");
            break;
        }

        CcFunctionDeclaration *function = parse_function(parser);

        if (parser->failed || function == NULL) break;

        *link = function;
        link = &function->next;
    }

    return first;
}
