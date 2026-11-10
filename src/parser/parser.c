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
    if (parser->current.kind == CC_TOKEN_LBRACKET) {
        CcToken opening = parser->current;
        advance(parser);
        CcTupleExpressionElement *first = NULL;
        CcTupleExpressionElement **link = &first;
        if (parser->current.kind != CC_TOKEN_RBRACKET) {
            for (;;) {
                CcExpression *value = parse_expression(parser);
                if (parser->failed || value == NULL) return NULL;
                CcTupleExpressionElement *element =
                    allocate(parser, sizeof(*element));
                if (element == NULL) return NULL;
                element->value = value;
                element->next = NULL;
                *link = element;
                link = &element->next;
                if (!accept(parser, CC_TOKEN_COMMA, NULL)) break;
                if (parser->current.kind == CC_TOKEN_RBRACKET) break;
            }
        }
        CcToken closing;
        if (!expect(parser, CC_TOKEN_RBRACKET,
                    "expected `]` after array literal", &closing))
            return NULL;
        CcExpression *array = allocate(parser, sizeof(*array));
        if (array == NULL) return NULL;
        array->kind = CC_AST_EXPRESSION_ARRAY;
        array->span.file_id = opening.span.file_id;
        array->span.start = opening.span.start;
        array->span.end = closing.span.end;
        array->array_elements = first;
        return array;
    }
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

    if (parser->current.kind == CC_TOKEN_KW_SOME) {
        CcToken keyword = parser->current;

        advance(parser);

        if (!expect(parser, CC_TOKEN_LPAREN, "expected `(` after `some`",
                    NULL))
            return NULL;

        CcExpression *value = parse_expression(parser);

        if (parser->failed || value == NULL) return NULL;

        CcToken rparen;

        if (!expect(parser, CC_TOKEN_RPAREN, "expected `)` after `some` value",
                    &rparen))
            return NULL;

        CcExpression *expression = allocate(parser, sizeof(*expression));

        if (expression == NULL) return NULL;

        expression->kind = CC_AST_EXPRESSION_SOME;
        expression->span.file_id = keyword.span.file_id;
        expression->span.start = keyword.span.start;
        expression->span.end = rparen.span.end;
        expression->some_value = value;

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

        CcToken rparen;

        if (accept(parser, CC_TOKEN_RPAREN, &rparen)) {
            CcExpression *tuple = allocate(parser, sizeof(*tuple));

            if (tuple == NULL) return NULL;

            tuple->kind = CC_AST_EXPRESSION_TUPLE;
            tuple->span.file_id = lparen.span.file_id;
            tuple->span.start = lparen.span.start;
            tuple->span.end = rparen.span.end;
            tuple->tuple_elements = NULL;
            return tuple;
        }

        CcExpression *inner = parse_expression(parser);

        if (parser->failed || inner == NULL) return NULL;

        if (parser->current.kind == CC_TOKEN_COMMA) {
            CcTupleExpressionElement *first = allocate(parser, sizeof(*first));

            if (first == NULL) return NULL;

            first->value = inner;
            first->next = NULL;
            CcTupleExpressionElement **link = &first->next;

            while (accept(parser, CC_TOKEN_COMMA, NULL)) {
                if (parser->current.kind == CC_TOKEN_RPAREN) break;

                CcExpression *value = parse_expression(parser);

                if (parser->failed || value == NULL) return NULL;

                CcTupleExpressionElement *element =
                    allocate(parser, sizeof(*element));

                if (element == NULL) return NULL;

                element->value = value;
                element->next = NULL;
                *link = element;
                link = &element->next;
            }

            if (!expect(parser, CC_TOKEN_RPAREN,
                        "expected `)` after tuple expression", &rparen))
                return NULL;

            CcExpression *tuple = allocate(parser, sizeof(*tuple));

            if (tuple == NULL) return NULL;

            tuple->kind = CC_AST_EXPRESSION_TUPLE;
            tuple->span.file_id = lparen.span.file_id;
            tuple->span.start = lparen.span.start;
            tuple->span.end = rparen.span.end;
            tuple->tuple_elements = first;
            return tuple;
        }

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
        bool named_fields = false;
        if (parser->current.kind == CC_TOKEN_LBRACE &&
            parser->before_block) {
            CcLexer peek = *parser->lexer;
            CcToken first = cc_lexer_next(&peek);
            CcToken second = cc_lexer_next(&peek);
            named_fields = first.kind == CC_TOKEN_IDENTIFIER &&
                           second.kind == CC_TOKEN_COLON;
        }
        if (parser->current.kind == CC_TOKEN_LBRACE &&
            (expression->kind == CC_AST_EXPRESSION_IDENTIFIER ||
             (expression->kind == CC_AST_EXPRESSION_MEMBER &&
              expression->member.operand->kind == CC_AST_EXPRESSION_IDENTIFIER)) &&
            (!parser->before_block || named_fields)) {
            advance(parser);
            CcStructFieldValue *fields = NULL;
            CcStructFieldValue **link = &fields;
            while (parser->current.kind != CC_TOKEN_RBRACE) {
                CcToken name;
                if (!expect(parser, CC_TOKEN_IDENTIFIER,
                            "expected struct field name", &name))
                    return NULL;
                if (!expect(parser, CC_TOKEN_COLON,
                            "expected `:` after struct field name", NULL))
                    return NULL;
                bool saved = parser->before_block;
                parser->before_block = false;
                CcExpression *value = parse_expression(parser);
                parser->before_block = saved;
                if (parser->failed || value == NULL) return NULL;
                CcStructFieldValue *field = allocate(parser, sizeof(*field));
                if (field == NULL) return NULL;
                field->name = name.span;
                field->value = value;
                field->next = NULL;
                *link = field;
                link = &field->next;
                if (!accept(parser, CC_TOKEN_COMMA, NULL)) break;
                if (parser->current.kind == CC_TOKEN_RBRACE) break;
            }
            CcToken end;
            if (!expect(parser, CC_TOKEN_RBRACE,
                        "expected `}` after struct literal", &end))
                return NULL;
            CcExpression *literal = allocate(parser, sizeof(*literal));
            if (literal == NULL) return NULL;
            literal->kind = CC_AST_EXPRESSION_STRUCT;
            literal->span = expression->span;
            literal->span.end = end.span.end;
            literal->struct_literal.name = expression->kind ==
                CC_AST_EXPRESSION_IDENTIFIER
                    ? expression->identifier_name : expression->span;
            literal->struct_literal.fields = fields;
            expression = literal;
            continue;
        }
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
    if (parser->current.kind == CC_TOKEN_LPAREN) {
        CcToken lparen = parser->current;

        advance(parser);

        CcToken rparen;
        CcType *inner = NULL;

        if (parser->current.kind != CC_TOKEN_RPAREN) {
            inner = parse_type(parser);

            if (parser->failed || inner == NULL) return NULL;
        }

        if (inner == NULL || parser->current.kind == CC_TOKEN_COMMA) {
            CcTupleTypeElement *first = NULL;
            CcTupleTypeElement **link = &first;

            if (inner != NULL) {
                first = allocate(parser, sizeof(*first));

                if (first == NULL) return NULL;

                first->type = inner;
                first->next = NULL;
                link = &first->next;
            }

            while (accept(parser, CC_TOKEN_COMMA, NULL)) {
                if (parser->current.kind == CC_TOKEN_RPAREN) break;

                CcType *element_type = parse_type(parser);

                if (parser->failed || element_type == NULL) return NULL;

                CcTupleTypeElement *element =
                    allocate(parser, sizeof(*element));

                if (element == NULL) return NULL;

                element->type = element_type;
                element->next = NULL;
                *link = element;
                link = &element->next;
            }

            if (!expect(parser, CC_TOKEN_RPAREN,
                        "expected `)` after tuple type", &rparen))
                return NULL;

            CcType *type = allocate(parser, sizeof(*type));

            if (type == NULL) return NULL;

            type->kind = CC_AST_TYPE_TUPLE;
            type->span.file_id = lparen.span.file_id;
            type->span.start = lparen.span.start;
            type->span.end = rparen.span.end;
            type->pointer_pointee_const = false;
            type->slice_element_const = false;
            type->tuple_elements = first;
            return type;
        }

        if (!expect(parser, CC_TOKEN_RPAREN,
                    "expected `)` after type", &rparen))
            return NULL;

        CcType *type = allocate(parser, sizeof(*type));

        if (type == NULL) return NULL;

        type->kind = CC_AST_TYPE_PARENTHESIZED;
        type->span.file_id = lparen.span.file_id;
        type->span.start = lparen.span.start;
        type->span.end = rparen.span.end;
        type->pointer_pointee_const = false;
        type->slice_element_const = false;
        type->parenthesized_inner = inner;
        return type;
    }

    if (parser->current.kind == CC_TOKEN_LBRACKET) {
        CcToken lbracket = parser->current;

        advance(parser);

        CcExpression *length = NULL;

        if (parser->current.kind != CC_TOKEN_RBRACKET) {
            length = parse_expression(parser);

            if (parser->failed || length == NULL) return NULL;
        }

        if (!expect(parser, CC_TOKEN_RBRACKET,
                    length == NULL ? "expected `]` in slice type"
                                   : "expected `]` after array length", NULL))
            return NULL;

        bool element_const = length == NULL &&
            accept(parser, CC_TOKEN_KW_CONST, NULL);

        CcType *inner = parse_type(parser);

        if (parser->failed || inner == NULL) return NULL;

        CcType *type = allocate(parser, sizeof(*type));

        if (type == NULL) return NULL;

        type->kind = length == NULL ? CC_AST_TYPE_SLICE : CC_AST_TYPE_ARRAY;
        type->span.file_id = lbracket.span.file_id;
        type->span.start = lbracket.span.start;
        type->span.end = inner->span.end;
        type->pointer_pointee_const = false;
        type->slice_element_const = element_const;
        if (length == NULL)
            type->slice_inner = inner;
        else {
            type->array.length = length;
            type->array.element = inner;
        }

        return type;
    }

    if (parser->current.kind == CC_TOKEN_QUESTION ||
        parser->current.kind == CC_TOKEN_STAR) {
        CcToken prefix = parser->current;

        advance(parser);

        bool pointee_const = prefix.kind == CC_TOKEN_STAR &&
            accept(parser, CC_TOKEN_KW_CONST, NULL);

        CcType *inner = parse_type(parser);

        if (parser->failed || inner == NULL) return NULL;

        CcType *type = allocate(parser, sizeof(*type));

        if (type == NULL) return NULL;

        type->kind = prefix.kind == CC_TOKEN_QUESTION
            ? CC_AST_TYPE_OPTIONAL
            : CC_AST_TYPE_POINTER;
        type->span.file_id = prefix.span.file_id;
        type->span.start = prefix.span.start;
        type->span.end = inner->span.end;
        type->pointer_pointee_const = pointee_const;
        type->slice_element_const = false;
        if (prefix.kind == CC_TOKEN_QUESTION)
            type->optional_inner = inner;
        else
            type->pointer_inner = inner;

        return type;
    }

    CcToken name;

    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected a type", &name))
        return NULL;

    CcType *type = allocate(parser, sizeof(*type));

    if (type == NULL) return NULL;

    type->kind = CC_AST_TYPE_NAMED;
    type->span = name.span;
    if (parser->current.kind == CC_TOKEN_DOT) {
        advance(parser);
        CcToken member;
        if (!expect(parser, CC_TOKEN_IDENTIFIER,
                    "expected imported type after `.`", &member))
            return NULL;
        type->span.end = member.span.end;
    }
    type->pointer_pointee_const = false;
    type->slice_element_const = false;
    type->optional_inner = NULL;

    return type;
}

static CcPattern *parse_pattern(CcParser *parser)
{
    if (parser->current.kind == CC_TOKEN_IDENTIFIER) {
        CcToken name = parser->current;
        CcPattern *pattern = allocate(parser, sizeof(*pattern));

        if (pattern == NULL) return NULL;

        pattern->kind = CC_AST_PATTERN_NAME;
        pattern->span = name.span;
        pattern->name = name.span;

        advance(parser);
        return pattern;
    }

    if (parser->current.kind == CC_TOKEN_LPAREN) {
        CcToken lparen = parser->current;

        advance(parser);

        CcPattern *inner = NULL;

        if (parser->current.kind != CC_TOKEN_RPAREN) {
            inner = parse_pattern(parser);

            if (parser->failed || inner == NULL) return NULL;
        }

        CcToken rparen;

        if (inner == NULL || parser->current.kind == CC_TOKEN_COMMA) {
            CcTuplePatternElement *first = NULL;
            CcTuplePatternElement **link = &first;

            if (inner != NULL) {
                first = allocate(parser, sizeof(*first));

                if (first == NULL) return NULL;

                first->pattern = inner;
                first->next = NULL;
                link = &first->next;
            }

            while (accept(parser, CC_TOKEN_COMMA, NULL)) {
                if (parser->current.kind == CC_TOKEN_RPAREN) break;

                CcPattern *element_pattern = parse_pattern(parser);

                if (parser->failed || element_pattern == NULL) return NULL;

                CcTuplePatternElement *element =
                    allocate(parser, sizeof(*element));

                if (element == NULL) return NULL;

                element->pattern = element_pattern;
                element->next = NULL;
                *link = element;
                link = &element->next;
            }

            if (!expect(parser, CC_TOKEN_RPAREN,
                        "expected `)` after tuple pattern", &rparen))
                return NULL;

            CcPattern *pattern = allocate(parser, sizeof(*pattern));

            if (pattern == NULL) return NULL;

            pattern->kind = CC_AST_PATTERN_TUPLE;
            pattern->span.file_id = lparen.span.file_id;
            pattern->span.start = lparen.span.start;
            pattern->span.end = rparen.span.end;
            pattern->tuple_elements = first;
            return pattern;
        }

        if (!expect(parser, CC_TOKEN_RPAREN,
                    "expected `)` after pattern", &rparen))
            return NULL;

        CcPattern *pattern = allocate(parser, sizeof(*pattern));

        if (pattern == NULL) return NULL;

        pattern->kind = CC_AST_PATTERN_PARENTHESIZED;
        pattern->span.file_id = lparen.span.file_id;
        pattern->span.start = lparen.span.start;
        pattern->span.end = rparen.span.end;
        pattern->parenthesized_inner = inner;
        return pattern;
    }

    report_error(parser, "expected variable name");
    return NULL;
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

    CcPattern *pattern = parse_pattern(parser);

    if (parser->failed || pattern == NULL) return NULL;

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
    if (pattern->kind == CC_AST_PATTERN_NAME)
        statement->let.name = pattern->name;
    else {
        statement->let.name.file_id = CC_FILE_ID_INVALID;
        statement->let.name.start = 0;
        statement->let.name.end = 0;
    }
    statement->let.pattern = pattern;
    statement->let.type = type;
    statement->let.initializer = initializer;

    return statement;
}

static CcBlock *parse_block(CcParser *parser);
static CcIf *parse_if(CcParser *parser);
static CcWhile *parse_while(CcParser *parser);
static CcLoop *parse_loop(CcParser *parser);
static CcFor *parse_for(CcParser *parser);
static CcMatch *parse_match(CcParser *parser);
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

        if (parser->current.kind == CC_TOKEN_KW_MATCH) {
            CcMatch *match = parse_match(parser);
            if (parser->failed || match == NULL) break;

            CcStatement *statement = allocate(parser, sizeof(*statement));
            if (statement == NULL) break;
            statement->kind = CC_AST_STATEMENT_MATCH;
            statement->span = match->span;
            statement->next = NULL;
            statement->match_statement = match;
            *link = statement;
            link = &statement->next;
            continue;
        }

        if (parser->current.kind == CC_TOKEN_KW_UNSAFE) {
            CcToken keyword = parser->current;
            advance(parser);
            CcBlock *body = parse_block(parser);
            if (parser->failed || body == NULL) break;
            CcStatement *statement = allocate(parser, sizeof(*statement));
            if (statement == NULL) break;
            statement->kind = CC_AST_STATEMENT_UNSAFE;
            statement->span = keyword.span;
            statement->span.end = body->span.end;
            statement->unsafe_block = body;
            statement->next = NULL;
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

static CcMatch *parse_match(CcParser *parser)
{
    CcToken keyword = parser->current;
    advance(parser);
    bool saved = parser->before_block;
    parser->before_block = true;
    CcExpression *scrutinee = parse_expression(parser);
    parser->before_block = saved;
    if (parser->failed || scrutinee == NULL) return NULL;

    if (!expect(parser, CC_TOKEN_LBRACE, "expected `{` after match value",
                NULL)) return NULL;
    CcMatchArm *first = NULL;
    CcMatchArm **link = &first;
    while (!parser->failed && parser->current.kind != CC_TOKEN_RBRACE) {
        if (parser->current.kind != CC_TOKEN_KW_SOME &&
            parser->current.kind != CC_TOKEN_KW_NONE) {
            report_error(parser, "expected `some` or `none` match arm");
            return NULL;
        }
        CcToken variant = parser->current;
        bool is_some = variant.kind == CC_TOKEN_KW_SOME;
        advance(parser);
        CcPattern *payload = NULL;
        if (is_some) {
            if (!expect(parser, CC_TOKEN_LPAREN,
                        "expected `(` after `some` match arm", NULL))
                return NULL;
            payload = parse_pattern(parser);
            if (parser->failed || payload == NULL) return NULL;
            if (!expect(parser, CC_TOKEN_RPAREN,
                        "expected `)` after `some` match pattern", NULL))
                return NULL;
        }
        if (!expect(parser, CC_TOKEN_FAT_ARROW,
                    "expected `=>` after match pattern", NULL)) return NULL;
        CcBlock *body = parse_block(parser);
        if (parser->failed || body == NULL) return NULL;

        CcMatchArm *arm = allocate(parser, sizeof(*arm));
        if (arm == NULL) return NULL;
        arm->span = variant.span;
        arm->span.end = body->span.end;
        arm->is_some = is_some;
        arm->payload = payload;
        arm->body = body;
        arm->next = NULL;
        *link = arm;
        link = &arm->next;
        if (parser->current.kind == CC_TOKEN_RBRACE) break;
        if (!expect(parser, CC_TOKEN_COMMA,
                    "expected `,` between match arms", NULL)) return NULL;
    }
    CcToken end;
    if (!expect(parser, CC_TOKEN_RBRACE, "expected `}` after match arms",
                &end)) return NULL;

    CcMatch *match = allocate(parser, sizeof(*match));
    if (match == NULL) return NULL;
    match->span = keyword.span;
    match->span.end = end.span.end;
    match->scrutinee = scrutinee;
    match->arms = first;
    return match;
}

static CcIf *parse_if(CcParser *parser)
{
    CcToken keyword = parser->current;

    advance(parser);

    bool saved = parser->before_block;
    parser->before_block = true;
    CcExpression *condition = parse_expression(parser);
    parser->before_block = saved;

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

    bool saved = parser->before_block;
    parser->before_block = true;
    CcExpression *condition = parse_expression(parser);
    parser->before_block = saved;

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

    bool saved = parser->before_block;
    parser->before_block = true;
    CcExpression *range = parse_expression(parser);
    parser->before_block = saved;

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
    function->struct_decl = NULL;

    return function;
}

static CcFunctionDeclaration *parse_struct(CcParser *parser)
{
    uint32_t start = parser->current.span.start;
    bool is_public = accept(parser, CC_TOKEN_KW_PUB, NULL);
    CcToken keyword;
    if (!expect(parser, CC_TOKEN_KW_STRUCT,
                "expected struct declaration", &keyword))
        return NULL;
    CcToken name;
    if (!expect(parser, CC_TOKEN_IDENTIFIER, "expected struct name", &name))
        return NULL;
    if (!expect(parser, CC_TOKEN_LBRACE,
                "expected `{` after struct name", NULL))
        return NULL;
    CcStructField *fields = NULL;
    CcStructField **link = &fields;
    while (parser->current.kind != CC_TOKEN_RBRACE) {
        bool public_field = accept(parser, CC_TOKEN_KW_PUB, NULL);
        CcToken field_name;
        if (!expect(parser, CC_TOKEN_IDENTIFIER,
                    "expected field name", &field_name))
            return NULL;
        if (!expect(parser, CC_TOKEN_COLON,
                    "expected `:` after field name", NULL))
            return NULL;
        CcType *type = parse_type(parser);
        if (parser->failed || type == NULL) return NULL;
        CcStructField *field = allocate(parser, sizeof(*field));
        if (field == NULL) return NULL;
        field->name = field_name.span;
        field->type = type;
        field->is_public = public_field;
        field->next = NULL;
        *link = field;
        link = &field->next;
        if (!accept(parser, CC_TOKEN_COMMA, NULL)) break;
        if (parser->current.kind == CC_TOKEN_RBRACE) break;
    }
    CcToken ending;
    if (!expect(parser, CC_TOKEN_RBRACE,
                "expected `}` after struct declaration", &ending))
        return NULL;
    CcStructDeclaration *decl = allocate(parser, sizeof(*decl));
    CcFunctionDeclaration *item = allocate(parser, sizeof(*item));
    if (decl == NULL || item == NULL) return NULL;
    decl->span = keyword.span;
    decl->span.start = start;
    decl->span.end = ending.span.end;
    decl->name = name.span;
    decl->is_public = is_public;
    decl->fields = fields;
    item->span = decl->span;
    item->name = decl->name;
    item->is_public = is_public;
    item->parameters = NULL;
    item->return_type = NULL;
    item->body = NULL;
    item->struct_decl = decl;
    item->next = NULL;
    return item;
}

static bool parse_module_path(CcParser *parser, CcSourceSpan *path,
                              CcSourceSpan *last)
{
    CcToken name;
    if (!expect(parser, CC_TOKEN_IDENTIFIER,
                "expected module path", &name))
        return false;
    *path = name.span;
    *last = name.span;
    while (accept(parser, CC_TOKEN_DOT, NULL)) {
        if (!expect(parser, CC_TOKEN_IDENTIFIER,
                    "expected module path component", &name))
            return false;
        path->end = name.span.end;
        *last = name.span;
    }
    return true;
}

void cc_parser_init(CcParser *parser, CcLexer *lexer, CcArena *arena)
{
    if (parser == NULL) return;

    parser->lexer = lexer;
    parser->arena = arena;
    parser->failed = false;
    parser->error = NULL;
    parser->before_block = false;
    parser->has_module = false;
    parser->imports = NULL;
    parser->module_path = (CcSourceSpan){ CC_FILE_ID_INVALID, 0, 0 };
    parser->header_end = 0;
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
    if (accept(parser, CC_TOKEN_KW_MODULE, NULL)) {
        CcSourceSpan last;
        if (!parse_module_path(parser, &parser->module_path, &last))
            return NULL;
        CcToken semicolon;
        if (!expect(parser, CC_TOKEN_SEMICOLON,
                    "expected `;` after module path", &semicolon))
            return NULL;
        parser->has_module = true;
        parser->header_end = semicolon.span.end;
    }
    CcImportDeclaration **import_link = &parser->imports;
    while (!parser->failed &&
           parser->current.kind == CC_TOKEN_KW_IMPORT) {
        if (!parser->has_module) {
            report_error(parser, "import requires module declaration");
            return NULL;
        }
        advance(parser);
        CcSourceSpan path, last;
        if (!parse_module_path(parser, &path, &last)) return NULL;
        CcSourceSpan alias = last;
        if (accept(parser, CC_TOKEN_KW_AS, NULL)) {
            CcToken name;
            if (!expect(parser, CC_TOKEN_IDENTIFIER,
                        "expected import alias", &name))
                return NULL;
            alias = name.span;
        }
        CcToken semicolon;
        if (!expect(parser, CC_TOKEN_SEMICOLON,
                    "expected `;` after import", &semicolon))
            return NULL;
        CcImportDeclaration *node = allocate(parser, sizeof(*node));
        if (node == NULL) return NULL;
        node->path = path;
        node->alias = alias;
        node->next = NULL;
        *import_link = node;
        import_link = &node->next;
        parser->header_end = semicolon.span.end;
    }

    while (!parser->failed && parser->current.kind != CC_TOKEN_EOF) {
        if (parser->current.kind == CC_TOKEN_KW_IMPORT ||
            parser->current.kind == CC_TOKEN_KW_MODULE) {
            report_error(parser, "module and imports must precede declarations");
            break;
        }
        if (parser->current.kind != CC_TOKEN_KW_FN &&
            parser->current.kind != CC_TOKEN_KW_STRUCT &&
            parser->current.kind != CC_TOKEN_KW_PUB) {
            report_error(parser, "expected function or struct declaration");
            break;
        }

        CcTokenKind kind = parser->current.kind;
        if (kind == CC_TOKEN_KW_PUB) {
            CcLexer peek = *parser->lexer;
            kind = cc_lexer_next(&peek).kind;
        }
        CcFunctionDeclaration *function =
            kind == CC_TOKEN_KW_STRUCT ? parse_struct(parser)
                                       : parse_function(parser);

        if (parser->failed || function == NULL) break;

        *link = function;
        link = &function->next;
    }

    return first;
}
