#include "ir/ir.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct LowerVar LowerVar;
typedef struct LowerLoop LowerLoop;
typedef struct LowerStruct LowerStruct;

struct LowerStruct {
    const CcStructDeclaration *decl;
    CcIrType id;
    unsigned int state;
    LowerStruct *next;
};

struct LowerVar {
    CcSourceSpan name;
    uint32_t slot;
    bool mutable;
    LowerVar *next;
};

struct LowerLoop {
    uint32_t break_target;
    uint32_t continue_target;
    LowerLoop *previous;
};

typedef struct {
    const CcSourceFile *file;
    CcArena *arena;
    CcIrModule *module;
    CcIrFunction *function;
    CcIrBlock *current;
    LowerVar *variables;
    LowerLoop *loop;
    LowerStruct *structs;
    CcIrError *error;
    bool failed;
} Lower;

typedef struct {
    uint32_t reg;
    CcIrType type;
} Value;

static Value no_value(void)
{
    Value value = { CC_IR_NONE, CC_IR_VOID };
    return value;
}

static void error_at(Lower *ctx, CcSourceSpan span, const char *message)
{
    if (ctx->failed) return;
    ctx->failed = true;
    ctx->error->span = span;
    ctx->error->message = message;
}

static void *new_object(Lower *ctx, size_t size, CcSourceSpan span)
{
    void *pointer = cc_arena_alloc(ctx->arena, size);

    if (pointer == NULL) error_at(ctx, span, "out of memory");
    return pointer;
}

const CcIrTypeInfo *cc_ir_type_info(const CcIrModule *module, CcIrType type)
{
    if (module == NULL || type < 3 || type >= module->type_count)
        return NULL;
    for (const CcIrTypeInfo *info = module->types; info != NULL;
         info = info->next)
        if (info->id == type) return info;
    return NULL;
}

static CcIrType intern_type(Lower *ctx, CcIrCompositeKind kind,
                            CcIrType inner, uint32_t count,
                            const CcIrType *members, CcSourceSpan span)
{
    for (CcIrTypeInfo *info = ctx->module->types; info != NULL;
         info = info->next) {
        if (info->kind != kind) continue;
        if (kind == CC_IR_TYPE_STRUCT) continue;
        if (kind == CC_IR_TYPE_OPTIONAL || kind == CC_IR_TYPE_ARRAY ||
            kind == CC_IR_TYPE_POINTER || kind == CC_IR_TYPE_SLICE) {
            if (info->inner == inner &&
                (kind != CC_IR_TYPE_ARRAY || info->count == count) &&
                (kind != CC_IR_TYPE_POINTER ||
                 info->pointer_const == (count != 0)) &&
                (kind != CC_IR_TYPE_SLICE ||
                 info->slice_const == (count != 0)))
                return info->id;
            continue;
        }
        if (info->count != count) continue;
        uint32_t i;
        for (i = 0; i < count; i++)
            if (info->members[i] != members[i]) break;
        if (i == count) return info->id;
    }
    if (ctx->module->type_count >= CC_IR_USIZE) {
        error_at(ctx, span, "too many IR types");
        return CC_IR_NONE;
    }
    CcIrTypeInfo *info = new_object(ctx, sizeof(*info), span);
    if (info == NULL) return CC_IR_NONE;
    memset(info, 0, sizeof(*info));
    info->id = ctx->module->type_count++;
    info->kind = kind;
    info->inner = inner;
    info->count = count;
    info->pointer_const = kind == CC_IR_TYPE_POINTER && count != 0;
    info->slice_const = kind == CC_IR_TYPE_SLICE && count != 0;
    if (kind == CC_IR_TYPE_POINTER || kind == CC_IR_TYPE_SLICE)
        info->count = 0;
    if (kind == CC_IR_TYPE_TUPLE && count != 0) {
        info->members = new_object(ctx, count * sizeof(*members), span);
        if (info->members == NULL) return CC_IR_NONE;
        memcpy(info->members, members, count * sizeof(*members));
    }
    if (ctx->module->last_type == NULL)
        ctx->module->types = info;
    else
        ctx->module->last_type->next = info;
    ctx->module->last_type = info;
    return info->id;
}

static bool name_equal(Lower *ctx, CcSourceSpan a, CcSourceSpan b)
{
    if (a.end < a.start || b.end < b.start ||
        a.end > ctx->file->length || b.end > ctx->file->length)
        return false;
    size_t length = a.end - a.start;
    return length == b.end - b.start &&
           memcmp(ctx->file->bytes + a.start, ctx->file->bytes + b.start,
                  length) == 0;
}

static bool name_is(Lower *ctx, CcSourceSpan a, const char *name)
{
    size_t size = strlen(name);
    return a.end >= a.start && a.end <= ctx->file->length &&
           a.end - a.start == size &&
           memcmp(ctx->file->bytes + a.start, name, size) == 0;
}

static LowerStruct *find_struct(Lower *ctx, CcSourceSpan name)
{
    for (LowerStruct *entry = ctx->structs; entry != NULL;
         entry = entry->next)
        if (name_equal(ctx, entry->decl->name, name)) return entry;
    return NULL;
}

static bool type_of(Lower *ctx, const CcType *node, CcIrType *result,
                    CcSourceSpan fallback);

static CcIrType intern_struct(Lower *ctx, LowerStruct *entry)
{
    if (entry->state == 2) return entry->id;
    if (entry->state == 1) {
        error_at(ctx, entry->decl->name,
                 "IR lowering: recursive struct value type");
        return CC_IR_NONE;
    }
    entry->state = 1;
    uint32_t count = 0;
    for (const CcStructField *field = entry->decl->fields; field != NULL;
         field = field->next) {
        if (count >= 65536U) {
            error_at(ctx, field->name, "IR lowering: too many struct fields");
            return CC_IR_NONE;
        }
        count++;
    }
    CcIrType *types = NULL;
    CcSourceSpan *names = NULL;
    if (count != 0) {
        types = new_object(ctx, (size_t)count * sizeof(*types),
                           entry->decl->span);
        names = new_object(ctx, (size_t)count * sizeof(*names),
                           entry->decl->span);
        if (types == NULL || names == NULL) return CC_IR_NONE;
    }
    uint32_t index = 0;
    for (const CcStructField *field = entry->decl->fields; field != NULL;
         field = field->next) {
        if (!type_of(ctx, field->type, &types[index], field->name))
            return CC_IR_NONE;
        if (types[index] == CC_IR_VOID) {
            error_at(ctx, field->type->span,
                     "IR lowering: void struct field unsupported");
            return CC_IR_NONE;
        }
        names[index] = field->name;
        index++;
    }
    if (ctx->module->type_count >= CC_IR_USIZE) {
        error_at(ctx, entry->decl->span, "too many IR types");
        return CC_IR_NONE;
    }
    CcIrTypeInfo *info = new_object(ctx, sizeof(*info), entry->decl->span);
    if (info == NULL) return CC_IR_NONE;
    memset(info, 0, sizeof(*info));
    info->id = ctx->module->type_count++;
    info->kind = CC_IR_TYPE_STRUCT;
    info->count = count;
    info->members = types;
    info->name = entry->decl->name;
    info->field_names = names;
    if (ctx->module->last_type == NULL)
        ctx->module->types = info;
    else
        ctx->module->last_type->next = info;
    ctx->module->last_type = info;
    entry->id = info->id;
    entry->state = 2;
    return info->id;
}

static bool array_length(const CcExpression *expr, uint64_t *out)
{
    switch (expr->kind) {
    case CC_AST_EXPRESSION_INTEGER_LITERAL:
        if (expr->integer_literal.overflow) return false;
        *out = expr->integer_literal.value;
        return true;
    case CC_AST_EXPRESSION_PARENTHESIZED:
        return array_length(expr->parenthesized_inner, out);
    case CC_AST_EXPRESSION_BINARY: {
        uint64_t a, b;
        if (!array_length(expr->binary.left, &a) ||
            !array_length(expr->binary.right, &b)) return false;
        switch (expr->binary.operator) {
        case CC_AST_BINARY_OPERATOR_PLUS:
            if (UINT64_MAX - a < b) return false;
            *out = a + b; return true;
        case CC_AST_BINARY_OPERATOR_MINUS:
            if (a < b) return false;
            *out = a - b; return true;
        case CC_AST_BINARY_OPERATOR_STAR:
            if (b != 0 && a > UINT64_MAX / b) return false;
            *out = a * b; return true;
        case CC_AST_BINARY_OPERATOR_SLASH:
            if (b == 0) return false;
            *out = a / b; return true;
        case CC_AST_BINARY_OPERATOR_PERCENT:
            if (b == 0) return false;
            *out = a % b; return true;
        default: return false;
        }
    }
    default: return false;
    }
}

static bool type_of(Lower *ctx, const CcType *node, CcIrType *result,
                    CcSourceSpan fallback)
{
    if (node == NULL) {
        *result = CC_IR_VOID;
        return true;
    }
    if (node->kind == CC_AST_TYPE_PARENTHESIZED)
        return type_of(ctx, node->parenthesized_inner, result, fallback);
    if (node->kind == CC_AST_TYPE_NAMED) {
        if (name_is(ctx, node->span, "void")) {
            *result = CC_IR_VOID;
            return true;
        }
        if (name_is(ctx, node->span, "i32")) {
            *result = CC_IR_I32;
            return true;
        }
        if (name_is(ctx, node->span, "bool")) {
            *result = CC_IR_BOOL;
            return true;
        }
        if (name_is(ctx, node->span, "usize")) {
            *result = CC_IR_USIZE;
            return true;
        }
        LowerStruct *record = find_struct(ctx, node->span);
        if (record != NULL) {
            *result = intern_struct(ctx, record);
            return !ctx->failed;
        }
    }
    if (node->kind == CC_AST_TYPE_OPTIONAL) {
        CcIrType inner;
        if (!type_of(ctx, node->optional_inner, &inner, node->span))
            return false;
        if (inner == CC_IR_VOID) {
            error_at(ctx, node->span, "IR lowering: optional void unsupported");
            return false;
        }
        *result = intern_type(ctx, CC_IR_TYPE_OPTIONAL, inner, 0, NULL,
                              node->span);
        return !ctx->failed;
    }
    if (node->kind == CC_AST_TYPE_POINTER) {
        CcIrType inner;
        if (!type_of(ctx, node->pointer_inner, &inner, node->span))
            return false;
        if (inner == CC_IR_VOID) {
            error_at(ctx, node->span,
                     "IR lowering: void pointer unsupported");
            return false;
        }
        *result = intern_type(ctx, CC_IR_TYPE_POINTER, inner,
                              node->pointer_pointee_const ? 1U : 0U,
                              NULL, node->span);
        return !ctx->failed;
    }
    if (node->kind == CC_AST_TYPE_SLICE) {
        CcIrType inner;
        if (!type_of(ctx, node->slice_inner, &inner, node->span))
            return false;
        if (inner == CC_IR_VOID) {
            error_at(ctx, node->span,
                     "IR lowering: void slice element unsupported");
            return false;
        }
        *result = intern_type(ctx, CC_IR_TYPE_SLICE, inner,
                              node->slice_element_const ? 1U : 0U,
                              NULL, node->span);
        return !ctx->failed;
    }
    if (node->kind == CC_AST_TYPE_TUPLE) {
        uint32_t count = 0;
        for (CcTupleTypeElement *part = node->tuple_elements; part != NULL;
             part = part->next) count++;
        CcIrType *members = NULL;
        if (count != 0) {
            members = new_object(ctx, count * sizeof(*members), node->span);
            if (members == NULL) return false;
        }
        uint32_t index = 0;
        for (CcTupleTypeElement *part = node->tuple_elements; part != NULL;
             part = part->next) {
            if (!type_of(ctx, part->type, &members[index], part->type->span))
                return false;
            if (members[index] == CC_IR_VOID) {
                error_at(ctx, part->type->span,
                         "IR lowering: void tuple element unsupported");
                return false;
            }
            index++;
        }
        *result = intern_type(ctx, CC_IR_TYPE_TUPLE, CC_IR_VOID, count,
                              members, node->span);
        return !ctx->failed;
    }
    if (node->kind == CC_AST_TYPE_ARRAY) {
        uint64_t length = 0;
        if (!array_length(node->array.length, &length) ||
            length > 65536U) {
            error_at(ctx, node->span,
                     "IR lowering: array length exceeds current C17 limit");
            return false;
        }
        CcIrType element;
        if (!type_of(ctx, node->array.element, &element, node->span))
            return false;
        if (element == CC_IR_VOID) {
            error_at(ctx, node->span,
                     "IR lowering: void array element unsupported");
            return false;
        }
        *result = intern_type(ctx, CC_IR_TYPE_ARRAY, element,
                              (uint32_t)length, NULL, node->span);
        return !ctx->failed;
    }
    error_at(ctx, node == NULL ? fallback : node->span,
             "IR backend currently supports only i32, usize, bool, void, tuples, optionals, fixed arrays, structs, pointers and slices");
    return false;
}

static CcIrFunction *find_function(Lower *ctx, CcSourceSpan name)
{
    for (CcIrFunction *function = ctx->module->functions; function != NULL;
         function = function->next)
        if (name_equal(ctx, function->name, name)) return function;
    return NULL;
}

static LowerVar *find_variable(Lower *ctx, CcSourceSpan name)
{
    for (LowerVar *var = ctx->variables; var != NULL; var = var->next)
        if (name_equal(ctx, var->name, name)) return var;
    return NULL;
}

static CcIrType slot_type(Lower *ctx, uint32_t slot)
{
    for (CcIrSlot *item = ctx->function->slots; item != NULL;
         item = item->next)
        if (item->id == slot) return item->type;
    return CC_IR_VOID;
}

static uint32_t make_slot(Lower *ctx, CcIrType type, CcSourceSpan span)
{
    if (type == CC_IR_VOID || ctx->function->slot_count == CC_IR_NONE) {
        error_at(ctx, span, "invalid IR slot");
        return CC_IR_NONE;
    }

    CcIrSlot *slot = new_object(ctx, sizeof(*slot), span);
    if (slot == NULL) return CC_IR_NONE;
    slot->id = ctx->function->slot_count++;
    slot->type = type;
    slot->mutable = false;
    slot->next = NULL;
    if (ctx->function->last_slot == NULL)
        ctx->function->slots = slot;
    else
        ctx->function->last_slot->next = slot;
    ctx->function->last_slot = slot;
    return slot->id;
}

static bool bind_variable(Lower *ctx, CcSourceSpan name, uint32_t slot,
                          bool mutable)
{
    CcIrSlot *decl = ctx->function->slots;
    while (decl != NULL && decl->id != slot) decl = decl->next;
    if (decl == NULL) {
        error_at(ctx, name, "IR lowering: invalid binding slot");
        return false;
    }
    LowerVar *variable = new_object(ctx, sizeof(*variable), name);
    if (variable == NULL) return false;
    decl->mutable = mutable;
    variable->name = name;
    variable->slot = slot;
    variable->mutable = mutable;
    variable->next = ctx->variables;
    ctx->variables = variable;
    return true;
}

static CcIrBlock *new_block(Lower *ctx, CcSourceSpan span)
{
    if (ctx->function->block_count == CC_IR_NONE) {
        error_at(ctx, span, "too many IR blocks");
        return NULL;
    }
    CcIrBlock *block = new_object(ctx, sizeof(*block), span);
    if (block == NULL) return NULL;
    memset(block, 0, sizeof(*block));
    block->id = ctx->function->block_count++;
    block->target = CC_IR_NONE;
    block->alternate = CC_IR_NONE;
    block->condition = CC_IR_NONE;
    block->return_value = CC_IR_NONE;
    if (ctx->function->last_block == NULL)
        ctx->function->blocks = block;
    else
        ctx->function->last_block->next = block;
    ctx->function->last_block = block;
    return block;
}

static Value append(Lower *ctx, CcIrOpcode opcode, CcIrType type,
                    CcSourceSpan span, CcIrInstruction **out)
{
    Value value = no_value();
    if (ctx->failed || ctx->current == NULL || ctx->current->terminated) {
        error_at(ctx, span, "cannot append instruction to terminated IR block");
        return value;
    }
    CcIrInstruction *instruction =
        new_object(ctx, sizeof(*instruction), span);
    if (instruction == NULL) return value;
    memset(instruction, 0, sizeof(*instruction));
    instruction->op = opcode;
    instruction->type = type;
    instruction->span = span;
    instruction->result = CC_IR_NONE;
    instruction->lhs = CC_IR_NONE;
    instruction->rhs = CC_IR_NONE;
    instruction->slot = CC_IR_NONE;
    instruction->callee = CC_IR_NONE;
    if (type != CC_IR_VOID) {
        if (ctx->function->register_count == CC_IR_NONE) {
            error_at(ctx, span, "too many IR registers");
            return value;
        }
        CcIrRegister *reg = new_object(ctx, sizeof(*reg), span);
        if (reg == NULL) return value;
        reg->id = ctx->function->register_count++;
        reg->type = type;
        reg->block = ctx->current->id;
        reg->next = NULL;
        if (ctx->function->last_register == NULL)
            ctx->function->registers = reg;
        else
            ctx->function->last_register->next = reg;
        ctx->function->last_register = reg;
        instruction->result = reg->id;
        value.reg = reg->id;
        value.type = type;
    }
    if (ctx->current->last == NULL)
        ctx->current->first = instruction;
    else
        ctx->current->last->next = instruction;
    ctx->current->last = instruction;
    if (out != NULL) *out = instruction;
    return value;
}

static Value constant(Lower *ctx, CcIrType type, int32_t number,
                      CcSourceSpan span)
{
    CcIrInstruction *node = NULL;
    Value result = append(ctx, type == CC_IR_BOOL ? CC_IR_CONST_BOOL
                                                 : CC_IR_CONST_I32,
                          type, span, &node);
    if (node != NULL) node->constant = number;
    return result;
}

static Value constant_usize(Lower *ctx, uint64_t number,
                            CcSourceSpan span)
{
    CcIrInstruction *node = NULL;
    Value value = append(ctx, CC_IR_CONST_USIZE, CC_IR_USIZE, span, &node);
    if (node != NULL) node->wide_constant = number;
    return value;
}

static Value load(Lower *ctx, uint32_t slot, CcSourceSpan span)
{
    CcIrInstruction *node = NULL;
    Value result = append(ctx, CC_IR_LOAD, slot_type(ctx, slot), span, &node);
    if (node != NULL) node->slot = slot;
    return result;
}

static bool store(Lower *ctx, uint32_t slot, Value source,
                  CcSourceSpan span)
{
    if (source.reg == CC_IR_NONE || source.type != slot_type(ctx, slot)) {
        error_at(ctx, span, "IR store type mismatch");
        return false;
    }
    CcIrInstruction *node = NULL;
    append(ctx, CC_IR_STORE, CC_IR_VOID, span, &node);
    if (node == NULL) return false;
    node->slot = slot;
    node->lhs = source.reg;
    return true;
}

static void jump(Lower *ctx, CcIrBlock *target, CcSourceSpan span)
{
    if (ctx->current == NULL || ctx->current->terminated || target == NULL) {
        error_at(ctx, span, "invalid IR jump");
        return;
    }
    ctx->current->terminated = true;
    ctx->current->terminator = CC_IR_JUMP;
    ctx->current->target = target->id;
}

static void branch(Lower *ctx, Value condition, CcIrBlock *yes,
                   CcIrBlock *no, CcSourceSpan span)
{
    if (condition.type != CC_IR_BOOL ||
        ctx->current == NULL || ctx->current->terminated ||
        yes == NULL || no == NULL) {
        error_at(ctx, span, "invalid IR conditional branch");
        return;
    }
    ctx->current->terminated = true;
    ctx->current->terminator = CC_IR_BRANCH;
    ctx->current->condition = condition.reg;
    ctx->current->target = yes->id;
    ctx->current->alternate = no->id;
}

static Value lower_expr(Lower *ctx, const CcExpression *expr,
                        CcIrType expected);

static uint32_t struct_field_index(Lower *ctx,
                                   const CcIrTypeInfo *type,
                                   CcSourceSpan name)
{
    if (type == NULL || type->kind != CC_IR_TYPE_STRUCT)
        return CC_IR_NONE;
    for (uint32_t i = 0; i < type->count; i++)
        if (name_equal(ctx, type->field_names[i], name)) return i;
    return CC_IR_NONE;
}

static Value lower_logic(Lower *ctx, const CcExpression *expr, bool and)
{
    Value left = lower_expr(ctx, expr->binary.left, CC_IR_BOOL);
    if (ctx->failed) return no_value();
    if (left.type != CC_IR_BOOL) {
        error_at(ctx, expr->span, "logical operands must be bool");
        return no_value();
    }

    uint32_t slot = make_slot(ctx, CC_IR_BOOL, expr->span);
    CcIrBlock *rhs = new_block(ctx, expr->span);
    CcIrBlock *short_block = new_block(ctx, expr->span);
    CcIrBlock *join = new_block(ctx, expr->span);
    if (ctx->failed) return no_value();
    branch(ctx, left, and ? rhs : short_block,
           and ? short_block : rhs, expr->span);

    ctx->current = short_block;
    if (!store(ctx, slot, constant(ctx, CC_IR_BOOL, !and, expr->span),
               expr->span)) return no_value();
    jump(ctx, join, expr->span);

    ctx->current = rhs;
    Value right = lower_expr(ctx, expr->binary.right, CC_IR_BOOL);
    if (ctx->failed) return no_value();
    if (!store(ctx, slot, right, expr->span)) return no_value();
    jump(ctx, join, expr->span);

    ctx->current = join;
    return load(ctx, slot, expr->span);
}

static Value lower_subrange(Lower *ctx, const CcExpression *expr,
                            const CcExpression *range, CcIrType expected)
{
    /* A fixed array must be a named local: taking the address of an
     * evaluated array temporary would make the result immediately dangle. */
    const CcExpression *source = expr->index.operand;
    while (source->kind == CC_AST_EXPRESSION_PARENTHESIZED)
        source = source->parenthesized_inner;
    LowerVar *local = source->kind == CC_AST_EXPRESSION_IDENTIFIER
        ? find_variable(ctx, source->identifier_name) : NULL;
    const CcIrTypeInfo *array = local == NULL ? NULL :
        cc_ir_type_info(ctx->module, slot_type(ctx, local->slot));
    Value view;
    if (array != NULL && array->kind == CC_IR_TYPE_ARRAY) {
        CcIrType view_type = intern_type(ctx, CC_IR_TYPE_SLICE, array->inner,
                                         local->mutable ? 0U : 1U, NULL,
                                         expr->span);
        if (ctx->failed) return no_value();
        CcIrInstruction *make = NULL;
        view = append(ctx, CC_IR_MAKE_SLICE, view_type, expr->span, &make);
        if (make != NULL) {
            make->slot = local->slot;
            make->arg_count = array->count;
        }
    } else {
        view = lower_expr(ctx, expr->index.operand, CC_IR_NONE);
    }
    if (ctx->failed) return no_value();
    const CcIrTypeInfo *slice = cc_ir_type_info(ctx->module, view.type);
    if (slice == NULL || slice->kind != CC_IR_TYPE_SLICE ||
        (expected != CC_IR_NONE && expected != view.type)) {
        error_at(ctx, expr->span,
                 "IR lowering: subrange requires a matching slice");
        return no_value();
    }

    uint32_t view_slot = make_slot(ctx, view.type, expr->span);
    if (ctx->failed || !store(ctx, view_slot, view, expr->span))
        return no_value();
    Value begin = lower_expr(ctx, range->binary.left, CC_IR_I32);
    if (ctx->failed || begin.type != CC_IR_I32) {
        error_at(ctx, range->binary.left->span,
                 "IR lowering: subrange start requires i32");
        return no_value();
    }
    uint32_t begin_slot = make_slot(ctx, CC_IR_I32, expr->span);
    if (ctx->failed || !store(ctx, begin_slot, begin, expr->span))
        return no_value();
    Value end = lower_expr(ctx, range->binary.right, CC_IR_I32);
    if (ctx->failed || end.type != CC_IR_I32) {
        error_at(ctx, range->binary.right->span,
                 "IR lowering: subrange end requires i32");
        return no_value();
    }

    view = load(ctx, view_slot, expr->span);
    begin = load(ctx, begin_slot, expr->span);
    if (ctx->failed) return no_value();
    CcIrInstruction *instruction = NULL;
    Value result = append(ctx, CC_IR_SUBSLICE, view.type,
                          expr->span, &instruction);
    if (instruction != NULL) {
        instruction->lhs = view.reg;
        instruction->rhs = begin.reg;
        instruction->slot = end.reg;
    }
    return result;
}

static Value lower_expr(Lower *ctx, const CcExpression *expr,
                        CcIrType expected)
{
    if (ctx->failed) return no_value();
    switch (expr->kind) {
    case CC_AST_EXPRESSION_INTEGER_LITERAL:
        if (expected == CC_IR_USIZE &&
            !expr->integer_literal.overflow &&
            expr->integer_literal.value <= SIZE_MAX)
            return constant_usize(ctx, expr->integer_literal.value,
                                  expr->span);
        if (expr->integer_literal.overflow ||
            expr->integer_literal.value > INT32_MAX) {
            error_at(ctx, expr->span, "integer literal is not representable as i32");
            return no_value();
        }
        return constant(ctx, CC_IR_I32,
                        (int32_t)expr->integer_literal.value, expr->span);
    case CC_AST_EXPRESSION_BOOLEAN_LITERAL:
        return constant(ctx, CC_IR_BOOL, expr->boolean_literal, expr->span);
    case CC_AST_EXPRESSION_PARENTHESIZED:
        return lower_expr(ctx, expr->parenthesized_inner, expected);
    case CC_AST_EXPRESSION_IDENTIFIER: {
        LowerVar *var = find_variable(ctx, expr->identifier_name);
        if (var == NULL) {
            error_at(ctx, expr->span, "IR lowering: unknown local variable");
            return no_value();
        }
        return load(ctx, var->slot, expr->span);
    }
    case CC_AST_EXPRESSION_TUPLE: {
        uint32_t count = 0;
        for (CcTupleExpressionElement *part = expr->tuple_elements;
             part != NULL; part = part->next) count++;
        const CcIrTypeInfo *expected_info =
            cc_ir_type_info(ctx->module, expected);
        if (expected != CC_IR_NONE &&
            (expected_info == NULL ||
             expected_info->kind != CC_IR_TYPE_TUPLE ||
             expected_info->count != count)) {
            error_at(ctx, expr->span, "IR lowering: tuple type mismatch");
            return no_value();
        }
        CcIrType *members = NULL;
        uint32_t *slots = NULL;
        uint32_t *args = NULL;
        if (count != 0) {
            members = new_object(ctx, count * sizeof(*members), expr->span);
            slots = new_object(ctx, count * sizeof(*slots), expr->span);
            args = new_object(ctx, count * sizeof(*args), expr->span);
            if (members == NULL || slots == NULL || args == NULL)
                return no_value();
        }
        uint32_t index = 0;
        for (CcTupleExpressionElement *part = expr->tuple_elements;
             part != NULL; part = part->next) {
            CcIrType hint = expected_info == NULL
                ? CC_IR_NONE : expected_info->members[index];
            Value element = lower_expr(ctx, part->value, hint);
            if (ctx->failed) return no_value();
            if (element.type == CC_IR_VOID ||
                (hint != CC_IR_NONE && element.type != hint)) {
                error_at(ctx, part->value->span,
                         "IR lowering: tuple element type mismatch");
                return no_value();
            }
            members[index] = element.type;
            slots[index] = make_slot(ctx, element.type, part->value->span);
            if (ctx->failed ||
                !store(ctx, slots[index], element, part->value->span))
                return no_value();
            index++;
        }
        CcIrType type = expected_info == NULL
            ? intern_type(ctx, CC_IR_TYPE_TUPLE, CC_IR_VOID, count, members,
                          expr->span) : expected;
        if (ctx->failed) return no_value();
        for (uint32_t i = 0; i < count; i++) {
            Value element = load(ctx, slots[i], expr->span);
            if (ctx->failed) return no_value();
            args[i] = element.reg;
        }
        CcIrInstruction *instruction = NULL;
        Value tuple = append(ctx, CC_IR_MAKE_TUPLE, type, expr->span,
                             &instruction);
        if (instruction != NULL) {
            instruction->arg_count = count;
            instruction->args = args;
        }
        return tuple;
    }
    case CC_AST_EXPRESSION_ARRAY: {
        uint32_t count = 0;
        for (CcTupleExpressionElement *part = expr->array_elements;
             part != NULL; part = part->next) count++;
        const CcIrTypeInfo *hint = cc_ir_type_info(ctx->module, expected);
        if (expected != CC_IR_NONE &&
            (hint == NULL || hint->kind != CC_IR_TYPE_ARRAY ||
             hint->count != count)) {
            error_at(ctx, expr->span, "IR lowering: array type mismatch");
            return no_value();
        }
        if (expected == CC_IR_NONE && count == 0) {
            error_at(ctx, expr->span,
                     "IR lowering: empty array requires type context");
            return no_value();
        }
        if (count > 65536U) {
            error_at(ctx, expr->span,
                     "IR lowering: array length exceeds current C17 limit");
            return no_value();
        }
        uint32_t *slots = NULL;
        uint32_t *args = NULL;
        if (count != 0) {
            slots = new_object(ctx, count * sizeof(*slots), expr->span);
            args = new_object(ctx, count * sizeof(*args), expr->span);
            if (slots == NULL || args == NULL) return no_value();
        }
        CcIrType element_type = hint == NULL ? CC_IR_NONE : hint->inner;
        uint32_t index = 0;
        for (CcTupleExpressionElement *part = expr->array_elements;
             part != NULL; part = part->next) {
            Value value = lower_expr(ctx, part->value, element_type);
            if (ctx->failed) return no_value();
            if (element_type == CC_IR_NONE) element_type = value.type;
            if (value.type != element_type || value.type == CC_IR_VOID) {
                error_at(ctx, part->value->span,
                         "IR lowering: array element type mismatch");
                return no_value();
            }
            slots[index] = make_slot(ctx, value.type, part->value->span);
            if (ctx->failed ||
                !store(ctx, slots[index], value, part->value->span))
                return no_value();
            index++;
        }
        for (uint32_t p = 0; p < count; p++) {
            Value item = load(ctx, slots[p], expr->span);
            if (ctx->failed) return no_value();
            args[p] = item.reg;
        }
        CcIrType type = hint == NULL
            ? intern_type(ctx, CC_IR_TYPE_ARRAY, element_type, count,
                          NULL, expr->span)
            : expected;
        if (ctx->failed) return no_value();
        CcIrInstruction *node = NULL;
        Value array = append(ctx, CC_IR_MAKE_ARRAY, type, expr->span, &node);
        if (node != NULL) {
            node->arg_count = count;
            node->args = args;
        }
        return array;
    }
    case CC_AST_EXPRESSION_STRUCT: {
        LowerStruct *entry = find_struct(ctx, expr->struct_literal.name);
        if (entry == NULL) {
            error_at(ctx, expr->span, "IR lowering: unknown struct type");
            return no_value();
        }
        CcIrType type = intern_struct(ctx, entry);
        if (ctx->failed) return no_value();
        if (expected != CC_IR_NONE && expected != type) {
            error_at(ctx, expr->span, "IR lowering: struct type mismatch");
            return no_value();
        }
        const CcIrTypeInfo *info = cc_ir_type_info(ctx->module, type);
        uint32_t *slots = NULL;
        uint32_t *args = NULL;
        if (info->count != 0) {
            slots = new_object(ctx, (size_t)info->count * sizeof(*slots),
                               expr->span);
            args = new_object(ctx, (size_t)info->count * sizeof(*args),
                              expr->span);
            if (slots == NULL || args == NULL) return no_value();
            for (uint32_t p = 0; p < info->count; p++)
                slots[p] = CC_IR_NONE;
        }
        for (const CcStructFieldValue *part = expr->struct_literal.fields;
             part != NULL; part = part->next) {
            uint32_t index = struct_field_index(ctx, info, part->name);
            if (index == CC_IR_NONE || slots[index] != CC_IR_NONE) {
                error_at(ctx, part->name,
                         "IR lowering: invalid struct initializer field");
                return no_value();
            }
            Value value = lower_expr(ctx, part->value, info->members[index]);
            if (ctx->failed || value.type != info->members[index]) {
                error_at(ctx, part->value->span,
                         "IR lowering: struct field type mismatch");
                return no_value();
            }
            slots[index] = make_slot(ctx, value.type, part->value->span);
            if (ctx->failed || !store(ctx, slots[index], value,
                                       part->value->span))
                return no_value();
        }
        for (uint32_t p = 0; p < info->count; p++) {
            if (slots[p] == CC_IR_NONE) {
                error_at(ctx, expr->span,
                         "IR lowering: missing struct initializer field");
                return no_value();
            }
            Value value = load(ctx, slots[p], expr->span);
            if (ctx->failed) return no_value();
            args[p] = value.reg;
        }
        CcIrInstruction *node = NULL;
        Value result = append(ctx, CC_IR_MAKE_STRUCT, type, expr->span, &node);
        if (node != NULL) {
            node->arg_count = info->count;
            node->args = args;
        }
        return result;
    }
    case CC_AST_EXPRESSION_TUPLE_MEMBER: {
        Value tuple = lower_expr(ctx, expr->tuple_member.operand, CC_IR_NONE);
        if (ctx->failed) return no_value();
        const CcIrTypeInfo *info = cc_ir_type_info(ctx->module, tuple.type);
        if (info == NULL || info->kind != CC_IR_TYPE_TUPLE ||
            expr->tuple_member.index_value.overflow ||
            expr->tuple_member.index_value.value >= info->count) {
            error_at(ctx, expr->span, "IR lowering: invalid tuple member");
            return no_value();
        }
        uint32_t index = (uint32_t)expr->tuple_member.index_value.value;
        CcIrInstruction *instruction = NULL;
        Value element = append(ctx, CC_IR_GET_TUPLE, info->members[index],
                               expr->span, &instruction);
        if (instruction != NULL) {
            instruction->lhs = tuple.reg;
            instruction->slot = index;
        }
        return element;
    }
    case CC_AST_EXPRESSION_INDEX: {
        const CcExpression *range = expr->index.index;
        while (range->kind == CC_AST_EXPRESSION_PARENTHESIZED)
            range = range->parenthesized_inner;
        if (range->kind == CC_AST_EXPRESSION_BINARY &&
            range->binary.operator == CC_AST_BINARY_OPERATOR_RANGE)
            return lower_subrange(ctx, expr, range, expected);
        Value array = lower_expr(ctx, expr->index.operand, CC_IR_NONE);
        if (ctx->failed) return no_value();
        const CcIrTypeInfo *type = cc_ir_type_info(ctx->module, array.type);
        if (type == NULL || (type->kind != CC_IR_TYPE_ARRAY &&
                             type->kind != CC_IR_TYPE_SLICE)) {
            error_at(ctx, expr->span,
                     "IR lowering: indexing requires array or slice");
            return no_value();
        }
        uint32_t saved = make_slot(ctx, array.type, expr->span);
        if (ctx->failed || !store(ctx, saved, array, expr->span))
            return no_value();
        Value index = lower_expr(ctx, expr->index.index, CC_IR_I32);
        if (ctx->failed) return no_value();
        if (index.type != CC_IR_I32) {
            error_at(ctx, expr->index.index->span,
                     "IR lowering: array index requires i32");
            return no_value();
        }
        array = load(ctx, saved, expr->span);
        if (ctx->failed) return no_value();
        CcIrInstruction *node = NULL;
        Value item = append(ctx, type->kind == CC_IR_TYPE_SLICE
                                ? CC_IR_GET_SLICE : CC_IR_GET_ARRAY, type->inner,
                            expr->span, &node);
        if (node != NULL) {
            node->lhs = array.reg;
            node->rhs = index.reg;
            if (type->kind == CC_IR_TYPE_ARRAY)
                node->arg_count = type->count;
        }
        return item;
    }
    case CC_AST_EXPRESSION_MEMBER: {
        Value array = lower_expr(ctx, expr->member.operand, CC_IR_NONE);
        if (ctx->failed) return no_value();
        const CcIrTypeInfo *type = cc_ir_type_info(ctx->module, array.type);
        if (type != NULL && type->kind == CC_IR_TYPE_OPTIONAL) {
            CcIrOpcode op;
            CcIrType result_type;
            if (name_is(ctx, expr->member.name, "has")) {
                op = CC_IR_OPTIONAL_HAS;
                result_type = CC_IR_BOOL;
            } else if (name_is(ctx, expr->member.name, "value")) {
                op = CC_IR_OPTIONAL_UNWRAP;
                result_type = type->inner;
            } else {
                error_at(ctx, expr->member.name,
                         "IR lowering: unknown optional member");
                return no_value();
            }
            if (expected != CC_IR_NONE && expected != result_type) {
                error_at(ctx, expr->span,
                         "IR lowering: optional member type mismatch");
                return no_value();
            }
            CcIrInstruction *node = NULL;
            Value result = append(ctx, op, result_type, expr->span, &node);
            if (node != NULL) node->lhs = array.reg;
            return result;
        }
        if (type != NULL && type->kind == CC_IR_TYPE_STRUCT) {
            uint32_t index = struct_field_index(ctx, type, expr->member.name);
            if (index == CC_IR_NONE) {
                error_at(ctx, expr->member.name,
                         "IR lowering: unknown struct field");
                return no_value();
            }
            CcIrInstruction *node = NULL;
            Value result = append(ctx, CC_IR_GET_STRUCT,
                                  type->members[index], expr->span, &node);
            if (node != NULL) {
                node->lhs = array.reg;
                node->slot = index;
            }
            return result;
        }
        if (type == NULL ||
            (type->kind != CC_IR_TYPE_ARRAY &&
             type->kind != CC_IR_TYPE_SLICE) ||
            !name_is(ctx, expr->member.name, "len")) {
            error_at(ctx, expr->span,
                     "IR lowering: only array/slice .len is supported");
            return no_value();
        }
        if (type->kind == CC_IR_TYPE_SLICE) {
            CcIrInstruction *node = NULL;
            Value length = append(ctx, CC_IR_SLICE_LEN, CC_IR_USIZE,
                                  expr->span, &node);
            if (node != NULL) node->lhs = array.reg;
            return length;
        }
        return constant_usize(ctx, type->count, expr->span);
    }
    case CC_AST_EXPRESSION_NONE_LITERAL: {
        const CcIrTypeInfo *info = cc_ir_type_info(ctx->module, expected);
        if (info == NULL || info->kind != CC_IR_TYPE_OPTIONAL) {
            error_at(ctx, expr->span,
                     "IR lowering: none requires an optional type");
            return no_value();
        }
        return append(ctx, CC_IR_MAKE_NONE, expected, expr->span, NULL);
    }
    case CC_AST_EXPRESSION_SOME: {
        const CcIrTypeInfo *info = cc_ir_type_info(ctx->module, expected);
        if (expected != CC_IR_NONE &&
            (info == NULL || info->kind != CC_IR_TYPE_OPTIONAL)) {
            error_at(ctx, expr->span, "IR lowering: some requires optional");
            return no_value();
        }
        Value inner = lower_expr(ctx, expr->some_value,
                                 info == NULL ? CC_IR_NONE : info->inner);
        if (ctx->failed) return no_value();
        CcIrType type = info == NULL
            ? intern_type(ctx, CC_IR_TYPE_OPTIONAL, inner.type, 0, NULL,
                          expr->span) : expected;
        if (ctx->failed || inner.type == CC_IR_VOID ||
            (info != NULL && info->inner != inner.type)) {
            error_at(ctx, expr->span, "IR lowering: optional payload mismatch");
            return no_value();
        }
        CcIrInstruction *instruction = NULL;
        Value optional = append(ctx, CC_IR_MAKE_SOME, type, expr->span,
                                &instruction);
        if (instruction != NULL) instruction->lhs = inner.reg;
        return optional;
    }
    case CC_AST_EXPRESSION_UNARY: {
        if (expr->unary.operator == CC_AST_UNARY_OPERATOR_AMP) {
            const CcExpression *target = expr->unary.operand;
            while (target->kind == CC_AST_EXPRESSION_PARENTHESIZED)
                target = target->parenthesized_inner;
            if (target->kind != CC_AST_EXPRESSION_IDENTIFIER) {
                error_at(ctx, expr->span,
                         "IR lowering: address-of requires named local");
                return no_value();
            }
            LowerVar *variable = find_variable(ctx, target->identifier_name);
            if (variable == NULL) {
                error_at(ctx, target->span,
                         "IR lowering: unknown address-of variable");
                return no_value();
            }
            CcIrType pointer_type = intern_type(ctx, CC_IR_TYPE_POINTER,
                slot_type(ctx, variable->slot), variable->mutable ? 0 : 1,
                NULL, expr->span);
            if (ctx->failed || (expected != CC_IR_NONE &&
                                expected != pointer_type)) {
                error_at(ctx, expr->span,
                         "IR lowering: address-of pointer type mismatch");
                return no_value();
            }
            CcIrInstruction *node = NULL;
            Value result = append(ctx, CC_IR_ADDR_SLOT, pointer_type,
                                  expr->span, &node);
            if (node != NULL) node->slot = variable->slot;
            return result;
        }
        if (expr->unary.operator == CC_AST_UNARY_OPERATOR_STAR) {
            Value pointer = lower_expr(ctx, expr->unary.operand, CC_IR_NONE);
            if (ctx->failed) return no_value();
            const CcIrTypeInfo *info =
                cc_ir_type_info(ctx->module, pointer.type);
            if (info == NULL || info->kind != CC_IR_TYPE_POINTER ||
                (expected != CC_IR_NONE && info->inner != expected)) {
                error_at(ctx, expr->span,
                         "IR lowering: dereference requires matching pointer");
                return no_value();
            }
            CcIrInstruction *node = NULL;
            Value value = append(ctx, CC_IR_DEREF_PTR, info->inner,
                                 expr->span, &node);
            if (node != NULL) node->lhs = pointer.reg;
            return value;
        }
        if (expr->unary.operator == CC_AST_UNARY_OPERATOR_MINUS &&
            expr->unary.operand->kind == CC_AST_EXPRESSION_INTEGER_LITERAL &&
            !expr->unary.operand->integer_literal.overflow &&
            expr->unary.operand->integer_literal.value ==
                (uint64_t)INT32_MAX + 1)
            return constant(ctx, CC_IR_I32, INT32_MIN, expr->span);
        CcIrOpcode op;
        if (expr->unary.operator == CC_AST_UNARY_OPERATOR_MINUS)
            op = CC_IR_NEG;
        else if (expr->unary.operator == CC_AST_UNARY_OPERATOR_NOT)
            op = CC_IR_NOT;
        else if (expr->unary.operator == CC_AST_UNARY_OPERATOR_TILDE)
            op = CC_IR_BIT_NOT;
        else {
            error_at(ctx, expr->span,
                     "IR lowering: unary operator not yet supported");
            return no_value();
        }
        Value operand = lower_expr(ctx, expr->unary.operand, CC_IR_NONE);
        if (ctx->failed) return no_value();
        CcIrInstruction *node = NULL;
        Value result = append(ctx, op, operand.type, expr->span, &node);
        if (node != NULL) node->lhs = operand.reg;
        return result;
    }
    case CC_AST_EXPRESSION_BINARY: {
        if (expr->binary.operator == CC_AST_BINARY_OPERATOR_AND_AND)
            return lower_logic(ctx, expr, true);
        if (expr->binary.operator == CC_AST_BINARY_OPERATOR_OR_OR)
            return lower_logic(ctx, expr, false);
        CcIrOpcode op;
        CcIrType result_type = CC_IR_I32;
        switch (expr->binary.operator) {
        case CC_AST_BINARY_OPERATOR_PLUS: op = CC_IR_ADD; break;
        case CC_AST_BINARY_OPERATOR_MINUS: op = CC_IR_SUB; break;
        case CC_AST_BINARY_OPERATOR_STAR: op = CC_IR_MUL; break;
        case CC_AST_BINARY_OPERATOR_SLASH: op = CC_IR_DIV; break;
        case CC_AST_BINARY_OPERATOR_PERCENT: op = CC_IR_MOD; break;
        case CC_AST_BINARY_OPERATOR_PLUS_PERCENT: op = CC_IR_WRAP_ADD; break;
        case CC_AST_BINARY_OPERATOR_MINUS_PERCENT: op = CC_IR_WRAP_SUB; break;
        case CC_AST_BINARY_OPERATOR_STAR_PERCENT: op = CC_IR_WRAP_MUL; break;
        case CC_AST_BINARY_OPERATOR_AMP: op = CC_IR_BIT_AND; break;
        case CC_AST_BINARY_OPERATOR_PIPE: op = CC_IR_BIT_OR; break;
        case CC_AST_BINARY_OPERATOR_CARET: op = CC_IR_BIT_XOR; break;
        case CC_AST_BINARY_OPERATOR_EQ: op = CC_IR_EQ; result_type = CC_IR_BOOL; break;
        case CC_AST_BINARY_OPERATOR_NE: op = CC_IR_NE; result_type = CC_IR_BOOL; break;
        case CC_AST_BINARY_OPERATOR_LT: op = CC_IR_LT; result_type = CC_IR_BOOL; break;
        case CC_AST_BINARY_OPERATOR_LE: op = CC_IR_LE; result_type = CC_IR_BOOL; break;
        case CC_AST_BINARY_OPERATOR_GT: op = CC_IR_GT; result_type = CC_IR_BOOL; break;
        case CC_AST_BINARY_OPERATOR_GE: op = CC_IR_GE; result_type = CC_IR_BOOL; break;
        default:
            error_at(ctx, expr->span,
                     "IR lowering: binary operator not yet supported");
            return no_value();
        }
        Value left = lower_expr(ctx, expr->binary.left, CC_IR_NONE);
        if (ctx->failed) return no_value();
        /* The right operand may contain short-circuit control flow.
         * Preserve the left value in a slot and reload it in the join block. */
        uint32_t slot = make_slot(ctx, left.type, expr->span);
        if (ctx->failed || !store(ctx, slot, left, expr->span))
            return no_value();
        Value right = lower_expr(ctx, expr->binary.right,
            (op == CC_IR_EQ || op == CC_IR_NE) ? left.type : CC_IR_NONE);
        if (ctx->failed) return no_value();
        if (left.type != right.type) {
            const CcExpression *literal = expr->binary.left;
            while (literal->kind == CC_AST_EXPRESSION_PARENTHESIZED)
                literal = literal->parenthesized_inner;
            if ((op == CC_IR_EQ || op == CC_IR_NE) &&
                left.type == CC_IR_I32 && right.type == CC_IR_USIZE &&
                literal->kind == CC_AST_EXPRESSION_INTEGER_LITERAL &&
                !literal->integer_literal.overflow) {
                /* A context-free integer literal is initially i32. When
                 * its RHS resolves to usize, re-materialize that pure
                 * literal with the precise contextual type. */
                left = constant_usize(ctx, literal->integer_literal.value,
                                      literal->span);
            } else {
                error_at(ctx, expr->span, "IR operand type mismatch");
                return no_value();
            }
        } else {
            left = load(ctx, slot, expr->span);
        }
        if (ctx->failed) return no_value();
        CcIrInstruction *node = NULL;
        Value result = append(ctx, op, result_type, expr->span, &node);
        if (node != NULL) {
            node->lhs = left.reg;
            node->rhs = right.reg;
        }
        return result;
    }
    case CC_AST_EXPRESSION_CALL: {
        if (expr->call.callee->kind == CC_AST_EXPRESSION_IDENTIFIER &&
            name_is(ctx, expr->call.callee->identifier_name, "slice") &&
            find_function(ctx, expr->call.callee->identifier_name) == NULL) {
            const CcCallArgument *arg = expr->call.arguments;
            if (arg == NULL || arg->next != NULL) {
                error_at(ctx, expr->span,
                         "IR lowering: slice expects one array local");
                return no_value();
            }
            const CcExpression *source = arg->value;
            while (source->kind == CC_AST_EXPRESSION_PARENTHESIZED)
                source = source->parenthesized_inner;
            if (source->kind != CC_AST_EXPRESSION_IDENTIFIER) {
                error_at(ctx, source->span,
                         "IR lowering: slice needs local array");
                return no_value();
            }
            LowerVar *var = find_variable(ctx, source->identifier_name);
            const CcIrTypeInfo *array = var == NULL ? NULL
                : cc_ir_type_info(ctx->module, slot_type(ctx, var->slot));
            if (array == NULL || array->kind != CC_IR_TYPE_ARRAY) {
                error_at(ctx, source->span,
                         "IR lowering: slice needs fixed array");
                return no_value();
            }
            CcIrType slice = intern_type(ctx, CC_IR_TYPE_SLICE, array->inner,
                                         var->mutable ? 0U : 1U, NULL,
                                         expr->span);
            if (ctx->failed || (expected != CC_IR_NONE && expected != slice)) {
                error_at(ctx, expr->span,
                         "IR lowering: slice type mismatch");
                return no_value();
            }
            CcIrInstruction *node = NULL;
            Value result = append(ctx, CC_IR_MAKE_SLICE, slice,
                                  expr->span, &node);
            if (node != NULL) {
                node->slot = var->slot;
                node->arg_count = array->count;
            }
            return result;
        }
        if (expr->call.callee->kind != CC_AST_EXPRESSION_IDENTIFIER) {
            error_at(ctx, expr->span, "IR lowering: indirect call unsupported");
            return no_value();
        }
        CcIrFunction *target = find_function(ctx,
                                             expr->call.callee->identifier_name);
        if (target == NULL) {
            error_at(ctx, expr->span, "IR lowering: unknown function");
            return no_value();
        }
        uint32_t count = 0;
        for (CcCallArgument *arg = expr->call.arguments; arg != NULL;
             arg = arg->next) count++;
        if (count != target->param_count) {
            error_at(ctx, expr->span, "IR lowering: wrong argument count");
            return no_value();
        }
        uint32_t *args = NULL;
        uint32_t *slots = NULL;
        if (count != 0) {
            args = new_object(ctx, sizeof(*args) * count, expr->span);
            slots = new_object(ctx, sizeof(*slots) * count, expr->span);
            if (args == NULL || slots == NULL) return no_value();
        }
        uint32_t index = 0;
        for (CcCallArgument *arg = expr->call.arguments; arg != NULL;
             arg = arg->next) {
            Value value = lower_expr(ctx, arg->value,
                                     target->param_types[index]);
            if (ctx->failed || value.type != target->param_types[index]) {
                error_at(ctx, arg->value->span,
                         "IR lowering: call argument type mismatch");
                return no_value();
            }
            slots[index] = make_slot(ctx, value.type, arg->value->span);
            if (ctx->failed ||
                !store(ctx, slots[index], value, arg->value->span))
                return no_value();
            index++;
        }
        for (uint32_t p = 0; p < count; p++) {
            Value value = load(ctx, slots[p], expr->span);
            if (ctx->failed) return no_value();
            args[p] = value.reg;
        }
        CcIrInstruction *node = NULL;
        Value result = append(ctx, CC_IR_CALL, target->result, expr->span, &node);
        if (node != NULL) {
            node->callee = target->id;
            node->arg_count = count;
            node->args = args;
        }
        return result;
    }
    default:
        error_at(ctx, expr->span,
                 "IR lowering: expression not yet supported by C backend");
        return no_value();
    }
}

static bool compound_opcode(CcAssignmentOperator operator, CcIrOpcode *out);

static bool lower_pointer_assignment(Lower *ctx, const CcStatement *node)
{
    const CcExpression *target = node->assignment.target;
    Value pointer = lower_expr(ctx, target->unary.operand, CC_IR_NONE);
    if (ctx->failed) return false;
    const CcIrTypeInfo *info =
        cc_ir_type_info(ctx->module, pointer.type);
    if (info == NULL || info->kind != CC_IR_TYPE_POINTER ||
        info->pointer_const) {
        error_at(ctx, target->span,
                 "IR lowering: pointer store requires mutable pointee");
        return false;
    }
    CcIrType inner = info->inner;
    uint32_t pointer_slot = make_slot(ctx, pointer.type, target->span);
    if (ctx->failed || !store(ctx, pointer_slot, pointer, target->span))
        return false;
    bool compound = node->assignment.operator != CC_AST_ASSIGNMENT_ASSIGN;
    uint32_t old_slot = CC_IR_NONE;
    CcIrOpcode opcode = CC_IR_ADD;
    if (compound) {
        if (inner != CC_IR_I32 ||
            !compound_opcode(node->assignment.operator, &opcode)) {
            error_at(ctx, target->span,
                     "IR lowering: compound pointer store requires i32");
            return false;
        }
        CcIrInstruction *read = NULL;
        Value old = append(ctx, CC_IR_DEREF_PTR, inner,
                           target->span, &read);
        if (read == NULL) return false;
        read->lhs = pointer.reg;
        old_slot = make_slot(ctx, inner, target->span);
        if (ctx->failed || !store(ctx, old_slot, old, target->span))
            return false;
    }
    Value rhs = lower_expr(ctx, node->assignment.value, inner);
    if (ctx->failed || rhs.type != inner) {
        error_at(ctx, node->span,
                 "IR lowering: pointer store type mismatch");
        return false;
    }
    if (compound) {
        Value old = load(ctx, old_slot, node->span);
        if (ctx->failed) return false;
        CcIrInstruction *operation = NULL;
        Value combined = append(ctx, opcode, CC_IR_I32,
                                node->span, &operation);
        if (operation == NULL) return false;
        operation->lhs = old.reg;
        operation->rhs = rhs.reg;
        rhs = combined;
    }
    pointer = load(ctx, pointer_slot, node->span);
    if (ctx->failed) return false;
    CcIrInstruction *write = NULL;
    append(ctx, CC_IR_STORE_PTR, CC_IR_VOID, node->span, &write);
    if (write == NULL) return false;
    write->lhs = pointer.reg;
    write->rhs = rhs.reg;
    return true;
}

static bool lower_block(Lower *ctx, const CcBlock *block, bool scope);

static bool lower_if(Lower *ctx, const CcIf *node)
{
    Value condition = lower_expr(ctx, node->condition, CC_IR_BOOL);
    if (ctx->failed) return false;
    CcIrBlock *yes = new_block(ctx, node->span);
    CcIrBlock *no = new_block(ctx, node->span);
    CcIrBlock *join = new_block(ctx, node->span);
    if (ctx->failed) return false;
    branch(ctx, condition, yes, no, node->span);
    ctx->current = yes;
    if (!lower_block(ctx, node->then_block, true)) return false;
    if (!ctx->current->terminated) jump(ctx, join, node->span);
    ctx->current = no;
    if (node->else_if != NULL) {
        if (!lower_if(ctx, node->else_if)) return false;
    } else if (node->else_block != NULL &&
               !lower_block(ctx, node->else_block, true)) return false;
    if (!ctx->current->terminated) jump(ctx, join, node->span);
    ctx->current = join;
    return !ctx->failed;
}

static bool lower_pattern(Lower *ctx, const CcPattern *pattern, Value value,
                          bool mutable)
{
    switch (pattern->kind) {
    case CC_AST_PATTERN_NAME: {
        uint32_t slot = make_slot(ctx, value.type, pattern->span);
        return !ctx->failed && store(ctx, slot, value, pattern->span) &&
               bind_variable(ctx, pattern->name, slot, mutable);
    }
    case CC_AST_PATTERN_PARENTHESIZED:
        return lower_pattern(ctx, pattern->parenthesized_inner, value,
                             mutable);
    case CC_AST_PATTERN_TUPLE: {
        const CcIrTypeInfo *info = cc_ir_type_info(ctx->module, value.type);
        if (info == NULL || info->kind != CC_IR_TYPE_TUPLE) {
            error_at(ctx, pattern->span,
                     "IR lowering: destructuring requires tuple");
            return false;
        }
        uint32_t index = 0;
        for (CcTuplePatternElement *part = pattern->tuple_elements;
             part != NULL; part = part->next) {
            if (index >= info->count) {
                error_at(ctx, pattern->span,
                         "IR lowering: destructuring arity mismatch");
                return false;
            }
            CcIrInstruction *instruction = NULL;
            Value element = append(ctx, CC_IR_GET_TUPLE,
                info->members[index], part->pattern->span, &instruction);
            if (instruction == NULL) return false;
            instruction->lhs = value.reg;
            instruction->slot = index;
            if (!lower_pattern(ctx, part->pattern, element, mutable))
                return false;
            index++;
        }
        if (index != info->count) {
            error_at(ctx, pattern->span,
                     "IR lowering: destructuring arity mismatch");
            return false;
        }
        return true;
    }
    }
    error_at(ctx, pattern->span, "IR lowering: unknown binding pattern");
    return false;
}

static bool lower_match(Lower *ctx, const CcMatch *match)
{
    Value scrutinee = lower_expr(ctx, match->scrutinee, CC_IR_NONE);
    if (ctx->failed) return false;
    const CcIrTypeInfo *type =
        cc_ir_type_info(ctx->module, scrutinee.type);
    if (type == NULL || type->kind != CC_IR_TYPE_OPTIONAL) {
        error_at(ctx, match->scrutinee->span,
                 "IR lowering: match requires optional");
        return false;
    }
    CcIrType inner = type->inner;
    uint32_t scrutinee_slot = make_slot(ctx, scrutinee.type, match->span);
    if (ctx->failed ||
        !store(ctx, scrutinee_slot, scrutinee, match->span)) return false;
    const CcMatchArm *some = NULL;
    const CcMatchArm *none = NULL;
    for (const CcMatchArm *arm = match->arms; arm != NULL;
         arm = arm->next) {
        if (arm->is_some) some = arm;
        else none = arm;
    }
    if (some == NULL || none == NULL) {
        error_at(ctx, match->span, "IR lowering: non-exhaustive optional match");
        return false;
    }
    CcIrBlock *some_block = new_block(ctx, some->span);
    CcIrBlock *none_block = new_block(ctx, none->span);
    CcIrBlock *join = new_block(ctx, match->span);
    if (ctx->failed) return false;
    scrutinee = load(ctx, scrutinee_slot, match->span);
    if (ctx->failed) return false;
    CcIrInstruction *test = NULL;
    Value has = append(ctx, CC_IR_OPTIONAL_HAS, CC_IR_BOOL,
                       match->scrutinee->span, &test);
    if (test == NULL) return false;
    test->lhs = scrutinee.reg;
    branch(ctx, has, some_block, none_block, match->span);

    ctx->current = some_block;
    scrutinee = load(ctx, scrutinee_slot, some->span);
    if (ctx->failed) return false;
    CcIrInstruction *unwrap = NULL;
    Value payload = append(ctx, CC_IR_OPTIONAL_UNWRAP, inner,
                           some->span, &unwrap);
    if (unwrap == NULL) return false;
    unwrap->lhs = scrutinee.reg;
    LowerVar *saved = ctx->variables;
    bool ok = lower_pattern(ctx, some->payload, payload, false) &&
              lower_block(ctx, some->body, true);
    ctx->variables = saved;
    if (!ok) return false;
    if (!ctx->current->terminated) jump(ctx, join, some->span);

    ctx->current = none_block;
    if (!lower_block(ctx, none->body, true)) return false;
    if (!ctx->current->terminated) jump(ctx, join, none->span);
    ctx->current = join;
    return !ctx->failed;
}

static bool compound_opcode(CcAssignmentOperator operator, CcIrOpcode *out)
{
    switch (operator) {
    case CC_AST_ASSIGNMENT_PLUS: *out = CC_IR_ADD; return true;
    case CC_AST_ASSIGNMENT_MINUS: *out = CC_IR_SUB; return true;
    case CC_AST_ASSIGNMENT_STAR: *out = CC_IR_MUL; return true;
    case CC_AST_ASSIGNMENT_SLASH: *out = CC_IR_DIV; return true;
    case CC_AST_ASSIGNMENT_PERCENT: *out = CC_IR_MOD; return true;
    case CC_AST_ASSIGNMENT_AMP: *out = CC_IR_BIT_AND; return true;
    case CC_AST_ASSIGNMENT_PIPE: *out = CC_IR_BIT_OR; return true;
    case CC_AST_ASSIGNMENT_CARET: *out = CC_IR_BIT_XOR; return true;
    default: return false;
    }
}

static bool lower_slice_assignment(Lower *ctx, const CcStatement *node,
                                   uint32_t source_slot,
                                   const CcIrTypeInfo *slice)
{
    const CcExpression *target = node->assignment.target;
    Value view = load(ctx, source_slot, target->span);
    uint32_t view_slot = make_slot(ctx, view.type, target->span);
    if (ctx->failed || !store(ctx, view_slot, view, target->span))
        return false;
    Value index = lower_expr(ctx, target->index.index, CC_IR_I32);
    if (ctx->failed || index.type != CC_IR_I32) {
        error_at(ctx, target->index.index->span,
                 "IR lowering: slice index requires i32");
        return false;
    }
    uint32_t index_slot = make_slot(ctx, CC_IR_I32, target->span);
    if (ctx->failed || !store(ctx, index_slot, index, target->span))
        return false;
    bool compound = node->assignment.operator != CC_AST_ASSIGNMENT_ASSIGN;
    CcIrOpcode operation = CC_IR_ADD;
    uint32_t old_slot = CC_IR_NONE;
    if (compound) {
        if (slice->inner != CC_IR_I32 ||
            !compound_opcode(node->assignment.operator, &operation)) {
            error_at(ctx, target->span,
                     "IR lowering: compound slice store requires i32");
            return false;
        }
        view = load(ctx, view_slot, target->span);
        index = load(ctx, index_slot, target->span);
        if (ctx->failed) return false;
        CcIrInstruction *read = NULL;
        Value old = append(ctx, CC_IR_GET_SLICE, slice->inner,
                           target->span, &read);
        if (read == NULL) return false;
        read->lhs = view.reg;
        read->rhs = index.reg;
        old_slot = make_slot(ctx, slice->inner, target->span);
        if (ctx->failed || !store(ctx, old_slot, old, target->span))
            return false;
    }
    Value rhs = lower_expr(ctx, node->assignment.value, slice->inner);
    if (ctx->failed || rhs.type != slice->inner) {
        error_at(ctx, node->span,
                 "IR lowering: slice store type mismatch");
        return false;
    }
    if (compound) {
        Value old = load(ctx, old_slot, node->span);
        if (ctx->failed) return false;
        CcIrInstruction *arithmetic = NULL;
        Value updated = append(ctx, operation, CC_IR_I32,
                               node->span, &arithmetic);
        if (arithmetic == NULL) return false;
        arithmetic->lhs = old.reg;
        arithmetic->rhs = rhs.reg;
        rhs = updated;
    }
    view = load(ctx, view_slot, node->span);
    index = load(ctx, index_slot, node->span);
    if (ctx->failed) return false;
    CcIrInstruction *write = NULL;
    append(ctx, CC_IR_STORE_SLICE, CC_IR_VOID, node->span, &write);
    if (write == NULL) return false;
    write->lhs = view.reg;
    write->rhs = index.reg;
    write->slot = rhs.reg; /* Register holding the new element value. */
    return true;
}

static bool lower_index_assignment(Lower *ctx, const CcStatement *node)
{
    const CcExpression *target = node->assignment.target;
    if (target->index.operand->kind != CC_AST_EXPRESSION_IDENTIFIER) {
        error_at(ctx, target->span,
                 "IR lowering: indexed assignment needs local array");
        return false;
    }
    LowerVar *var = find_variable(ctx,
                                  target->index.operand->identifier_name);
    if (var == NULL) {
        error_at(ctx, target->span, "IR lowering: unknown array binding");
        return false;
    }
    const CcIrTypeInfo *array =
        cc_ir_type_info(ctx->module, slot_type(ctx, var->slot));
    if (array != NULL && array->kind == CC_IR_TYPE_SLICE) {
        if (array->slice_const) {
            error_at(ctx, target->span,
                     "IR lowering: cannot write through const slice");
            return false;
        }
        return lower_slice_assignment(ctx, node, var->slot, array);
    }
    if (array == NULL || array->kind != CC_IR_TYPE_ARRAY) {
        error_at(ctx, target->span,
                 "IR lowering: indexed assignment needs fixed array");
        return false;
    }
    Value index = lower_expr(ctx, target->index.index, CC_IR_I32);
    if (ctx->failed) return false;
    if (index.type != CC_IR_I32) {
        error_at(ctx, target->span, "IR lowering: array index requires i32");
        return false;
    }
    uint32_t index_slot = make_slot(ctx, CC_IR_I32, target->span);
    if (ctx->failed || !store(ctx, index_slot, index, target->span))
        return false;

    uint32_t old_slot = CC_IR_NONE;
    CcIrOpcode opcode = CC_IR_ADD;
    bool compound = node->assignment.operator != CC_AST_ASSIGNMENT_ASSIGN;
    if (compound) {
        if (array->inner != CC_IR_I32 ||
            !compound_opcode(node->assignment.operator, &opcode)) {
            error_at(ctx, target->span,
                     "IR lowering: indexed compound assignment requires i32");
            return false;
        }
        Value source = load(ctx, var->slot, target->span);
        index = load(ctx, index_slot, target->span);
        if (ctx->failed) return false;
        CcIrInstruction *get = NULL;
        Value old = append(ctx, CC_IR_GET_ARRAY, array->inner,
                           target->span, &get);
        if (get == NULL) return false;
        get->lhs = source.reg;
        get->rhs = index.reg;
        get->arg_count = array->count;
        old_slot = make_slot(ctx, array->inner, target->span);
        if (ctx->failed || !store(ctx, old_slot, old, target->span))
            return false;
    }
    Value rhs = lower_expr(ctx, node->assignment.value, array->inner);
    if (ctx->failed) return false;
    if (rhs.type != array->inner) {
        error_at(ctx, node->span,
                 "IR lowering: array assignment value mismatch");
        return false;
    }
    if (compound) {
        Value old = load(ctx, old_slot, node->span);
        if (ctx->failed) return false;
        CcIrInstruction *operation = NULL;
        Value updated = append(ctx, opcode, CC_IR_I32,
                               node->span, &operation);
        if (operation == NULL) return false;
        operation->lhs = old.reg;
        operation->rhs = rhs.reg;
        rhs = updated;
    }
    index = load(ctx, index_slot, target->span);
    if (ctx->failed) return false;
    CcIrInstruction *set = NULL;
    append(ctx, CC_IR_STORE_INDEX, CC_IR_VOID, node->span, &set);
    if (set == NULL) return false;
    set->slot = var->slot;
    set->lhs = index.reg;
    set->rhs = rhs.reg;
    set->arg_count = array->count;
    return true;
}

static bool lower_field_assignment(Lower *ctx, const CcStatement *node)
{
    const CcExpression *target = node->assignment.target;
    if (target->member.operand->kind != CC_AST_EXPRESSION_IDENTIFIER) {
        error_at(ctx, target->span,
                 "IR lowering: field assignment requires named local");
        return false;
    }
    LowerVar *variable =
        find_variable(ctx, target->member.operand->identifier_name);
    if (variable == NULL) {
        error_at(ctx, target->span, "IR lowering: unknown struct binding");
        return false;
    }
    const CcIrTypeInfo *info =
        cc_ir_type_info(ctx->module, slot_type(ctx, variable->slot));
    uint32_t index = struct_field_index(ctx, info, target->member.name);
    if (index == CC_IR_NONE) {
        error_at(ctx, target->span,
                 "IR lowering: field assignment requires struct field");
        return false;
    }
    CcIrType type = info->members[index];
    CcIrOpcode opcode = CC_IR_ADD;
    bool compound = node->assignment.operator != CC_AST_ASSIGNMENT_ASSIGN;
    uint32_t old_slot = CC_IR_NONE;
    if (compound) {
        if (type != CC_IR_I32 ||
            !compound_opcode(node->assignment.operator, &opcode)) {
            error_at(ctx, target->span,
                     "IR lowering: compound field assignment requires i32");
            return false;
        }
        Value object = load(ctx, variable->slot, target->span);
        if (ctx->failed) return false;
        CcIrInstruction *get = NULL;
        Value old = append(ctx, CC_IR_GET_STRUCT, type,
                           target->span, &get);
        if (get == NULL) return false;
        get->lhs = object.reg;
        get->slot = index;
        old_slot = make_slot(ctx, type, target->span);
        if (ctx->failed || !store(ctx, old_slot, old, target->span))
            return false;
    }
    Value rhs = lower_expr(ctx, node->assignment.value, type);
    if (ctx->failed || rhs.type != type) {
        error_at(ctx, node->span, "IR lowering: struct field type mismatch");
        return false;
    }
    if (compound) {
        Value old = load(ctx, old_slot, node->span);
        if (ctx->failed) return false;
        CcIrInstruction *operation = NULL;
        Value result = append(ctx, opcode, CC_IR_I32,
                              node->span, &operation);
        if (operation == NULL) return false;
        operation->lhs = old.reg;
        operation->rhs = rhs.reg;
        rhs = result;
    }
    CcIrInstruction *set = NULL;
    append(ctx, CC_IR_SET_FIELD, CC_IR_VOID, node->span, &set);
    if (set == NULL) return false;
    set->slot = variable->slot;
    set->arg_count = index;
    set->lhs = rhs.reg;
    return true;
}

static bool lower_statement(Lower *ctx, const CcStatement *node)
{
    switch (node->kind) {
    case CC_AST_STATEMENT_LET: {
        CcIrType declared = CC_IR_VOID;
        if (node->let.type != NULL &&
            !type_of(ctx, node->let.type, &declared, node->span))
            return false;
        Value value = lower_expr(ctx, node->let.initializer,
                                 node->let.type == NULL ? CC_IR_NONE : declared);
        if (ctx->failed) return false;
        if (declared != CC_IR_VOID && declared != value.type) {
            error_at(ctx, node->span, "IR lowering: binding type mismatch");
            return false;
        }
        return !ctx->failed &&
               lower_pattern(ctx, node->let.pattern, value,
                             node->let.mutable);
    }
    case CC_AST_STATEMENT_RETURN: {
        Value value = no_value();
        if (node->return_expression != NULL) {
            value = lower_expr(ctx, node->return_expression,
                                ctx->function->result);
            if (ctx->failed) return false;
        }
        if (value.type != ctx->function->result) {
            error_at(ctx, node->span, "IR lowering: return type mismatch");
            return false;
        }
        ctx->current->terminated = true;
        ctx->current->terminator = CC_IR_RET;
        ctx->current->return_value = value.reg;
        return true;
    }
    case CC_AST_STATEMENT_EXPRESSION: {
        lower_expr(ctx, node->expression, CC_IR_NONE);
        return !ctx->failed;
    }
    case CC_AST_STATEMENT_ASSIGNMENT: {
        if (node->assignment.target->kind == CC_AST_EXPRESSION_UNARY &&
            node->assignment.target->unary.operator ==
                CC_AST_UNARY_OPERATOR_STAR)
            return lower_pointer_assignment(ctx, node);
        if (node->assignment.target->kind == CC_AST_EXPRESSION_MEMBER)
            return lower_field_assignment(ctx, node);
        if (node->assignment.target->kind == CC_AST_EXPRESSION_INDEX)
            return lower_index_assignment(ctx, node);
        if (node->assignment.target->kind != CC_AST_EXPRESSION_IDENTIFIER) {
            error_at(ctx, node->span,
                     "IR lowering: complex assignment targets unsupported");
            return false;
        }
        LowerVar *variable =
            find_variable(ctx, node->assignment.target->identifier_name);
        if (variable == NULL) {
            error_at(ctx, node->span, "IR lowering: unknown assignment target");
            return false;
        }
        CcIrType type = slot_type(ctx, variable->slot);
        if (node->assignment.operator == CC_AST_ASSIGNMENT_ASSIGN) {
            Value value = lower_expr(ctx, node->assignment.value, type);
            return !ctx->failed &&
                   store(ctx, variable->slot, value, node->span);
        }
        if (type != CC_IR_I32) {
            error_at(ctx, node->span,
                     "IR lowering: compound assignment currently requires i32");
            return false;
        }
        CcIrOpcode opcode;
        if (!compound_opcode(node->assignment.operator, &opcode)) {
            error_at(ctx, node->span,
                     "IR lowering: compound shift not yet supported");
            return false;
        }
        /* RHS may have branching calls/short-circuit logic: retain the old
         * target value across block changes, then apply the operation once. */
        uint32_t old_slot = make_slot(ctx, type, node->span);
        Value old = load(ctx, variable->slot, node->span);
        if (ctx->failed || !store(ctx, old_slot, old, node->span))
            return false;
        Value rhs = lower_expr(ctx, node->assignment.value, type);
        if (ctx->failed || rhs.type != type) {
            error_at(ctx, node->span, "IR lowering: assignment type mismatch");
            return false;
        }
        old = load(ctx, old_slot, node->span);
        if (ctx->failed) return false;
        CcIrInstruction *instruction = NULL;
        Value result = append(ctx, opcode, type, node->span, &instruction);
        if (instruction == NULL) return false;
        instruction->lhs = old.reg;
        instruction->rhs = rhs.reg;
        return store(ctx, variable->slot, result, node->span);
    }
    case CC_AST_STATEMENT_IF:
        return lower_if(ctx, node->if_statement);
    case CC_AST_STATEMENT_MATCH:
        return lower_match(ctx, node->match_statement);
    case CC_AST_STATEMENT_UNSAFE:
        return lower_block(ctx, node->unsafe_block, true);
    case CC_AST_STATEMENT_WHILE: {
        CcIrBlock *head = new_block(ctx, node->span);
        CcIrBlock *body = new_block(ctx, node->span);
        CcIrBlock *exit = new_block(ctx, node->span);
        if (ctx->failed) return false;
        jump(ctx, head, node->span);
        ctx->current = head;
        Value condition = lower_expr(ctx, node->while_statement->condition,
                                     CC_IR_BOOL);
        if (ctx->failed) return false;
        branch(ctx, condition, body, exit, node->span);
        LowerLoop loop = { exit->id, head->id, ctx->loop };
        ctx->loop = &loop;
        ctx->current = body;
        bool ok = lower_block(ctx, node->while_statement->body, true);
        ctx->loop = loop.previous;
        if (!ok) return false;
        if (!ctx->current->terminated) jump(ctx, head, node->span);
        ctx->current = exit;
        return !ctx->failed;
    }
    case CC_AST_STATEMENT_LOOP: {
        CcIrBlock *body = new_block(ctx, node->span);
        CcIrBlock *exit = new_block(ctx, node->span);
        if (ctx->failed) return false;
        jump(ctx, body, node->span);
        LowerLoop loop = { exit->id, body->id, ctx->loop };
        ctx->loop = &loop;
        ctx->current = body;
        bool ok = lower_block(ctx, node->loop_statement->body, true);
        ctx->loop = loop.previous;
        if (!ok) return false;
        if (!ctx->current->terminated) jump(ctx, body, node->span);
        ctx->current = exit;
        return !ctx->failed;
    }
    case CC_AST_STATEMENT_FOR: {
        const CcFor *for_node = node->for_statement;
        const CcExpression *range = for_node->range;
        while (range->kind == CC_AST_EXPRESSION_PARENTHESIZED)
            range = range->parenthesized_inner;
        if (range->kind != CC_AST_EXPRESSION_BINARY ||
            (range->binary.operator != CC_AST_BINARY_OPERATOR_RANGE &&
             range->binary.operator != CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE)) {
            error_at(ctx, for_node->range->span,
                     "IR lowering: for requires a direct integer range");
            return false;
        }
        bool inclusive =
            range->binary.operator == CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE;
        uint32_t index_slot = make_slot(ctx, CC_IR_I32, for_node->span);
        uint32_t end_slot = make_slot(ctx, CC_IR_I32, for_node->span);
        if (ctx->failed) return false;
        Value start = lower_expr(ctx, range->binary.left, CC_IR_I32);
        if (ctx->failed || start.type != CC_IR_I32 ||
            !store(ctx, index_slot, start, range->span)) {
            error_at(ctx, range->span, "IR lowering: for requires i32 bounds");
            return false;
        }
        Value end = lower_expr(ctx, range->binary.right, CC_IR_I32);
        if (ctx->failed || end.type != CC_IR_I32 ||
            !store(ctx, end_slot, end, range->span)) {
            error_at(ctx, range->span, "IR lowering: for requires i32 bounds");
            return false;
        }
        CcIrBlock *head = new_block(ctx, node->span);
        CcIrBlock *body = new_block(ctx, node->span);
        CcIrBlock *increment = new_block(ctx, node->span);
        CcIrBlock *exit = new_block(ctx, node->span);
        CcIrBlock *step = inclusive ? new_block(ctx, node->span) : increment;
        if (ctx->failed) return false;
        jump(ctx, head, node->span);

        ctx->current = head;
        Value index = load(ctx, index_slot, node->span);
        Value limit = load(ctx, end_slot, node->span);
        CcIrInstruction *test = NULL;
        Value condition = append(ctx, inclusive ? CC_IR_LE : CC_IR_LT,
                                 CC_IR_BOOL, node->span, &test);
        if (ctx->failed) return false;
        test->lhs = index.reg;
        test->rhs = limit.reg;
        branch(ctx, condition, body, exit, node->span);

        LowerVar *saved_variables = ctx->variables;
        if (!bind_variable(ctx, for_node->name, index_slot, false))
            return false;
        LowerLoop loop = { exit->id, increment->id, ctx->loop };
        ctx->loop = &loop;
        ctx->current = body;
        bool ok = lower_block(ctx, for_node->body, true);
        ctx->loop = loop.previous;
        ctx->variables = saved_variables;
        if (!ok) return false;
        if (!ctx->current->terminated) jump(ctx, increment, node->span);

        ctx->current = increment;
        if (inclusive) {
            index = load(ctx, index_slot, node->span);
            limit = load(ctx, end_slot, node->span);
            CcIrInstruction *at_end = NULL;
            Value done = append(ctx, CC_IR_EQ, CC_IR_BOOL, node->span,
                                &at_end);
            if (ctx->failed) return false;
            at_end->lhs = index.reg;
            at_end->rhs = limit.reg;
            branch(ctx, done, exit, step, node->span);
            ctx->current = step;
        }
        index = load(ctx, index_slot, node->span);
        Value one = constant(ctx, CC_IR_I32, 1, node->span);
        CcIrInstruction *add = NULL;
        Value next = append(ctx, CC_IR_ADD, CC_IR_I32, node->span, &add);
        if (ctx->failed) return false;
        add->lhs = index.reg;
        add->rhs = one.reg;
        if (!store(ctx, index_slot, next, node->span)) return false;
        jump(ctx, head, node->span);
        ctx->current = exit;
        return !ctx->failed;
    }
    case CC_AST_STATEMENT_BREAK:
    case CC_AST_STATEMENT_CONTINUE: {
        if (ctx->loop == NULL) {
            error_at(ctx, node->span, "IR lowering: break outside loop");
            return false;
        }
        ctx->current->terminated = true;
        ctx->current->terminator = CC_IR_JUMP;
        ctx->current->target = node->kind == CC_AST_STATEMENT_BREAK
            ? ctx->loop->break_target : ctx->loop->continue_target;
        return true;
    }
    default:
        error_at(ctx, node->span,
                 "IR lowering: statement not yet supported by C backend");
        return false;
    }
}

static bool lower_block(Lower *ctx, const CcBlock *block, bool scope)
{
    LowerVar *saved = ctx->variables;
    for (CcStatement *statement = block->first; statement != NULL;
         statement = statement->next) {
        if (ctx->current->terminated) break;
        if (!lower_statement(ctx, statement)) return false;
    }
    if (scope) ctx->variables = saved;
    return !ctx->failed;
}

bool cc_ir_lower(const CcSourceFile *file,
                 const CcFunctionDeclaration *program,
                 CcArena *arena, CcIrModule **out, CcIrError *error)
{
    if (out != NULL) *out = NULL;
    if (error == NULL) return false;
    error->span = (CcSourceSpan){ CC_FILE_ID_INVALID, 0, 0 };
    error->message = NULL;
    if (file == NULL || arena == NULL || out == NULL) {
        error->message = "invalid IR lowering arguments";
        return false;
    }
    Lower ctx = {0};
    ctx.file = file;
    ctx.arena = arena;
    ctx.error = error;
    CcSourceSpan fallback = { 0, 0, 0 };
    ctx.module = new_object(&ctx, sizeof(*ctx.module), fallback);
    if (ctx.module == NULL) return false;
    memset(ctx.module, 0, sizeof(*ctx.module));
    ctx.module->file = file;
    ctx.module->type_count = 3;

    for (const CcFunctionDeclaration *decl = program; decl != NULL;
         decl = decl->next) {
        if (decl->struct_decl == NULL) continue;
        LowerStruct *entry = new_object(&ctx, sizeof(*entry), decl->span);
        if (entry == NULL) return false;
        entry->decl = decl->struct_decl;
        entry->id = CC_IR_NONE;
        entry->state = 0;
        entry->next = ctx.structs;
        ctx.structs = entry;
    }
    for (LowerStruct *entry = ctx.structs; entry != NULL;
         entry = entry->next)
        if (intern_struct(&ctx, entry) == CC_IR_NONE) return false;

    CcIrFunction **link = &ctx.module->functions;
    for (const CcFunctionDeclaration *decl = program; decl != NULL;
         decl = decl->next) {
        if (decl->struct_decl != NULL) continue;
        CcIrFunction *function = new_object(&ctx, sizeof(*function), decl->span);
        if (function == NULL) return false;
        memset(function, 0, sizeof(*function));
        function->id = ctx.module->function_count++;
        function->name = decl->name;
        function->span = decl->span;
        ctx.function = function;
        if (!type_of(&ctx, decl->return_type, &function->result, decl->span))
            return false;
        for (CcParameter *p = decl->parameters; p != NULL; p = p->next)
            function->param_count++;
        if (function->param_count != 0) {
            function->param_types = new_object(&ctx,
                sizeof(*function->param_types) * function->param_count,
                decl->span);
            if (function->param_types == NULL) return false;
        }
        uint32_t index = 0;
        for (CcParameter *p = decl->parameters; p != NULL; p = p->next) {
            if (!type_of(&ctx, p->type, &function->param_types[index],
                         p->span))
                return false;
            if (function->param_types[index] == CC_IR_VOID) {
                error_at(&ctx, p->span, "IR lowering: void parameter");
                return false;
            }
            make_slot(&ctx, function->param_types[index++], p->span);
            if (ctx.failed) return false;
        }
        *link = function;
        link = &function->next;
    }

    ctx.function = ctx.module->functions;
    for (const CcFunctionDeclaration *decl = program; decl != NULL;
         decl = decl->next) {
        if (decl->struct_decl != NULL) continue;
        ctx.variables = NULL;
        ctx.loop = NULL;
        uint32_t index = 0;
        for (CcParameter *p = decl->parameters; p != NULL; p = p->next)
            if (!bind_variable(&ctx, p->name, index++, false)) return false;
        ctx.current = new_block(&ctx, decl->span);
        if (ctx.failed || !lower_block(&ctx, decl->body, false)) return false;
        if (!ctx.current->terminated) {
            ctx.current->terminated = true;
            if (ctx.function->result == CC_IR_VOID) {
                ctx.current->terminator = CC_IR_RET;
                ctx.current->return_value = CC_IR_NONE;
            } else {
                /* The semantic pass proves the source does return. This can be
                 * an unreachable join after fully returning if/else arms. */
                ctx.current->terminator = CC_IR_TRAP;
            }
        }
        ctx.function = ctx.function->next;
    }
    *out = ctx.module;
    return true;
}
