#include "semantic/semantic.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    SEM_VOID,
    SEM_BOOL,
    SEM_INTEGER,
    SEM_FLOAT,
    SEM_OPTIONAL,
    SEM_POINTER,
    SEM_SLICE,
    SEM_ARRAY,
    SEM_TUPLE,
    SEM_UNTYPED_INTEGER,
    SEM_RANGE
} SemKind;

typedef enum {
    PRIM_VOID,
    PRIM_BOOL,
    PRIM_I8,
    PRIM_I16,
    PRIM_I32,
    PRIM_I64,
    PRIM_ISIZE,
    PRIM_U8,
    PRIM_U16,
    PRIM_U32,
    PRIM_U64,
    PRIM_USIZE,
    PRIM_F32,
    PRIM_F64
} Primitive;

typedef struct SemType SemType;
typedef struct SemElement SemElement;
typedef struct SemVariable SemVariable;
typedef struct SemFunction SemFunction;
typedef struct SemParameter SemParameter;

struct SemElement {
    SemType *type;
    SemElement *next;
};

struct SemType {
    SemKind kind;
    Primitive primitive;
    SemType *inner;
    bool is_const;
    uint64_t length;
    SemElement *elements;
    size_t count;
};

struct SemVariable {
    CcSourceSpan name;
    SemType *type;
    bool mutable;
    unsigned int depth;
    SemVariable *next;
};

struct SemParameter {
    CcSourceSpan name;
    SemType *type;
    SemParameter *next;
};

struct SemFunction {
    const CcFunctionDeclaration *decl;
    SemType *result;
    SemParameter *parameters;
    SemFunction *next;
};

typedef struct {
    const CcSourceFile *file;
    CcArena *arena;
    CcSemanticError *error;
    SemFunction *functions;
    SemFunction *current_function;
    SemVariable *variables;
    unsigned int depth;
    unsigned int loop_depth;
    bool failed;
} SemContext;

typedef struct {
    const char *name;
    Primitive id;
    SemKind kind;
} PrimitiveInfo;

static const PrimitiveInfo primitives[] = {
    { "void", PRIM_VOID, SEM_VOID },
    { "bool", PRIM_BOOL, SEM_BOOL },
    { "i8", PRIM_I8, SEM_INTEGER },
    { "i16", PRIM_I16, SEM_INTEGER },
    { "i32", PRIM_I32, SEM_INTEGER },
    { "i64", PRIM_I64, SEM_INTEGER },
    { "isize", PRIM_ISIZE, SEM_INTEGER },
    { "u8", PRIM_U8, SEM_INTEGER },
    { "u16", PRIM_U16, SEM_INTEGER },
    { "u32", PRIM_U32, SEM_INTEGER },
    { "u64", PRIM_U64, SEM_INTEGER },
    { "usize", PRIM_USIZE, SEM_INTEGER },
    { "f32", PRIM_F32, SEM_FLOAT },
    { "f64", PRIM_F64, SEM_FLOAT }
};

static void fail(SemContext *ctx, CcSourceSpan span, const char *message)
{
    if (ctx->failed) return;

    ctx->failed = true;
    ctx->error->span = span;
    ctx->error->message = message;
}

static void *arena_alloc(SemContext *ctx, size_t size, CcSourceSpan span)
{
    void *value = cc_arena_alloc(ctx->arena, size);

    if (value == NULL) fail(ctx, span, "out of memory");
    return value;
}

static bool same_name(SemContext *ctx, CcSourceSpan a, CcSourceSpan b)
{
    if (a.file_id != b.file_id || a.end < a.start || b.end < b.start)
        return false;
    size_t len = a.end - a.start;

    return len == b.end - b.start && a.end <= ctx->file->length &&
           b.end <= ctx->file->length &&
           memcmp(ctx->file->bytes + a.start, ctx->file->bytes + b.start,
                  len) == 0;
}

static bool name_is(SemContext *ctx, CcSourceSpan span, const char *name)
{
    size_t len = strlen(name);

    return span.end >= span.start && span.end <= ctx->file->length &&
           span.end - span.start == len &&
           memcmp(ctx->file->bytes + span.start, name, len) == 0;
}

static SemType *new_type(SemContext *ctx, SemKind kind, CcSourceSpan span)
{
    SemType *type = arena_alloc(ctx, sizeof(*type), span);

    if (type == NULL) return NULL;

    memset(type, 0, sizeof(*type));
    type->kind = kind;
    return type;
}

static SemType *builtin(SemContext *ctx, Primitive id, CcSourceSpan span)
{
    for (size_t i = 0; i < sizeof(primitives) / sizeof(primitives[0]); i++) {
        if (primitives[i].id != id) continue;

        SemType *type = new_type(ctx, primitives[i].kind, span);

        if (type != NULL) type->primitive = id;
        return type;
    }

    fail(ctx, span, "internal: unknown primitive type");
    return NULL;
}

static unsigned int int_bits(Primitive primitive)
{
    switch (primitive) {
    case PRIM_I8:
    case PRIM_U8: return 8;
    case PRIM_I16:
    case PRIM_U16: return 16;
    case PRIM_I32:
    case PRIM_U32: return 32;
    case PRIM_I64:
    case PRIM_U64:
        return 64;
    case PRIM_ISIZE:
    case PRIM_USIZE: return sizeof(size_t) * CHAR_BIT;
    default: return 0;
    }
}

static bool signed_integer(Primitive p)
{
    return p == PRIM_I8 || p == PRIM_I16 || p == PRIM_I32 ||
           p == PRIM_I64 || p == PRIM_ISIZE;
}

static bool fits_integer(uint64_t value, Primitive primitive, bool negative)
{
    unsigned int bits = int_bits(primitive);

    if (bits == 0 || (negative && !signed_integer(primitive))) return false;

    if (signed_integer(primitive)) {
        uint64_t max = (UINT64_C(1) << (bits - 1)) - 1;

        return value <= max + (negative ? 1U : 0U);
    }

    if (bits == 64) return true;
    return value <= (UINT64_C(1) << bits) - 1;
}

static bool same_type(const SemType *a, const SemType *b)
{
    if (a == NULL || b == NULL || a->kind != b->kind) return false;

    switch (a->kind) {
    case SEM_VOID:
    case SEM_BOOL: return true;
    case SEM_INTEGER:
    case SEM_FLOAT: return a->primitive == b->primitive;
    case SEM_OPTIONAL:
    case SEM_RANGE: return same_type(a->inner, b->inner);
    case SEM_POINTER:
    case SEM_SLICE:
        return a->is_const == b->is_const && same_type(a->inner, b->inner);
    case SEM_ARRAY:
        return a->length == b->length && same_type(a->inner, b->inner);
    case SEM_TUPLE: {
        if (a->count != b->count) return false;

        SemElement *left = a->elements;
        SemElement *right = b->elements;

        while (left != NULL && right != NULL) {
            if (!same_type(left->type, right->type)) return false;
            left = left->next;
            right = right->next;
        }

        return left == NULL && right == NULL;
    }
    case SEM_UNTYPED_INTEGER: return true;
    }

    return false;
}

static SemType *finalize_type(SemContext *ctx, SemType *type,
                              CcSourceSpan span)
{
    if (type == NULL) return NULL;

    if (type->kind == SEM_UNTYPED_INTEGER) {
        if (!fits_integer(type->length, PRIM_I32, false)) {
            fail(ctx, span, "integer literal does not fit i32");
            return NULL;
        }

        return builtin(ctx, PRIM_I32, span);
    }

    if (type->kind == SEM_TUPLE) {
        for (SemElement *element = type->elements; element != NULL;
             element = element->next) {
            element->type = finalize_type(ctx, element->type, span);

            if (ctx->failed) return NULL;
        }
    } else if (type->kind == SEM_OPTIONAL) {
        type->inner = finalize_type(ctx, type->inner, span);

        if (ctx->failed) return NULL;
    }

    return type;
}

static bool const_integer(const CcExpression *expr, uint64_t *out)
{
    switch (expr->kind) {
    case CC_AST_EXPRESSION_INTEGER_LITERAL:
        if (expr->integer_literal.overflow) return false;
        *out = expr->integer_literal.value;
        return true;
    case CC_AST_EXPRESSION_PARENTHESIZED:
        return const_integer(expr->parenthesized_inner, out);
    case CC_AST_EXPRESSION_BINARY: {
        uint64_t left, right;

        if (!const_integer(expr->binary.left, &left) ||
            !const_integer(expr->binary.right, &right))
            return false;

        switch (expr->binary.operator) {
        case CC_AST_BINARY_OPERATOR_PLUS:
            if (UINT64_MAX - left < right) return false;
            *out = left + right;
            return true;
        case CC_AST_BINARY_OPERATOR_MINUS:
            if (left < right) return false;
            *out = left - right;
            return true;
        case CC_AST_BINARY_OPERATOR_STAR:
            if (right != 0 && left > UINT64_MAX / right) return false;
            *out = left * right;
            return true;
        case CC_AST_BINARY_OPERATOR_SLASH:
            if (right == 0) return false;
            *out = left / right;
            return true;
        case CC_AST_BINARY_OPERATOR_PERCENT:
            if (right == 0) return false;
            *out = left % right;
            return true;
        default: return false;
        }
    }
    default: return false;
    }
}

static SemType *resolve_type(SemContext *ctx, const CcType *node)
{
    if (node == NULL) return NULL;

    switch (node->kind) {
    case CC_AST_TYPE_NAMED:
        for (size_t i = 0; i < sizeof(primitives) / sizeof(primitives[0]);
             i++) {
            if (name_is(ctx, node->span, primitives[i].name))
                return builtin(ctx, primitives[i].id, node->span);
        }

        fail(ctx, node->span, "unknown type");
        return NULL;
    case CC_AST_TYPE_PARENTHESIZED:
        return resolve_type(ctx, node->parenthesized_inner);
    case CC_AST_TYPE_OPTIONAL:
    case CC_AST_TYPE_POINTER:
    case CC_AST_TYPE_SLICE:
    case CC_AST_TYPE_ARRAY: {
        const CcType *inner = node->kind == CC_AST_TYPE_OPTIONAL
            ? node->optional_inner
            : node->kind == CC_AST_TYPE_POINTER
                ? node->pointer_inner
                : node->kind == CC_AST_TYPE_SLICE
                    ? node->slice_inner : node->array.element;
        SemType *element = resolve_type(ctx, inner);

        if (ctx->failed) return NULL;

        SemKind kind = node->kind == CC_AST_TYPE_OPTIONAL ? SEM_OPTIONAL
            : node->kind == CC_AST_TYPE_POINTER ? SEM_POINTER
            : node->kind == CC_AST_TYPE_SLICE ? SEM_SLICE : SEM_ARRAY;
        SemType *type = new_type(ctx, kind, node->span);

        if (type == NULL) return NULL;
        type->inner = element;
        type->is_const = node->kind == CC_AST_TYPE_POINTER
            ? node->pointer_pointee_const : node->slice_element_const;

        if (kind == SEM_ARRAY &&
            !const_integer(node->array.length, &type->length))
            fail(ctx, node->array.length->span,
                 "array length must be a nonnegative constant integer");
        return ctx->failed ? NULL : type;
    }
    case CC_AST_TYPE_TUPLE: {
        SemType *tuple = new_type(ctx, SEM_TUPLE, node->span);

        if (tuple == NULL) return NULL;

        SemElement **link = &tuple->elements;

        for (CcTupleTypeElement *part = node->tuple_elements; part != NULL;
             part = part->next) {
            SemType *member = resolve_type(ctx, part->type);

            if (ctx->failed) return NULL;

            SemElement *element =
                arena_alloc(ctx, sizeof(*element), part->type->span);

            if (element == NULL) return NULL;
            element->type = member;
            element->next = NULL;
            *link = element;
            link = &element->next;
            tuple->count++;
        }

        return tuple;
    }
    }

    fail(ctx, node->span, "unsupported type syntax");
    return NULL;
}

static SemVariable *find_variable(SemContext *ctx, CcSourceSpan name)
{
    for (SemVariable *var = ctx->variables; var != NULL; var = var->next)
        if (same_name(ctx, name, var->name)) return var;

    return NULL;
}

static SemFunction *find_function(SemContext *ctx, CcSourceSpan name)
{
    for (SemFunction *function = ctx->functions; function != NULL;
         function = function->next)
        if (same_name(ctx, name, function->decl->name)) return function;

    return NULL;
}

static bool bind_variable(SemContext *ctx, CcSourceSpan name, SemType *type,
                          bool mutable)
{
    SemVariable *previous = find_variable(ctx, name);

    if (previous != NULL && previous->depth == ctx->depth) {
        fail(ctx, name, "duplicate binding in scope");
        return false;
    }

    SemVariable *variable = arena_alloc(ctx, sizeof(*variable), name);

    if (variable == NULL) return false;

    variable->name = name;
    variable->type = type;
    variable->mutable = mutable;
    variable->depth = ctx->depth;
    variable->next = ctx->variables;
    ctx->variables = variable;
    return true;
}

static bool bind_pattern(SemContext *ctx, const CcPattern *pattern,
                         SemType *type, bool mutable)
{
    if (ctx->failed) return false;

    switch (pattern->kind) {
    case CC_AST_PATTERN_NAME:
        return bind_variable(ctx, pattern->name, type, mutable);
    case CC_AST_PATTERN_PARENTHESIZED:
        return bind_pattern(ctx, pattern->parenthesized_inner, type, mutable);
    case CC_AST_PATTERN_TUPLE: {
        if (type->kind != SEM_TUPLE) {
            fail(ctx, pattern->span, "tuple pattern requires a tuple value");
            return false;
        }

        CcTuplePatternElement *item = pattern->tuple_elements;
        SemElement *element = type->elements;

        while (item != NULL && element != NULL) {
            if (!bind_pattern(ctx, item->pattern, element->type, mutable))
                return false;
            item = item->next;
            element = element->next;
        }

        if (item != NULL || element != NULL) {
            fail(ctx, pattern->span, "tuple destructuring arity mismatch");
            return false;
        }

        return true;
    }
    }

    fail(ctx, pattern->span, "unsupported binding pattern");
    return false;
}

static SemType *coerce(SemContext *ctx, SemType *actual, SemType *expected,
                       CcSourceSpan span)
{
    if (ctx->failed || actual == NULL) return NULL;
    if (expected == NULL) return actual;

    if (actual->kind == SEM_UNTYPED_INTEGER &&
        expected->kind == SEM_INTEGER) {
        if (!fits_integer(actual->length, expected->primitive, false)) {
            fail(ctx, span, "integer literal out of range for target type");
            return NULL;
        }

        return expected;
    }

    if (!same_type(actual, expected)) {
        fail(ctx, span, "type mismatch");
        return NULL;
    }

    return expected;
}

static SemType *check_expr(SemContext *ctx, const CcExpression *expr,
                           SemType *expected);

static SemType *check_binary(SemContext *ctx, const CcExpression *expr,
                             SemType *expected)
{
    CcBinaryOperator op = expr->binary.operator;

    if (op == CC_AST_BINARY_OPERATOR_AND_AND ||
        op == CC_AST_BINARY_OPERATOR_OR_OR) {
        SemType *boolean = builtin(ctx, PRIM_BOOL, expr->span);

        if (boolean == NULL ||
            check_expr(ctx, expr->binary.left, boolean) == NULL ||
            check_expr(ctx, expr->binary.right, boolean) == NULL)
            return NULL;
        return coerce(ctx, boolean, expected, expr->span);
    }

    bool comparison = op == CC_AST_BINARY_OPERATOR_EQ ||
        op == CC_AST_BINARY_OPERATOR_NE ||
        op == CC_AST_BINARY_OPERATOR_LT ||
        op == CC_AST_BINARY_OPERATOR_LE ||
        op == CC_AST_BINARY_OPERATOR_GT ||
        op == CC_AST_BINARY_OPERATOR_GE;
    bool equality = op == CC_AST_BINARY_OPERATOR_EQ ||
        op == CC_AST_BINARY_OPERATOR_NE;
    bool range = op == CC_AST_BINARY_OPERATOR_RANGE ||
        op == CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE;
    bool arithmetic = op == CC_AST_BINARY_OPERATOR_PLUS ||
        op == CC_AST_BINARY_OPERATOR_MINUS ||
        op == CC_AST_BINARY_OPERATOR_STAR ||
        op == CC_AST_BINARY_OPERATOR_SLASH ||
        op == CC_AST_BINARY_OPERATOR_PERCENT ||
        op == CC_AST_BINARY_OPERATOR_PLUS_PERCENT ||
        op == CC_AST_BINARY_OPERATOR_MINUS_PERCENT ||
        op == CC_AST_BINARY_OPERATOR_STAR_PERCENT;
    bool integral = range ||
        op == CC_AST_BINARY_OPERATOR_AMP ||
        op == CC_AST_BINARY_OPERATOR_CARET ||
        op == CC_AST_BINARY_OPERATOR_PIPE ||
        op == CC_AST_BINARY_OPERATOR_SHIFT_LEFT ||
        op == CC_AST_BINARY_OPERATOR_SHIFT_RIGHT ||
        op == CC_AST_BINARY_OPERATOR_PLUS_PERCENT ||
        op == CC_AST_BINARY_OPERATOR_MINUS_PERCENT ||
        op == CC_AST_BINARY_OPERATOR_STAR_PERCENT ||
        op == CC_AST_BINARY_OPERATOR_PERCENT;

    SemType *operand_hint = NULL;

    if (!comparison && !range && expected != NULL &&
        (expected->kind == SEM_INTEGER || expected->kind == SEM_FLOAT))
        operand_hint = expected;

    SemType *left = check_expr(ctx, expr->binary.left, operand_hint);

    if (left == NULL) return NULL;

    SemType *right = check_expr(ctx, expr->binary.right,
                                 left->kind == SEM_UNTYPED_INTEGER
                                     ? operand_hint : left);

    if (right == NULL) return NULL;

    if (left->kind == SEM_UNTYPED_INTEGER &&
        right->kind != SEM_UNTYPED_INTEGER) {
        left = coerce(ctx, left, right, expr->binary.left->span);
    } else {
        right = coerce(ctx, right, left, expr->binary.right->span);
    }

    if (ctx->failed) return NULL;

    if (left->kind == SEM_UNTYPED_INTEGER &&
        right->kind == SEM_UNTYPED_INTEGER &&
        left->length < right->length)
        left->length = right->length;

    if (comparison) {
        if (left->kind != SEM_INTEGER && left->kind != SEM_UNTYPED_INTEGER &&
            left->kind != SEM_FLOAT && !(equality && left->kind == SEM_BOOL)) {
            fail(ctx, expr->span, "invalid operands for comparison");
            return NULL;
        }

        if (left->kind == SEM_UNTYPED_INTEGER) {
            left = finalize_type(ctx, left, expr->span);
            if (left == NULL) return NULL;
        }

        SemType *boolean = builtin(ctx, PRIM_BOOL, expr->span);

        if (boolean == NULL) return NULL;
        return coerce(ctx, boolean, expected, expr->span);
    }

    if (left->kind != SEM_INTEGER && left->kind != SEM_UNTYPED_INTEGER &&
        !(arithmetic && !integral && left->kind == SEM_FLOAT)) {
        fail(ctx, expr->span, "operator requires numeric operands");
        return NULL;
    }

    if (range) {
        left = finalize_type(ctx, left, expr->span);

        if (left == NULL) return NULL;

        SemType *type = new_type(ctx, SEM_RANGE, expr->span);

        if (type == NULL) return NULL;
        type->inner = left;
        return coerce(ctx, type, expected, expr->span);
    }

    return coerce(ctx, left, expected, expr->span);
}

static SemType *check_expr(SemContext *ctx, const CcExpression *expr,
                           SemType *expected)
{
    if (expr == NULL || ctx->failed) return NULL;

    switch (expr->kind) {
    case CC_AST_EXPRESSION_INTEGER_LITERAL: {
        if (expr->integer_literal.overflow) {
            fail(ctx, expr->span, "integer literal exceeds 64 bits");
            return NULL;
        }

        SemType *value = new_type(ctx, SEM_UNTYPED_INTEGER, expr->span);

        if (value == NULL) return NULL;
        value->length = expr->integer_literal.value;
        return coerce(ctx, value, expected, expr->span);
    }
    case CC_AST_EXPRESSION_BOOLEAN_LITERAL: {
        SemType *type = builtin(ctx, PRIM_BOOL, expr->span);

        return coerce(ctx, type, expected, expr->span);
    }
    case CC_AST_EXPRESSION_BYTE_LITERAL: {
        SemType *type = builtin(ctx, PRIM_U8, expr->span);

        return coerce(ctx, type, expected, expr->span);
    }
    case CC_AST_EXPRESSION_STRING_LITERAL:
    case CC_AST_EXPRESSION_C_STRING_LITERAL: {
        bool c_string = expr->kind == CC_AST_EXPRESSION_C_STRING_LITERAL;
        SemType *element = builtin(ctx, PRIM_U8, expr->span);
        SemType *type = new_type(ctx, c_string ? SEM_POINTER : SEM_SLICE,
                                 expr->span);

        if (element == NULL || type == NULL) return NULL;
        type->inner = element;
        type->is_const = true;
        return coerce(ctx, type, expected, expr->span);
    }
    case CC_AST_EXPRESSION_IDENTIFIER: {
        SemVariable *variable = find_variable(ctx, expr->identifier_name);

        if (variable == NULL) {
            fail(ctx, expr->span,
                 find_function(ctx, expr->identifier_name) != NULL
                     ? "function values are not yet supported"
                     : "undefined name");
            return NULL;
        }

        return coerce(ctx, variable->type, expected, expr->span);
    }
    case CC_AST_EXPRESSION_PARENTHESIZED:
        return check_expr(ctx, expr->parenthesized_inner, expected);
    case CC_AST_EXPRESSION_TUPLE: {
        if (expected != NULL && expected->kind != SEM_TUPLE) {
            fail(ctx, expr->span, "type mismatch: expected a tuple");
            return NULL;
        }

        SemType *tuple = new_type(ctx, SEM_TUPLE, expr->span);

        if (tuple == NULL) return NULL;

        CcTupleExpressionElement *source = expr->tuple_elements;
        SemElement *target = expected == NULL ? NULL : expected->elements;
        SemElement **link = &tuple->elements;

        while (source != NULL) {
            if (expected != NULL && target == NULL) {
                fail(ctx, source->value->span, "tuple arity mismatch");
                return NULL;
            }

            SemType *element_type = check_expr(ctx, source->value,
                                                target == NULL ? NULL
                                                               : target->type);

            if (element_type == NULL) return NULL;

            SemElement *element = arena_alloc(ctx, sizeof(*element),
                                              source->value->span);

            if (element == NULL) return NULL;
            element->type = element_type;
            element->next = NULL;
            *link = element;
            link = &element->next;
            tuple->count++;
            source = source->next;
            if (target != NULL) target = target->next;
        }

        if (target != NULL) {
            fail(ctx, expr->span, "tuple arity mismatch");
            return NULL;
        }

        return coerce(ctx, tuple, expected, expr->span);
    }
    case CC_AST_EXPRESSION_NONE_LITERAL:
        if (expected == NULL || expected->kind != SEM_OPTIONAL) {
            fail(ctx, expr->span, "none requires an optional type context");
            return NULL;
        }
        return expected;
    case CC_AST_EXPRESSION_SOME: {
        if (expected != NULL && expected->kind != SEM_OPTIONAL) {
            fail(ctx, expr->span, "some requires an optional type");
            return NULL;
        }

        SemType *inner = check_expr(ctx, expr->some_value,
                                    expected == NULL ? NULL : expected->inner);

        if (inner == NULL) return NULL;
        if (expected != NULL) return expected;

        inner = finalize_type(ctx, inner, expr->some_value->span);

        if (inner == NULL) return NULL;

        SemType *optional = new_type(ctx, SEM_OPTIONAL, expr->span);

        if (optional != NULL) optional->inner = inner;
        return optional;
    }
    case CC_AST_EXPRESSION_UNARY: {
        CcUnaryOperator op = expr->unary.operator;

        if (op == CC_AST_UNARY_OPERATOR_AMP ||
            op == CC_AST_UNARY_OPERATOR_STAR) {
            fail(ctx, expr->span, "raw pointer operations are not yet checked");
            return NULL;
        }

        if (op == CC_AST_UNARY_OPERATOR_NOT) {
            SemType *boolean = builtin(ctx, PRIM_BOOL, expr->span);

            if (boolean == NULL ||
                check_expr(ctx, expr->unary.operand, boolean) == NULL)
                return NULL;
            return coerce(ctx, boolean, expected, expr->span);
        }

        if (op == CC_AST_UNARY_OPERATOR_MINUS &&
            expr->unary.operand->kind == CC_AST_EXPRESSION_INTEGER_LITERAL &&
            expected != NULL && expected->kind == SEM_INTEGER) {
            CcIntegerLiteral value = expr->unary.operand->integer_literal;

            if (value.overflow ||
                !fits_integer(value.value, expected->primitive, true)) {
                fail(ctx, expr->span, "negative integer out of range");
                return NULL;
            }

            return expected;
        }
        if (op == CC_AST_UNARY_OPERATOR_MINUS &&
            expr->unary.operand->kind == CC_AST_EXPRESSION_INTEGER_LITERAL &&
            expected == NULL &&
            !expr->unary.operand->integer_literal.overflow &&
            expr->unary.operand->integer_literal.value ==
                (uint64_t)INT32_MAX + 1) {
            /* The default i32 minimum is representable as a negative
             * literal even though its unsigned magnitude exceeds INT32_MAX. */
            return builtin(ctx, PRIM_I32, expr->span);
        }

        SemType *operand = check_expr(ctx, expr->unary.operand,
                                      expected != NULL &&
                                      expected->kind == SEM_INTEGER
                                          ? expected : NULL);

        if (operand == NULL) return NULL;
        if (operand->kind != SEM_INTEGER &&
            operand->kind != SEM_UNTYPED_INTEGER) {
            fail(ctx, expr->span, "unary operator requires an integer");
            return NULL;
        }
        if (op == CC_AST_UNARY_OPERATOR_MINUS &&
            operand->kind == SEM_INTEGER &&
            !signed_integer(operand->primitive)) {
            fail(ctx, expr->span, "cannot negate an unsigned integer");
            return NULL;
        }

        return coerce(ctx, operand, expected, expr->span);
    }
    case CC_AST_EXPRESSION_BINARY:
        return check_binary(ctx, expr, expected);
    case CC_AST_EXPRESSION_CALL: {
        const CcExpression *callee = expr->call.callee;

        if (callee->kind != CC_AST_EXPRESSION_IDENTIFIER) {
            fail(ctx, callee->span, "only direct function calls are supported");
            return NULL;
        }

        if (find_variable(ctx, callee->identifier_name) != NULL) {
            fail(ctx, callee->span,
                 "local variable cannot be called as a function");
            return NULL;
        }

        SemFunction *function = find_function(ctx, callee->identifier_name);

        if (function == NULL) {
            fail(ctx, callee->span, "undefined function");
            return NULL;
        }

        CcCallArgument *argument = expr->call.arguments;
        SemParameter *parameter = function->parameters;

        while (argument != NULL && parameter != NULL) {
            if (check_expr(ctx, argument->value, parameter->type) == NULL)
                return NULL;
            argument = argument->next;
            parameter = parameter->next;
        }

        if (argument != NULL || parameter != NULL) {
            fail(ctx, expr->span, "wrong number of call arguments");
            return NULL;
        }

        return coerce(ctx, function->result, expected, expr->span);
    }
    case CC_AST_EXPRESSION_TUPLE_MEMBER: {
        SemType *tuple = check_expr(ctx, expr->tuple_member.operand, NULL);

        if (tuple == NULL) return NULL;
        if (tuple->kind != SEM_TUPLE ||
            expr->tuple_member.index_value.overflow ||
            expr->tuple_member.index_value.value >= tuple->count) {
            fail(ctx, expr->tuple_member.index, "invalid tuple element index");
            return NULL;
        }

        SemElement *member = tuple->elements;

        for (uint64_t i = 0; i < expr->tuple_member.index_value.value; i++)
            member = member->next;
        return coerce(ctx, member->type, expected, expr->span);
    }
    case CC_AST_EXPRESSION_INDEX: {
        SemType *collection = check_expr(ctx, expr->index.operand, NULL);

        if (collection == NULL) return NULL;
        if (collection->kind != SEM_ARRAY && collection->kind != SEM_SLICE) {
            fail(ctx, expr->span, "indexing requires an array or slice");
            return NULL;
        }

        SemType *index = check_expr(ctx, expr->index.index, NULL);

        if (index == NULL) return NULL;
        if (index->kind != SEM_INTEGER && index->kind != SEM_UNTYPED_INTEGER) {
            fail(ctx, expr->index.index->span, "index must be an integer");
            return NULL;
        }

        if (finalize_type(ctx, index, expr->index.index->span) == NULL)
            return NULL;

        return coerce(ctx, collection->inner, expected, expr->span);
    }
    case CC_AST_EXPRESSION_MEMBER: {
        SemType *base = check_expr(ctx, expr->member.operand, NULL);

        if (base == NULL) return NULL;
        if (!name_is(ctx, expr->member.name, "len") ||
            (base->kind != SEM_ARRAY && base->kind != SEM_SLICE)) {
            fail(ctx, expr->member.name, "unknown member");
            return NULL;
        }

        SemType *size_type = builtin(ctx, PRIM_USIZE, expr->span);

        return coerce(ctx, size_type, expected, expr->span);
    }
    }

    fail(ctx, expr->span, "unsupported expression");
    return NULL;
}

static bool check_block(SemContext *ctx, const CcBlock *block,
                        bool new_scope);

static bool check_if(SemContext *ctx, const CcIf *branch)
{
    SemType *boolean = builtin(ctx, PRIM_BOOL, branch->condition->span);

    if (boolean == NULL ||
        check_expr(ctx, branch->condition, boolean) == NULL ||
        !check_block(ctx, branch->then_block, true))
        return false;

    if (branch->else_if != NULL && !check_if(ctx, branch->else_if))
        return false;

    if (branch->else_block != NULL &&
        !check_block(ctx, branch->else_block, true))
        return false;

    return true;
}

static bool check_statement(SemContext *ctx, const CcStatement *statement)
{
    switch (statement->kind) {
    case CC_AST_STATEMENT_LET: {
        SemType *declared = statement->let.type == NULL ? NULL
            : resolve_type(ctx, statement->let.type);

        if (ctx->failed) return false;

        SemType *value = check_expr(ctx, statement->let.initializer, declared);

        if (value == NULL) return false;
        value = declared != NULL ? declared
            : finalize_type(ctx, value, statement->let.initializer->span);

        if (value == NULL) return false;
        if (value->kind == SEM_VOID || value->kind == SEM_RANGE) {
            fail(ctx, statement->let.pattern->span,
                 "cannot bind a void or range value");
            return false;
        }

        return bind_pattern(ctx, statement->let.pattern, value,
                            statement->let.mutable);
    }
    case CC_AST_STATEMENT_RETURN: {
        SemType *result = ctx->current_function->result;

        if (statement->return_expression == NULL) {
            if (result->kind != SEM_VOID) {
                fail(ctx, statement->span, "return value required");
                return false;
            }
            return true;
        }

        if (result->kind == SEM_VOID) {
            fail(ctx, statement->return_expression->span,
                 "void function cannot return a value");
            return false;
        }

        return check_expr(ctx, statement->return_expression, result) != NULL;
    }
    case CC_AST_STATEMENT_EXPRESSION: {
        SemType *type = check_expr(ctx, statement->expression, NULL);

        return type != NULL &&
               finalize_type(ctx, type, statement->expression->span) != NULL;
    }
    case CC_AST_STATEMENT_ASSIGNMENT: {
        const CcExpression *target = statement->assignment.target;

        if (target->kind != CC_AST_EXPRESSION_IDENTIFIER) {
            fail(ctx, target->span, "assignment target is not a variable");
            return false;
        }

        SemVariable *variable = find_variable(ctx, target->identifier_name);

        if (variable == NULL) {
            fail(ctx, target->span, "undefined name");
            return false;
        }
        if (!variable->mutable) {
            fail(ctx, target->span, "cannot assign to immutable binding");
            return false;
        }

        if (statement->assignment.operator != CC_AST_ASSIGNMENT_ASSIGN) {
            if (variable->type->kind != SEM_INTEGER &&
                variable->type->kind != SEM_FLOAT) {
                fail(ctx, target->span,
                     "compound assignment requires a numeric variable");
                return false;
            }

            if (variable->type->kind == SEM_FLOAT &&
                statement->assignment.operator != CC_AST_ASSIGNMENT_PLUS &&
                statement->assignment.operator != CC_AST_ASSIGNMENT_MINUS &&
                statement->assignment.operator != CC_AST_ASSIGNMENT_STAR &&
                statement->assignment.operator != CC_AST_ASSIGNMENT_SLASH) {
                fail(ctx, target->span,
                     "compound operator requires an integer variable");
                return false;
            }
        }

        return check_expr(ctx, statement->assignment.value,
                          variable->type) != NULL;
    }
    case CC_AST_STATEMENT_IF:
        return check_if(ctx, statement->if_statement);
    case CC_AST_STATEMENT_WHILE: {
        SemType *boolean = builtin(ctx, PRIM_BOOL,
                                   statement->while_statement->condition->span);

        if (boolean == NULL ||
            check_expr(ctx, statement->while_statement->condition, boolean) ==
                NULL)
            return false;

        ctx->loop_depth++;
        bool ok = check_block(ctx, statement->while_statement->body, true);
        ctx->loop_depth--;
        return ok;
    }
    case CC_AST_STATEMENT_LOOP: {
        ctx->loop_depth++;
        bool ok = check_block(ctx, statement->loop_statement->body, true);
        ctx->loop_depth--;
        return ok;
    }
    case CC_AST_STATEMENT_FOR: {
        SemType *range = check_expr(ctx, statement->for_statement->range, NULL);

        if (range == NULL) return false;
        if (range->kind != SEM_RANGE) {
            fail(ctx, statement->for_statement->range->span,
                 "for expects an integer range");
            return false;
        }

        SemType *inner = finalize_type(ctx, range->inner,
                                      statement->for_statement->range->span);

        if (inner == NULL) return false;

        SemVariable *saved = ctx->variables;
        ctx->depth++;
        bool ok = bind_variable(ctx, statement->for_statement->name, inner,
                                false);

        if (ok) {
            ctx->loop_depth++;
            ok = check_block(ctx, statement->for_statement->body, true);
            ctx->loop_depth--;
        }

        ctx->variables = saved;
        ctx->depth--;
        return ok;
    }
    case CC_AST_STATEMENT_BREAK:
    case CC_AST_STATEMENT_CONTINUE:
        if (ctx->loop_depth == 0) {
            fail(ctx, statement->span, "break or continue outside a loop");
            return false;
        }
        return true;
    }

    fail(ctx, statement->span, "unsupported statement");
    return false;
}

static bool check_block(SemContext *ctx, const CcBlock *block, bool new_scope)
{
    SemVariable *saved = ctx->variables;

    if (new_scope) ctx->depth++;

    for (CcStatement *statement = block->first; statement != NULL;
         statement = statement->next) {
        if (!check_statement(ctx, statement)) {
            ctx->variables = saved;
            if (new_scope) ctx->depth--;
            return false;
        }
    }

    if (new_scope) {
        ctx->variables = saved;
        ctx->depth--;
    }
    return true;
}

static bool if_guarantees_return(const CcIf *branch);

static bool block_guarantees_return(const CcBlock *block)
{
    for (CcStatement *stmt = block->first; stmt != NULL; stmt = stmt->next) {
        if (stmt->kind == CC_AST_STATEMENT_RETURN) return true;
        if (stmt->kind == CC_AST_STATEMENT_IF &&
            if_guarantees_return(stmt->if_statement))
            return true;
    }

    return false;
}

static bool if_guarantees_return(const CcIf *branch)
{
    if (!block_guarantees_return(branch->then_block)) return false;
    if (branch->else_if != NULL)
        return if_guarantees_return(branch->else_if);
    if (branch->else_block != NULL)
        return block_guarantees_return(branch->else_block);
    return false;
}

static bool register_functions(SemContext *ctx,
                               const CcFunctionDeclaration *functions)
{
    SemFunction **link = &ctx->functions;

    for (const CcFunctionDeclaration *decl = functions; decl != NULL;
         decl = decl->next) {
        if (find_function(ctx, decl->name) != NULL) {
            fail(ctx, decl->name, "duplicate function name");
            return false;
        }

        SemFunction *function =
            arena_alloc(ctx, sizeof(*function), decl->span);

        if (function == NULL) return false;
        function->decl = decl;
        function->parameters = NULL;
        function->next = NULL;
        function->result = decl->return_type == NULL
            ? builtin(ctx, PRIM_VOID, decl->span)
            : resolve_type(ctx, decl->return_type);

        if (ctx->failed) return false;

        SemParameter **param_link = &function->parameters;

        for (CcParameter *param = decl->parameters; param != NULL;
             param = param->next) {
            SemType *type = resolve_type(ctx, param->type);

            if (ctx->failed) return false;
            if (type->kind == SEM_VOID) {
                fail(ctx, param->type->span, "parameter cannot have void type");
                return false;
            }

            for (SemParameter *previous = function->parameters;
                 previous != NULL; previous = previous->next) {
                if (same_name(ctx, previous->name, param->name)) {
                    fail(ctx, param->name, "duplicate parameter name");
                    return false;
                }
            }

            SemParameter *node = arena_alloc(ctx, sizeof(*node), param->span);

            if (node == NULL) return false;
            node->name = param->name;
            node->type = type;
            node->next = NULL;
            *param_link = node;
            param_link = &node->next;
        }

        *link = function;
        link = &function->next;
    }

    return true;
}

bool cc_semantic_check(const CcSourceFile *file,
                       const CcFunctionDeclaration *functions,
                       CcArena *arena, CcSemanticError *error)
{
    if (error == NULL) return false;

    error->message = NULL;
    error->span.file_id = CC_FILE_ID_INVALID;
    error->span.start = 0;
    error->span.end = 0;

    if (file == NULL || arena == NULL) {
        error->message = "invalid semantic analyzer input";
        return false;
    }

    SemContext ctx = {0};

    ctx.file = file;
    ctx.arena = arena;
    ctx.error = error;

    if (!register_functions(&ctx, functions)) return false;

    for (SemFunction *function = ctx.functions; function != NULL;
         function = function->next) {
        ctx.current_function = function;
        ctx.variables = NULL;
        ctx.depth = 1;
        ctx.loop_depth = 0;

        for (SemParameter *param = function->parameters; param != NULL;
             param = param->next) {
            if (!bind_variable(&ctx, param->name, param->type, false))
                return false;
        }

        if (!check_block(&ctx, function->decl->body, false)) return false;

        if (function->result->kind != SEM_VOID &&
            !block_guarantees_return(function->decl->body)) {
            fail(&ctx, function->decl->span,
                 "not all control-flow paths return a value");
            return false;
        }
    }

    return true;
}
