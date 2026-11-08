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
    CC_AST_EXPRESSION_IDENTIFIER
} CcExpressionKind;

typedef enum {
    CC_AST_STATEMENT_RETURN
} CcStatementKind;

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
