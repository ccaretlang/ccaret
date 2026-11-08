#include "ir/ir.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct LowerVar LowerVar;
typedef struct LowerLoop LowerLoop;

struct LowerVar {
    CcSourceSpan name;
    uint32_t slot;
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
    }
    error_at(ctx, node == NULL ? fallback : node->span,
             "IR backend currently supports only i32, bool and void");
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
    slot->next = NULL;
    if (ctx->function->last_slot == NULL)
        ctx->function->slots = slot;
    else
        ctx->function->last_slot->next = slot;
    ctx->function->last_slot = slot;
    return slot->id;
}

static bool bind_variable(Lower *ctx, CcSourceSpan name, uint32_t slot)
{
    LowerVar *variable = new_object(ctx, sizeof(*variable), name);
    if (variable == NULL) return false;
    variable->name = name;
    variable->slot = slot;
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

static Value lower_expr(Lower *ctx, const CcExpression *expr);

static Value lower_logic(Lower *ctx, const CcExpression *expr, bool and)
{
    Value left = lower_expr(ctx, expr->binary.left);
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
    Value right = lower_expr(ctx, expr->binary.right);
    if (ctx->failed) return no_value();
    if (!store(ctx, slot, right, expr->span)) return no_value();
    jump(ctx, join, expr->span);

    ctx->current = join;
    return load(ctx, slot, expr->span);
}

static Value lower_expr(Lower *ctx, const CcExpression *expr)
{
    if (ctx->failed) return no_value();
    switch (expr->kind) {
    case CC_AST_EXPRESSION_INTEGER_LITERAL:
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
        return lower_expr(ctx, expr->parenthesized_inner);
    case CC_AST_EXPRESSION_IDENTIFIER: {
        LowerVar *var = find_variable(ctx, expr->identifier_name);
        if (var == NULL) {
            error_at(ctx, expr->span, "IR lowering: unknown local variable");
            return no_value();
        }
        return load(ctx, var->slot, expr->span);
    }
    case CC_AST_EXPRESSION_UNARY: {
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
        else {
            error_at(ctx, expr->span,
                     "IR lowering: unary operator not yet supported");
            return no_value();
        }
        Value operand = lower_expr(ctx, expr->unary.operand);
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
        Value left = lower_expr(ctx, expr->binary.left);
        if (ctx->failed) return no_value();
        /* The right operand may contain short-circuit control flow.
         * Preserve the left value in a slot and reload it in the join block. */
        uint32_t slot = make_slot(ctx, left.type, expr->span);
        if (ctx->failed || !store(ctx, slot, left, expr->span))
            return no_value();
        Value right = lower_expr(ctx, expr->binary.right);
        if (ctx->failed) return no_value();
        if (left.type != right.type) {
            error_at(ctx, expr->span, "IR operand type mismatch");
            return no_value();
        }
        left = load(ctx, slot, expr->span);
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
            Value value = lower_expr(ctx, arg->value);
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

static bool lower_block(Lower *ctx, const CcBlock *block, bool scope);

static bool lower_if(Lower *ctx, const CcIf *node)
{
    Value condition = lower_expr(ctx, node->condition);
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

static bool lower_statement(Lower *ctx, const CcStatement *node)
{
    switch (node->kind) {
    case CC_AST_STATEMENT_LET: {
        if (node->let.pattern->kind != CC_AST_PATTERN_NAME) {
            error_at(ctx, node->let.pattern->span,
                     "IR lowering: tuple binding patterns not yet supported");
            return false;
        }
        CcIrType declared = CC_IR_VOID;
        if (node->let.type != NULL &&
            !type_of(ctx, node->let.type, &declared, node->span))
            return false;
        Value value = lower_expr(ctx, node->let.initializer);
        if (ctx->failed) return false;
        if (declared != CC_IR_VOID && declared != value.type) {
            error_at(ctx, node->span, "IR lowering: binding type mismatch");
            return false;
        }
        uint32_t slot = make_slot(ctx, value.type, node->span);
        return !ctx->failed &&
               store(ctx, slot, value, node->span) &&
               bind_variable(ctx, node->let.pattern->name, slot);
    }
    case CC_AST_STATEMENT_RETURN: {
        Value value = no_value();
        if (node->return_expression != NULL) {
            value = lower_expr(ctx, node->return_expression);
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
        lower_expr(ctx, node->expression);
        return !ctx->failed;
    }
    case CC_AST_STATEMENT_ASSIGNMENT: {
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
        if (node->assignment.operator != CC_AST_ASSIGNMENT_ASSIGN) {
            error_at(ctx, node->span,
                     "IR lowering: compound assignment not yet supported");
            return false;
        }
        Value value = lower_expr(ctx, node->assignment.value);
        return !ctx->failed && store(ctx, variable->slot, value, node->span);
    }
    case CC_AST_STATEMENT_IF:
        return lower_if(ctx, node->if_statement);
    case CC_AST_STATEMENT_WHILE: {
        CcIrBlock *head = new_block(ctx, node->span);
        CcIrBlock *body = new_block(ctx, node->span);
        CcIrBlock *exit = new_block(ctx, node->span);
        if (ctx->failed) return false;
        jump(ctx, head, node->span);
        ctx->current = head;
        Value condition = lower_expr(ctx, node->while_statement->condition);
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

    CcIrFunction **link = &ctx.module->functions;
    for (const CcFunctionDeclaration *decl = program; decl != NULL;
         decl = decl->next) {
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
         decl = decl->next, ctx.function = ctx.function->next) {
        ctx.variables = NULL;
        ctx.loop = NULL;
        uint32_t index = 0;
        for (CcParameter *p = decl->parameters; p != NULL; p = p->next)
            if (!bind_variable(&ctx, p->name, index++)) return false;
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
    }
    *out = ctx.module;
    return true;
}
