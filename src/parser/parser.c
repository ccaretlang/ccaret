#include "parser/parser.h"

#include <limits.h>
#include <stddef.h>

enum {
    PREC_NONE = 0,
    PREC_RANGE,
    PREC_OR_OR,
    PREC_AND_AND,
    PREC_PIPE,
    PREC_CARET,
    PREC_AMP,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_SHIFT,
    PREC_ADDITIVE,
    PREC_MULTIPLICATIVE
};

static CcExpression *parse_expression(CcParser *parser);
static CcExpression *parse_postfix(CcParser *parser);

typedef struct {
    CcTokenKind token_kind;
    CcBinaryOperator operator;
    int precedence;
} CcBinaryOperatorInfo;

typedef struct {
    CcTokenKind token_kind;
    CcUnaryOperator operator;
} CcUnaryOperatorInfo;

typedef struct {
    CcTokenKind token_kind;
    CcAssignmentOperator operator;
} CcAssignmentOperatorInfo;

static const CcAssignmentOperatorInfo assignment_operators[] = {
    { CC_TOKEN_ASSIGN, CC_AST_ASSIGNMENT_ASSIGN },
    { CC_TOKEN_PLUS_ASSIGN, CC_AST_ASSIGNMENT_PLUS },
    { CC_TOKEN_MINUS_ASSIGN, CC_AST_ASSIGNMENT_MINUS },
    { CC_TOKEN_STAR_ASSIGN, CC_AST_ASSIGNMENT_STAR },
    { CC_TOKEN_SLASH_ASSIGN, CC_AST_ASSIGNMENT_SLASH },
    { CC_TOKEN_PERCENT_ASSIGN, CC_AST_ASSIGNMENT_PERCENT },
    { CC_TOKEN_AMP_ASSIGN, CC_AST_ASSIGNMENT_AMP },
    { CC_TOKEN_PIPE_ASSIGN, CC_AST_ASSIGNMENT_PIPE },
    { CC_TOKEN_CARET_ASSIGN, CC_AST_ASSIGNMENT_CARET },
    { CC_TOKEN_SHIFT_LEFT_ASSIGN, CC_AST_ASSIGNMENT_SHIFT_LEFT },
    { CC_TOKEN_SHIFT_RIGHT_ASSIGN, CC_AST_ASSIGNMENT_SHIFT_RIGHT }
};

static const CcBinaryOperatorInfo binary_operators[] = {
    { CC_TOKEN_DOT_DOT, CC_AST_BINARY_OPERATOR_RANGE, PREC_RANGE },
    { CC_TOKEN_DOT_DOT_EQUAL,
      CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE,
      PREC_RANGE },
    { CC_TOKEN_OR_OR, CC_AST_BINARY_OPERATOR_OR_OR, PREC_OR_OR },
    { CC_TOKEN_AND_AND, CC_AST_BINARY_OPERATOR_AND_AND, PREC_AND_AND },
    { CC_TOKEN_PIPE, CC_AST_BINARY_OPERATOR_PIPE, PREC_PIPE },
    { CC_TOKEN_CARET, CC_AST_BINARY_OPERATOR_CARET, PREC_CARET },
    { CC_TOKEN_AMP, CC_AST_BINARY_OPERATOR_AMP, PREC_AMP },
    { CC_TOKEN_EQ, CC_AST_BINARY_OPERATOR_EQ, PREC_EQUALITY },
    { CC_TOKEN_NE, CC_AST_BINARY_OPERATOR_NE, PREC_EQUALITY },
    { CC_TOKEN_LT, CC_AST_BINARY_OPERATOR_LT, PREC_COMPARISON },
    { CC_TOKEN_LE, CC_AST_BINARY_OPERATOR_LE, PREC_COMPARISON },
    { CC_TOKEN_GT, CC_AST_BINARY_OPERATOR_GT, PREC_COMPARISON },
    { CC_TOKEN_GE, CC_AST_BINARY_OPERATOR_GE, PREC_COMPARISON },
    { CC_TOKEN_SHIFT_LEFT, CC_AST_BINARY_OPERATOR_SHIFT_LEFT, PREC_SHIFT },
    { CC_TOKEN_SHIFT_RIGHT, CC_AST_BINARY_OPERATOR_SHIFT_RIGHT, PREC_SHIFT },
    { CC_TOKEN_PLUS, CC_AST_BINARY_OPERATOR_PLUS, PREC_ADDITIVE },
    { CC_TOKEN_MINUS, CC_AST_BINARY_OPERATOR_MINUS, PREC_ADDITIVE },
    { CC_TOKEN_PLUS_PERCENT,
      CC_AST_BINARY_OPERATOR_PLUS_PERCENT,
      PREC_ADDITIVE },
    { CC_TOKEN_MINUS_PERCENT,
      CC_AST_BINARY_OPERATOR_MINUS_PERCENT,
      PREC_ADDITIVE },
    { CC_TOKEN_STAR,
      CC_AST_BINARY_OPERATOR_STAR,
      PREC_MULTIPLICATIVE },
    { CC_TOKEN_SLASH,
      CC_AST_BINARY_OPERATOR_SLASH,
      PREC_MULTIPLICATIVE },
    { CC_TOKEN_PERCENT,
      CC_AST_BINARY_OPERATOR_PERCENT,
      PREC_MULTIPLICATIVE },
    { CC_TOKEN_STAR_PERCENT,
      CC_AST_BINARY_OPERATOR_STAR_PERCENT,
      PREC_MULTIPLICATIVE }
};

static const CcUnaryOperatorInfo unary_operators[] = {
    { CC_TOKEN_NOT, CC_AST_UNARY_OPERATOR_NOT },
    { CC_TOKEN_TILDE, CC_AST_UNARY_OPERATOR_TILDE },
    { CC_TOKEN_MINUS, CC_AST_UNARY_OPERATOR_MINUS },
    { CC_TOKEN_AMP, CC_AST_UNARY_OPERATOR_AMP },
    { CC_TOKEN_STAR, CC_AST_UNARY_OPERATOR_STAR }
};

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

static const CcBinaryOperatorInfo *binary_operator_info(CcTokenKind kind)
{
    for (size_t i = 0; i < sizeof(binary_operators) / sizeof(binary_operators[0]);
         i++) {
        if (binary_operators[i].token_kind == kind)
            return &binary_operators[i];
    }

    return NULL;
}

static const CcUnaryOperatorInfo *unary_operator_info(CcTokenKind kind)
{
    for (size_t i = 0; i < sizeof(unary_operators) / sizeof(unary_operators[0]);
         i++) {
        if (unary_operators[i].token_kind == kind)
            return &unary_operators[i];
    }

    return NULL;
}

static const CcAssignmentOperatorInfo *assignment_operator_info(
    CcTokenKind kind)
{
    for (size_t i = 0;
         i < sizeof(assignment_operators) / sizeof(assignment_operators[0]);
         i++) {
        if (assignment_operators[i].token_kind == kind)
            return &assignment_operators[i];
    }

    return NULL;
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

static CcExpression *parse_primary(CcParser *parser)
{
    if (parser->current.kind == CC_TOKEN_INTEGER_LITERAL) {
        CcToken token = parser->current;
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

    if (parser->current.kind == CC_TOKEN_KW_TRUE ||
        parser->current.kind == CC_TOKEN_KW_FALSE) {
        CcToken token = parser->current;
        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_BOOLEAN_LITERAL;
        expression->span = token.span;
        expression->boolean_literal = token.kind == CC_TOKEN_KW_TRUE;

        advance(parser);
        return expression;
    }

    if (parser->current.kind == CC_TOKEN_STRING_LITERAL ||
        parser->current.kind == CC_TOKEN_C_STRING_LITERAL) {
        CcToken token = parser->current;
        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = token.kind == CC_TOKEN_STRING_LITERAL
            ? CC_AST_EXPRESSION_STRING_LITERAL
            : CC_AST_EXPRESSION_C_STRING_LITERAL;
        expression->span = token.span;

        advance(parser);
        return expression;
    }

    if (parser->current.kind == CC_TOKEN_BYTE_LITERAL) {
        CcToken token = parser->current;
        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_BYTE_LITERAL;
        expression->span = token.span;

        advance(parser);
        return expression;
    }

    if (parser->current.kind == CC_TOKEN_KW_NONE) {
        CcToken token = parser->current;
        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_NONE_LITERAL;
        expression->span = token.span;

        advance(parser);
        return expression;
    }

    if (parser->current.kind == CC_TOKEN_IDENTIFIER) {
        CcToken token = parser->current;
        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_IDENTIFIER;
        expression->span = token.span;
        expression->identifier_name = token.span;

        advance(parser);
        return expression;
    }

    if (parser->current.kind == CC_TOKEN_LPAREN) {
        CcToken lparen = parser->current;

        advance(parser);

        CcExpression *inner = parse_expression(parser);

        if (parser->failed || inner == NULL) return NULL;

        CcToken rparen;

        if (!expect(parser, CC_TOKEN_RPAREN, "expected `)` after expression",
                    &rparen))
            return NULL;

        CcExpression *outer = allocate(parser, sizeof(*outer));

        if (outer == NULL) return NULL;

        outer->kind = CC_AST_EXPRESSION_PARENTHESIZED;
        outer->span.file_id = lparen.span.file_id;
        outer->span.start = lparen.span.start;
        outer->span.end = rparen.span.end;
        outer->parenthesized_inner = inner;

        return outer;
    }

    report_error(parser, "expected expression");
    return NULL;
}

/*
 * Expressions are right-associative for unary operators and left-associative
 * for the binary operators, per docs/GRAMMAR.md. Ranges and casts are still
 * open and are not part of the grammar yet.
 */
static CcExpression *parse_unary(CcParser *parser)
{
    const CcUnaryOperatorInfo *info =
        unary_operator_info(parser->current.kind);

    if (info != NULL) {
        CcToken operator = parser->current;

        advance(parser);

        CcExpression *operand = parse_unary(parser);

        if (parser->failed || operand == NULL) return NULL;

        CcExpression *outer = allocate(parser, sizeof(*outer));

        if (outer == NULL) return NULL;

        outer->kind = CC_AST_EXPRESSION_UNARY;
        outer->span.file_id = operator.span.file_id;
        outer->span.start = operator.span.start;
        outer->span.end = operand->span.end;
        outer->unary.operand = operand;
        outer->unary.operator = info->operator;

        return outer;
    }

    return parse_postfix(parser);
}

static CcExpression *parse_postfix(CcParser *parser)
{
    CcExpression *expression = parse_primary(parser);

    if (parser->failed || expression == NULL) return NULL;

    for (;;) {
        if (parser->current.kind == CC_TOKEN_LPAREN) {
            advance(parser);

            CcCallArgument *arguments = NULL;
            CcCallArgument **link = &arguments;

            if (parser->current.kind != CC_TOKEN_RPAREN) {
                for (;;) {
                    CcExpression *argument = parse_expression(parser);

                    if (parser->failed || argument == NULL) return NULL;

                    CcCallArgument *node = allocate(parser, sizeof(*node));

                    if (node == NULL) return NULL;

                    node->value = argument;
                    node->next = NULL;
                    *link = node;
                    link = &node->next;

                    if (parser->current.kind != CC_TOKEN_COMMA) break;

                    advance(parser);
                }
            }

            CcToken rparen;

            if (!expect(parser, CC_TOKEN_RPAREN,
                        "expected `)` after arguments", &rparen))
                return NULL;

            CcExpression *call = allocate(parser, sizeof(*call));

            if (call == NULL) return NULL;

            call->kind = CC_AST_EXPRESSION_CALL;
            call->span.file_id = expression->span.file_id;
            call->span.start = expression->span.start;
            call->span.end = rparen.span.end;
            call->call.callee = expression;
            call->call.arguments = arguments;

            expression = call;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_DOT) {
            advance(parser);

            if (parser->current.kind == CC_TOKEN_IDENTIFIER) {
                CcToken name = parser->current;

                advance(parser);

                CcExpression *member = allocate(parser, sizeof(*member));

                if (member == NULL) return NULL;

                member->kind = CC_AST_EXPRESSION_MEMBER;
                member->span.file_id = expression->span.file_id;
                member->span.start = expression->span.start;
                member->span.end = name.span.end;
                member->member.operand = expression;
                member->member.name = name.span;

                expression = member;
                continue;
            }

            if (parser->current.kind == CC_TOKEN_INTEGER_LITERAL) {
                CcToken index_token = parser->current;
                unsigned long long value = 0;
                bool overflow = false;

                if (!integer_value(parser, index_token, &value, &overflow)) {
                    report_error(parser, "malformed integer literal");
                    return NULL;
                }

                advance(parser);

                CcExpression *tuple_member =
                    allocate(parser, sizeof(*tuple_member));

                if (tuple_member == NULL) return NULL;

                tuple_member->kind = CC_AST_EXPRESSION_TUPLE_MEMBER;
                tuple_member->span.file_id = expression->span.file_id;
                tuple_member->span.start = expression->span.start;
                tuple_member->span.end = index_token.span.end;
                tuple_member->tuple_member.operand = expression;
                tuple_member->tuple_member.index = index_token.span;
                tuple_member->tuple_member.index_value.value = value;
                tuple_member->tuple_member.index_value.overflow = overflow;

                expression = tuple_member;
                continue;
            }

            report_error(parser, "expected member name");
            return NULL;
        }

        if (parser->current.kind == CC_TOKEN_LBRACKET) {
            advance(parser);

            CcExpression *index = parse_expression(parser);

            if (parser->failed || index == NULL) return NULL;

            CcToken rbracket;

            if (!expect(parser, CC_TOKEN_RBRACKET,
                        "expected `]` after index", &rbracket))
                return NULL;

            CcExpression *indexed = allocate(parser, sizeof(*indexed));

            if (indexed == NULL) return NULL;

            indexed->kind = CC_AST_EXPRESSION_INDEX;
            indexed->span.file_id = expression->span.file_id;
            indexed->span.start = expression->span.start;
            indexed->span.end = rbracket.span.end;
            indexed->index.operand = expression;
            indexed->index.index = index;

            expression = indexed;
            continue;
        }

        break;
    }

    return expression;
}

static CcExpression *parse_binary(CcParser *parser, int min_precedence)
{
    CcExpression *left = parse_unary(parser);

    if (parser->failed || left == NULL) return NULL;

    for (;;) {
        const CcBinaryOperatorInfo *info =
            binary_operator_info(parser->current.kind);

        if (info == NULL || info->precedence < min_precedence) break;

        advance(parser);

        CcExpression *right = parse_binary(parser, info->precedence + 1);

        if (parser->failed || right == NULL) return NULL;

        CcExpression *outer = allocate(parser, sizeof(*outer));

        if (outer == NULL) return NULL;

        outer->kind = CC_AST_EXPRESSION_BINARY;
        outer->span.file_id = left->span.file_id;
        outer->span.start = left->span.start;
        outer->span.end = right->span.end;
        outer->binary.left = left;
        outer->binary.right = right;
        outer->binary.operator = info->operator;

        if (info->operator == CC_AST_BINARY_OPERATOR_RANGE ||
            info->operator == CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE) {
            if (parser->current.kind == CC_TOKEN_DOT_DOT ||
                parser->current.kind == CC_TOKEN_DOT_DOT_EQUAL) {
                report_error(parser, "ranges are not chainable");
                return NULL;
            }
        }

        left = outer;
    }

    return left;
}

static CcExpression *parse_expression(CcParser *parser)
{
    return parse_binary(parser, PREC_NONE);
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

static CcStatement *parse_let_statement(CcParser *parser)
{
    CcToken keyword = parser->current;
    bool mutable = keyword.kind == CC_TOKEN_KW_VAR;

    advance(parser);

    CcToken name;

    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected variable name", &name))
        return NULL;

    CcType *type = NULL;

    if (accept(parser, CC_TOKEN_COLON, NULL)) {
        type = parse_type(parser);

        if (parser->failed || type == NULL) return NULL;
    }

    if (!expect(parser, CC_TOKEN_ASSIGN,
                "expected `=` in variable declaration", NULL))
        return NULL;

    CcExpression *initializer = parse_expression(parser);

    if (parser->failed || initializer == NULL) return NULL;

    CcToken semicolon;

    if (!expect(parser, CC_TOKEN_SEMICOLON, "expected `;`", &semicolon))
        return NULL;

    CcStatement *statement = allocate(parser, sizeof(*statement));

    if (statement == NULL) return NULL;

    statement->kind = CC_AST_STATEMENT_LET;
    statement->span.file_id = keyword.span.file_id;
    statement->span.start = keyword.span.start;
    statement->span.end = semicolon.span.end;
    statement->next = NULL;
    statement->let.mutable = mutable;
    statement->let.name = name.span;
    statement->let.type = type;
    statement->let.initializer = initializer;

    return statement;
}

static CcBlock *parse_block(CcParser *parser);
static CcIf *parse_if(CcParser *parser);
static CcWhile *parse_while(CcParser *parser);
static CcLoop *parse_loop(CcParser *parser);
static CcFor *parse_for(CcParser *parser);
static CcStatement *parse_loop_keyword_statement(CcParser *parser,
                                                 CcStatementKind kind);

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

        if (parser->current.kind == CC_TOKEN_KW_LET ||
            parser->current.kind == CC_TOKEN_KW_VAR) {
            CcStatement *statement = parse_let_statement(parser);

            if (parser->failed || statement == NULL) break;

            *link = statement;
            link = &statement->next;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_KW_IF) {
            CcIf *if_statement = parse_if(parser);

            if (parser->failed || if_statement == NULL) break;

            CcStatement *statement = allocate(parser, sizeof(*statement));

            if (statement == NULL) break;

            statement->kind = CC_AST_STATEMENT_IF;
            statement->span = if_statement->span;
            statement->next = NULL;
            statement->if_statement = if_statement;

            *link = statement;
            link = &statement->next;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_KW_WHILE) {
            CcWhile *while_statement = parse_while(parser);

            if (parser->failed || while_statement == NULL) break;

            CcStatement *statement = allocate(parser, sizeof(*statement));

            if (statement == NULL) break;

            statement->kind = CC_AST_STATEMENT_WHILE;
            statement->span = while_statement->span;
            statement->next = NULL;
            statement->while_statement = while_statement;

            *link = statement;
            link = &statement->next;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_KW_BREAK ||
            parser->current.kind == CC_TOKEN_KW_CONTINUE) {
            CcStatementKind kind = CC_AST_STATEMENT_BREAK;

            if (parser->current.kind == CC_TOKEN_KW_CONTINUE)
                kind = CC_AST_STATEMENT_CONTINUE;

            CcStatement *statement = parse_loop_keyword_statement(parser, kind);

            if (parser->failed || statement == NULL) break;

            *link = statement;
            link = &statement->next;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_KW_LOOP) {
            CcLoop *loop_statement = parse_loop(parser);

            if (parser->failed || loop_statement == NULL) break;

            CcStatement *statement = allocate(parser, sizeof(*statement));

            if (statement == NULL) break;

            statement->kind = CC_AST_STATEMENT_LOOP;
            statement->span = loop_statement->span;
            statement->next = NULL;
            statement->loop_statement = loop_statement;

            *link = statement;
            link = &statement->next;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_KW_FOR) {
            CcFor *for_statement = parse_for(parser);

            if (parser->failed || for_statement == NULL) break;

            CcStatement *statement = allocate(parser, sizeof(*statement));

            if (statement == NULL) break;

            statement->kind = CC_AST_STATEMENT_FOR;
            statement->span = for_statement->span;
            statement->next = NULL;
            statement->for_statement = for_statement;

            *link = statement;
            link = &statement->next;
            continue;
        }

        CcExpression *expression = parse_expression(parser);

        if (parser->failed || expression == NULL) break;

        CcToken semicolon;
        CcStatement *statement = NULL;

        const CcAssignmentOperatorInfo *assignment =
            assignment_operator_info(parser->current.kind);

        if (assignment != NULL) {
            advance(parser);

            CcExpression *value = parse_expression(parser);

            if (parser->failed || value == NULL) break;

            if (!expect(parser, CC_TOKEN_SEMICOLON, "expected `;`", &semicolon))
                break;

            statement = allocate(parser, sizeof(*statement));

            if (statement == NULL) break;

            statement->kind = CC_AST_STATEMENT_ASSIGNMENT;
            statement->assignment.operator = assignment->operator;
            statement->assignment.target = expression;
            statement->assignment.value = value;
        } else {
            if (!expect(parser, CC_TOKEN_SEMICOLON, "expected `;`", &semicolon))
                break;

            statement = allocate(parser, sizeof(*statement));

            if (statement == NULL) break;

            statement->kind = CC_AST_STATEMENT_EXPRESSION;
            statement->expression = expression;
        }

        statement->span.file_id = expression->span.file_id;
        statement->span.start = expression->span.start;
        statement->span.end = semicolon.span.end;
        statement->next = NULL;

        *link = statement;
        link = &statement->next;
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

static CcIf *parse_if(CcParser *parser)
{
    CcToken keyword = parser->current;

    advance(parser);

    CcExpression *condition = parse_expression(parser);

    if (parser->failed || condition == NULL) return NULL;

    CcBlock *then_block = parse_block(parser);

    if (parser->failed || then_block == NULL) return NULL;

    CcIf *if_statement = allocate(parser, sizeof(*if_statement));

    if (if_statement == NULL) return NULL;

    if_statement->span.file_id = keyword.span.file_id;
    if_statement->span.start = keyword.span.start;
    if_statement->span.end = then_block->span.end;
    if_statement->condition = condition;
    if_statement->then_block = then_block;
    if_statement->else_if = NULL;
    if_statement->else_block = NULL;

    if (!accept(parser, CC_TOKEN_KW_ELSE, NULL)) return if_statement;

    if (parser->current.kind == CC_TOKEN_KW_IF) {
        CcIf *else_if = parse_if(parser);

        if (parser->failed || else_if == NULL) return NULL;

        if_statement->else_if = else_if;
        if_statement->span.end = else_if->span.end;

        return if_statement;
    }

    CcBlock *else_block = parse_block(parser);

    if (parser->failed || else_block == NULL) return NULL;

    if_statement->else_block = else_block;
    if_statement->span.end = else_block->span.end;

    return if_statement;
}

static CcWhile *parse_while(CcParser *parser)
{
    CcToken keyword = parser->current;

    advance(parser);

    CcExpression *condition = parse_expression(parser);

    if (parser->failed || condition == NULL) return NULL;

    CcBlock *body = parse_block(parser);

    if (parser->failed || body == NULL) return NULL;

    CcWhile *while_statement = allocate(parser, sizeof(*while_statement));

    if (while_statement == NULL) return NULL;

    while_statement->span.file_id = keyword.span.file_id;
    while_statement->span.start = keyword.span.start;
    while_statement->span.end = body->span.end;
    while_statement->condition = condition;
    while_statement->body = body;

    return while_statement;
}

static CcStatement *parse_loop_keyword_statement(CcParser *parser,
                                                 CcStatementKind kind)
{
    CcToken keyword = parser->current;

    advance(parser);

    CcToken semicolon;

    if (!expect(parser, CC_TOKEN_SEMICOLON, "expected `;`", &semicolon))
        return NULL;

    CcStatement *statement = allocate(parser, sizeof(*statement));

    if (statement == NULL) return NULL;

    statement->kind = kind;
    statement->span.file_id = keyword.span.file_id;
    statement->span.start = keyword.span.start;
    statement->span.end = semicolon.span.end;
    statement->next = NULL;

    return statement;
}

static CcLoop *parse_loop(CcParser *parser)
{
    CcToken keyword = parser->current;

    advance(parser);

    CcBlock *body = parse_block(parser);

    if (parser->failed || body == NULL) return NULL;

    CcLoop *loop_statement = allocate(parser, sizeof(*loop_statement));

    if (loop_statement == NULL) return NULL;

    loop_statement->span.file_id = keyword.span.file_id;
    loop_statement->span.start = keyword.span.start;
    loop_statement->span.end = body->span.end;
    loop_statement->body = body;

    return loop_statement;
}

static CcFor *parse_for(CcParser *parser)
{
    CcToken keyword = parser->current;

    advance(parser);

    CcToken name;

    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected loop variable", &name))
        return NULL;

    if (!expect(parser, CC_TOKEN_KW_IN, "expected `in` after loop variable",
                NULL))
        return NULL;

    CcExpression *range = parse_expression(parser);

    if (parser->failed || range == NULL) return NULL;

    CcBlock *body = parse_block(parser);

    if (parser->failed || body == NULL) return NULL;

    CcFor *for_statement = allocate(parser, sizeof(*for_statement));

    if (for_statement == NULL) return NULL;

    for_statement->span.file_id = keyword.span.file_id;
    for_statement->span.start = keyword.span.start;
    for_statement->span.end = body->span.end;
    for_statement->name = name.span;
    for_statement->range = range;
    for_statement->body = body;

    return for_statement;
}

static CcFunctionDeclaration *parse_function(CcParser *parser)
{
    uint32_t start = parser->current.span.start;
    bool is_public = accept(parser, CC_TOKEN_KW_PUB, NULL);
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
    function->is_public = is_public;
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
        if (parser->current.kind != CC_TOKEN_KW_FN &&
            parser->current.kind != CC_TOKEN_KW_PUB) {
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
