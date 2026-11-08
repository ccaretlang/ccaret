#ifndef CCARET_AST_AST_H
#define CCARET_AST_AST_H

#include "ast/arena.h"
#include "lexer/lexer.h"

typedef struct CcType CcType;
typedef struct CcParameter CcParameter;
typedef struct CcExpression CcExpression;
typedef struct CcStatement CcStatement;
typedef struct CcBlock CcBlock;
typedef struct CcFunctionDeclaration CcFunctionDeclaration;

typedef enum {
    CC_AST_TYPE_NAMED
} CcTypeKind;

typedef enum {
    CC_AST_EXPRESSION_INTEGER_LITERAL,
    CC_AST_EXPRESSION_IDENTIFIER,
    CC_AST_EXPRESSION_PARENTHESIZED,
    CC_AST_EXPRESSION_UNARY,
    CC_AST_EXPRESSION_BINARY
} CcExpressionKind;

typedef enum {
    CC_AST_STATEMENT_RETURN
} CcStatementKind;

typedef enum {
    CC_AST_UNARY_OPERATOR_NOT,
    CC_AST_UNARY_OPERATOR_TILDE,
    CC_AST_UNARY_OPERATOR_MINUS,
    CC_AST_UNARY_OPERATOR_AMP,
    CC_AST_UNARY_OPERATOR_STAR
} CcUnaryOperator;

typedef enum {
    CC_AST_BINARY_OPERATOR_PLUS,
    CC_AST_BINARY_OPERATOR_MINUS,
    CC_AST_BINARY_OPERATOR_STAR,
    CC_AST_BINARY_OPERATOR_SLASH,
    CC_AST_BINARY_OPERATOR_PERCENT,
    CC_AST_BINARY_OPERATOR_PLUS_PERCENT,
    CC_AST_BINARY_OPERATOR_MINUS_PERCENT,
    CC_AST_BINARY_OPERATOR_STAR_PERCENT,
    CC_AST_BINARY_OPERATOR_SHIFT_LEFT,
    CC_AST_BINARY_OPERATOR_SHIFT_RIGHT,
    CC_AST_BINARY_OPERATOR_LT,
    CC_AST_BINARY_OPERATOR_LE,
    CC_AST_BINARY_OPERATOR_GT,
    CC_AST_BINARY_OPERATOR_GE,
    CC_AST_BINARY_OPERATOR_EQ,
    CC_AST_BINARY_OPERATOR_NE,
    CC_AST_BINARY_OPERATOR_AMP,
    CC_AST_BINARY_OPERATOR_CARET,
    CC_AST_BINARY_OPERATOR_PIPE,
    CC_AST_BINARY_OPERATOR_AND_AND,
    CC_AST_BINARY_OPERATOR_OR_OR
} CcBinaryOperator;

struct CcType {
    CcTypeKind kind;
    CcSourceSpan span;
};

struct CcParameter {
    CcSourceSpan span;
    CcSourceSpan name;
    CcType *type;
    CcParameter *next;
};

typedef struct {
    unsigned long long value;
    bool overflow;
} CcIntegerLiteral;

struct CcExpression {
    CcExpressionKind kind;
    CcSourceSpan span;
    union {
        CcIntegerLiteral integer_literal;
        CcSourceSpan identifier_name;
        CcExpression *parenthesized_inner;
        struct {
            CcExpression *operand;
            CcUnaryOperator operator;
        } unary;
        struct {
            CcExpression *left;
            CcExpression *right;
            CcBinaryOperator operator;
        } binary;
    };
};

struct CcStatement {
    CcStatementKind kind;
    CcSourceSpan span;
    CcStatement *next;
    union {
        CcExpression *return_expression;
    };
};

struct CcBlock {
    CcSourceSpan span;
    CcStatement *first;
};

struct CcFunctionDeclaration {
    CcSourceSpan span;
    CcSourceSpan name;
    CcParameter *parameters;
    CcType *return_type;
    CcBlock *body;
    CcFunctionDeclaration *next;
};

#endif
