#ifndef CCARET_AST_AST_H
#define CCARET_AST_AST_H

#include "ast/arena.h"
#include "lexer/lexer.h"

typedef struct CcType CcType;
typedef struct CcTupleTypeElement CcTupleTypeElement;
typedef struct CcParameter CcParameter;
typedef struct CcCallArgument CcCallArgument;
typedef struct CcExpression CcExpression;
typedef struct CcTupleExpressionElement CcTupleExpressionElement;
typedef struct CcPattern CcPattern;
typedef struct CcTuplePatternElement CcTuplePatternElement;
typedef struct CcStatement CcStatement;
typedef struct CcBlock CcBlock;
typedef struct CcIf CcIf;
typedef struct CcWhile CcWhile;
typedef struct CcLoop CcLoop;
typedef struct CcFor CcFor;
typedef struct CcFunctionDeclaration CcFunctionDeclaration;

typedef enum {
    CC_AST_TYPE_NAMED,
    CC_AST_TYPE_OPTIONAL,
    CC_AST_TYPE_POINTER,
    CC_AST_TYPE_SLICE,
    CC_AST_TYPE_ARRAY,
    CC_AST_TYPE_PARENTHESIZED,
    CC_AST_TYPE_TUPLE
} CcTypeKind;

typedef enum {
    CC_AST_EXPRESSION_INTEGER_LITERAL,
    CC_AST_EXPRESSION_BOOLEAN_LITERAL,
    CC_AST_EXPRESSION_STRING_LITERAL,
    CC_AST_EXPRESSION_C_STRING_LITERAL,
    CC_AST_EXPRESSION_BYTE_LITERAL,
    CC_AST_EXPRESSION_NONE_LITERAL,
    CC_AST_EXPRESSION_SOME,
    CC_AST_EXPRESSION_IDENTIFIER,
    CC_AST_EXPRESSION_PARENTHESIZED,
    CC_AST_EXPRESSION_TUPLE,
    CC_AST_EXPRESSION_UNARY,
    CC_AST_EXPRESSION_BINARY,
    CC_AST_EXPRESSION_CALL,
    CC_AST_EXPRESSION_MEMBER,
    CC_AST_EXPRESSION_INDEX,
    CC_AST_EXPRESSION_TUPLE_MEMBER
} CcExpressionKind;

typedef enum {
    CC_AST_PATTERN_NAME,
    CC_AST_PATTERN_PARENTHESIZED,
    CC_AST_PATTERN_TUPLE
} CcPatternKind;

typedef enum {
    CC_AST_STATEMENT_RETURN,
    CC_AST_STATEMENT_LET,
    CC_AST_STATEMENT_EXPRESSION,
    CC_AST_STATEMENT_ASSIGNMENT,
    CC_AST_STATEMENT_IF,
    CC_AST_STATEMENT_WHILE,
    CC_AST_STATEMENT_BREAK,
    CC_AST_STATEMENT_CONTINUE,
    CC_AST_STATEMENT_LOOP,
    CC_AST_STATEMENT_FOR
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
    CC_AST_BINARY_OPERATOR_OR_OR,
    CC_AST_BINARY_OPERATOR_RANGE,
    CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE
} CcBinaryOperator;

typedef enum {
    CC_AST_ASSIGNMENT_ASSIGN,
    CC_AST_ASSIGNMENT_PLUS,
    CC_AST_ASSIGNMENT_MINUS,
    CC_AST_ASSIGNMENT_STAR,
    CC_AST_ASSIGNMENT_SLASH,
    CC_AST_ASSIGNMENT_PERCENT,
    CC_AST_ASSIGNMENT_AMP,
    CC_AST_ASSIGNMENT_PIPE,
    CC_AST_ASSIGNMENT_CARET,
    CC_AST_ASSIGNMENT_SHIFT_LEFT,
    CC_AST_ASSIGNMENT_SHIFT_RIGHT
} CcAssignmentOperator;

struct CcType {
    CcTypeKind kind;
    CcSourceSpan span;
    bool pointer_pointee_const;
    bool slice_element_const;
    union {
        CcType *optional_inner;
        CcType *pointer_inner;
        CcType *slice_inner;
        struct {
            CcExpression *length;
            CcType *element;
        } array;
        CcType *parenthesized_inner;
        CcTupleTypeElement *tuple_elements;
    };
};

struct CcTupleTypeElement {
    CcType *type;
    CcTupleTypeElement *next;
};

struct CcParameter {
    CcSourceSpan span;
    CcSourceSpan name;
    CcType *type;
    CcParameter *next;
};

struct CcCallArgument {
    CcExpression *value;
    CcCallArgument *next;
};

struct CcTupleExpressionElement {
    CcExpression *value;
    CcTupleExpressionElement *next;
};

struct CcTuplePatternElement {
    CcPattern *pattern;
    CcTuplePatternElement *next;
};

struct CcPattern {
    CcPatternKind kind;
    CcSourceSpan span;
    union {
        CcSourceSpan name;
        CcPattern *parenthesized_inner;
        CcTuplePatternElement *tuple_elements;
    };
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
        bool boolean_literal;
        CcExpression *some_value;
        CcSourceSpan identifier_name;
        CcExpression *parenthesized_inner;
        CcTupleExpressionElement *tuple_elements;
        struct {
            CcExpression *operand;
            CcUnaryOperator operator;
        } unary;
        struct {
            CcExpression *left;
            CcExpression *right;
            CcBinaryOperator operator;
        } binary;
        struct {
            CcExpression *callee;
            CcCallArgument *arguments;
        } call;
        struct {
            CcExpression *operand;
            CcSourceSpan name;
        } member;
        struct {
            CcExpression *operand;
            CcExpression *index;
        } index;
        struct {
            CcExpression *operand;
            CcSourceSpan index;
            CcIntegerLiteral index_value;
        } tuple_member;
    };
};

struct CcStatement {
    CcStatementKind kind;
    CcSourceSpan span;
    CcStatement *next;
    union {
        CcExpression *return_expression;
        struct {
            CcSourceSpan name;    /* Only valid for a simple name binding. */
            CcPattern *pattern;
            bool mutable;      /* `var` versus `let`. */
            CcType *type;      /* NULL when the type is inferred. */
            CcExpression *initializer;
        } let;
        CcExpression *expression;
        struct {
            CcAssignmentOperator operator;
            CcExpression *target;
            CcExpression *value;
        } assignment;
        CcIf *if_statement;
        CcWhile *while_statement;
        CcLoop *loop_statement;
        CcFor *for_statement;
    };
};

struct CcBlock {
    CcSourceSpan span;
    CcStatement *first;
};

struct CcIf {
    CcSourceSpan span;
    CcExpression *condition;
    CcBlock *then_block;
    CcIf *else_if;         /* Selected by an `else if` chain. */
    CcBlock *else_block;   /* Selected by a plain `else`. */
};

struct CcWhile {
    CcSourceSpan span;
    CcExpression *condition;
    CcBlock *body;
};

struct CcLoop {
    CcSourceSpan span;
    CcBlock *body;
};

struct CcFor {
    CcSourceSpan span;
    CcSourceSpan name;
    CcExpression *range;
    CcBlock *body;
};

struct CcFunctionDeclaration {
    CcSourceSpan span;
    CcSourceSpan name;
    bool is_public;
    CcParameter *parameters;
    CcType *return_type;
    CcBlock *body;
    CcFunctionDeclaration *next;
};

#endif
