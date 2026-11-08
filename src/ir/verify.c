#include "ir/ir.h"

#include <stdlib.h>
#include <string.h>

static bool fail(CcIrError *error, CcSourceSpan span, const char *message)
{
    error->span = span;
    error->message = message;
    return false;
}

static const CcIrFunction *find_function(const CcIrModule *module,
                                         uint32_t id)
{
    for (const CcIrFunction *function = module->functions;
         function != NULL; function = function->next)
        if (function->id == id) return function;
    return NULL;
}

static bool valid_type(const CcIrModule *module, CcIrType id)
{
    return id < 3 ||
        (id < module->type_count && cc_ir_type_info(module, id) != NULL);
}

static bool use_register(CcIrError *error, const CcIrInstruction *instruction,
                         uint32_t id, CcIrType expected,
                         const CcIrType *types, const uint32_t *blocks,
                         const bool *defined, uint32_t count,
                         uint32_t block_id)
{
    if (id >= count || !defined[id] || types[id] != expected ||
        blocks[id] != block_id)
        return fail(error, instruction->span,
                    "IR verifier: undefined, cross-block or mistyped register");
    return true;
}

static bool validate_instruction(const CcIrModule *module,
                                  const CcIrInstruction *i,
                                  CcIrError *error, const CcIrType *slots,
                                  uint32_t slot_count, CcIrType *types,
                                  uint32_t *blocks, bool *defined,
                                  uint32_t register_count, uint32_t block_id)
{
#define USE(ID, TYPE) \
    use_register(error, i, (ID), (TYPE), types, blocks, defined, \
                 register_count, block_id)
    bool has_result = i->type != CC_IR_VOID;
    if (!valid_type(module, i->type) ||
        has_result != (i->result != CC_IR_NONE) ||
        (has_result &&
         (i->result >= register_count || defined[i->result]))) {
        return fail(error, i->span, "IR verifier: invalid result register");
    }

    switch (i->op) {
    case CC_IR_CONST_I32:
        if (i->type != CC_IR_I32)
            return fail(error, i->span, "IR verifier: bad i32 constant");
        break;
    case CC_IR_CONST_BOOL:
        if (i->type != CC_IR_BOOL ||
            (i->constant != 0 && i->constant != 1))
            return fail(error, i->span, "IR verifier: bad boolean constant");
        break;
    case CC_IR_LOAD:
        if (i->slot >= slot_count || i->type != slots[i->slot] ||
            i->type == CC_IR_VOID)
            return fail(error, i->span, "IR verifier: bad load slot/type");
        break;
    case CC_IR_STORE:
        if (i->type != CC_IR_VOID || i->slot >= slot_count ||
            slots[i->slot] == CC_IR_VOID ||
            !USE(i->lhs, slots[i->slot]))
            return fail(error, i->span, "IR verifier: bad store");
        break;
    case CC_IR_NEG:
    case CC_IR_NOT:
    case CC_IR_BIT_NOT: {
        CcIrType operand = i->op == CC_IR_NOT ? CC_IR_BOOL : CC_IR_I32;
        if (i->type != operand || !USE(i->lhs, operand))
            return fail(error, i->span, "IR verifier: invalid unary operation");
        break;
    }
    case CC_IR_ADD:
    case CC_IR_SUB:
    case CC_IR_MUL:
    case CC_IR_DIV:
    case CC_IR_MOD:
    case CC_IR_WRAP_ADD:
    case CC_IR_WRAP_SUB:
    case CC_IR_WRAP_MUL:
    case CC_IR_BIT_AND:
    case CC_IR_BIT_OR:
    case CC_IR_BIT_XOR:
    case CC_IR_LT:
    case CC_IR_LE:
    case CC_IR_GT:
    case CC_IR_GE: {
        bool compare = i->op == CC_IR_LT || i->op == CC_IR_LE ||
                       i->op == CC_IR_GT || i->op == CC_IR_GE;
        if (i->type != (compare ? CC_IR_BOOL : CC_IR_I32) ||
            !USE(i->lhs, CC_IR_I32) || !USE(i->rhs, CC_IR_I32))
            return fail(error, i->span, "IR verifier: invalid arithmetic");
        break;
    }
    case CC_IR_EQ:
    case CC_IR_NE:
        if (i->type != CC_IR_BOOL || i->lhs >= register_count ||
            !defined[i->lhs] ||
            (types[i->lhs] != CC_IR_I32 && types[i->lhs] != CC_IR_BOOL) ||
            !USE(i->lhs, types[i->lhs]) || !USE(i->rhs, types[i->lhs]))
            return fail(error, i->span, "IR verifier: invalid equality");
        break;
    case CC_IR_CALL: {
        const CcIrFunction *target = find_function(module, i->callee);
        if (target == NULL || target->result != i->type ||
            target->param_count != i->arg_count ||
            (i->arg_count != 0 && i->args == NULL))
            return fail(error, i->span, "IR verifier: invalid call signature");
        for (uint32_t a = 0; a < i->arg_count; a++)
            if (!USE(i->args[a], target->param_types[a]))
                return false;
        break;
    }
    case CC_IR_MAKE_TUPLE: {
        const CcIrTypeInfo *tuple = cc_ir_type_info(module, i->type);
        if (tuple == NULL || tuple->kind != CC_IR_TYPE_TUPLE ||
            tuple->count != i->arg_count ||
            (i->arg_count != 0 && i->args == NULL))
            return fail(error, i->span,
                        "IR verifier: invalid tuple constructor");
        for (uint32_t a = 0; a < i->arg_count; a++)
            if (!USE(i->args[a], tuple->members[a])) return false;
        break;
    }
    case CC_IR_GET_TUPLE: {
        if (i->lhs >= register_count || !defined[i->lhs])
            return fail(error, i->span,
                        "IR verifier: invalid tuple projection");
        CcIrType source = types[i->lhs];
        const CcIrTypeInfo *tuple = cc_ir_type_info(module, source);
        if (tuple == NULL || tuple->kind != CC_IR_TYPE_TUPLE ||
            i->slot >= tuple->count ||
            i->type != tuple->members[i->slot] ||
            !USE(i->lhs, source))
            return fail(error, i->span,
                        "IR verifier: invalid tuple projection");
        break;
    }
    case CC_IR_MAKE_SOME: {
        const CcIrTypeInfo *optional = cc_ir_type_info(module, i->type);
        if (optional == NULL || optional->kind != CC_IR_TYPE_OPTIONAL ||
            !USE(i->lhs, optional->inner))
            return fail(error, i->span,
                        "IR verifier: invalid some constructor");
        break;
    }
    case CC_IR_MAKE_NONE: {
        const CcIrTypeInfo *optional = cc_ir_type_info(module, i->type);
        if (optional == NULL || optional->kind != CC_IR_TYPE_OPTIONAL)
            return fail(error, i->span,
                        "IR verifier: invalid none constructor");
        break;
    }
    default:
        return fail(error, i->span, "IR verifier: unknown instruction");
    }

    if (has_result) {
        defined[i->result] = true;
        types[i->result] = i->type;
        blocks[i->result] = block_id;
    }
    return true;
#undef USE
}

bool cc_ir_verify(const CcIrModule *module, CcIrError *error)
{
    if (error == NULL) return false;
    error->message = NULL;
    error->span = (CcSourceSpan){ CC_FILE_ID_INVALID, 0, 0 };
    if (module == NULL || module->file == NULL)
        return fail(error, error->span, "IR verifier: invalid module");

    if (module->type_count < 3)
        return fail(error, error->span, "IR verifier: invalid type table");
    uint32_t type_id = 3;
    for (const CcIrTypeInfo *type = module->types; type != NULL;
         type = type->next) {
        if (type->id != type_id++ ||
            (type->kind != CC_IR_TYPE_TUPLE &&
             type->kind != CC_IR_TYPE_OPTIONAL))
            return fail(error, error->span, "IR verifier: invalid type table");
        if (type->kind == CC_IR_TYPE_OPTIONAL) {
            if (type->inner == CC_IR_VOID || type->inner >= type->id)
                return fail(error, error->span,
                            "IR verifier: invalid optional type");
        } else {
            if (type->count != 0 && type->members == NULL)
                return fail(error, error->span,
                            "IR verifier: invalid tuple type");
            for (uint32_t i = 0; i < type->count; i++)
                if (type->members[i] == CC_IR_VOID ||
                    type->members[i] >= type->id)
                    return fail(error, error->span,
                                "IR verifier: invalid tuple element type");
        }
        for (const CcIrTypeInfo *other = module->types; other != type;
             other = other->next) {
            if (type->kind != other->kind) continue;
            if (type->kind == CC_IR_TYPE_OPTIONAL) {
                if (type->inner == other->inner)
                    return fail(error, error->span,
                                "IR verifier: duplicate composite type");
            } else if (type->count == other->count &&
                       (type->count == 0 ||
                        memcmp(type->members, other->members,
                               type->count * sizeof(*type->members)) == 0)) {
                return fail(error, error->span,
                            "IR verifier: duplicate composite type");
            }
        }
    }
    if (type_id != module->type_count)
        return fail(error, error->span, "IR verifier: invalid type table");

    uint32_t function_count = 0;
    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        if (fn->id != function_count++)
            return fail(error, fn->span, "IR verifier: invalid function id");
    }
    if (function_count != module->function_count)
        return fail(error, error->span, "IR verifier: invalid function count");

    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        if (!valid_type(module, fn->result) ||
            fn->param_count > fn->slot_count ||
            (fn->param_count != 0 && fn->param_types == NULL) ||
            fn->block_count == 0)
            return fail(error, fn->span, "IR verifier: invalid function header");
        for (uint32_t p = 0; p < fn->param_count; p++)
            if (fn->param_types[p] == CC_IR_VOID ||
                !valid_type(module, fn->param_types[p]))
                return fail(error, fn->span, "IR verifier: void parameter");

        CcIrType *slot_types =
            calloc(fn->slot_count == 0 ? 1 : fn->slot_count,
                   sizeof(*slot_types));
        CcIrType *types =
            calloc(fn->register_count == 0 ? 1 : fn->register_count,
                   sizeof(*types));
        uint32_t *blocks =
            calloc(fn->register_count == 0 ? 1 : fn->register_count,
                   sizeof(*blocks));
        bool *defined =
            calloc(fn->register_count == 0 ? 1 : fn->register_count,
                   sizeof(*defined));
        if (slot_types == NULL || types == NULL || blocks == NULL ||
            defined == NULL) {
            free(slot_types);
            free(types);
            free(blocks);
            free(defined);
            return fail(error, fn->span, "IR verifier: out of memory");
        }

        bool valid = true;
        uint32_t slot_count = 0, register_count = 0, block_count = 0;
        for (const CcIrSlot *slot = fn->slots; slot != NULL; slot = slot->next) {
            if (slot->id != slot_count || slot->id >= fn->slot_count ||
                slot->type == CC_IR_VOID ||
                !valid_type(module, slot->type)) { valid = false; break; }
            slot_types[slot_count++] = slot->type;
        }
        if (slot_count != fn->slot_count) valid = false;
        for (const CcIrRegister *reg = fn->registers;
             reg != NULL && valid; reg = reg->next) {
            if (reg->id != register_count ||
                reg->id >= fn->register_count ||
                reg->type == CC_IR_VOID ||
                !valid_type(module, reg->type) ||
                reg->block >= fn->block_count) {
                valid = false; break;
            }
            register_count++;
        }
        if (register_count != fn->register_count) valid = false;
        for (uint32_t p = 0; p < fn->param_count && valid; p++)
            if (slot_types[p] != fn->param_types[p]) valid = false;

        if (!valid)
            fail(error, fn->span, "IR verifier: invalid slot/register table");

        for (const CcIrBlock *block = fn->blocks;
             block != NULL && valid; block = block->next) {
            if (block->id != block_count || !block->terminated) {
                valid = false;
                fail(error, fn->span,
                     "IR verifier: missing terminator or invalid block id");
                break;
            }
            block_count++;

            for (const CcIrInstruction *instruction = block->first;
                 instruction != NULL; instruction = instruction->next) {
                if (!validate_instruction(module, instruction, error,
                                          slot_types, fn->slot_count, types,
                                          blocks, defined, fn->register_count,
                                          block->id)) {
                    valid = false; break;
                }
                if (instruction->result != CC_IR_NONE) {
                    const CcIrRegister *reg = fn->registers;
                    while (reg != NULL && reg->id != instruction->result)
                        reg = reg->next;
                    if (reg == NULL || reg->type != instruction->type ||
                        reg->block != block->id) {
                        valid = false;
                        fail(error, instruction->span,
                             "IR verifier: register declaration mismatch");
                        break;
                    }
                }
            }
            if (!valid) break;
            switch (block->terminator) {
            case CC_IR_RET:
                if (fn->result == CC_IR_VOID) {
                    if (block->return_value != CC_IR_NONE) valid = false;
                } else {
                    CcIrInstruction temporary = {0};
                    temporary.span = fn->span;
                    valid = use_register(error, &temporary,
                        block->return_value, fn->result, types, blocks,
                        defined, fn->register_count, block->id);
                }
                break;
            case CC_IR_JUMP:
                valid = block->target < fn->block_count;
                break;
            case CC_IR_BRANCH: {
                CcIrInstruction temporary = {0};
                temporary.span = fn->span;
                valid = block->target < fn->block_count &&
                        block->alternate < fn->block_count &&
                        use_register(error, &temporary, block->condition,
                          CC_IR_BOOL, types, blocks, defined,
                          fn->register_count, block->id);
                break;
            }
            case CC_IR_TRAP: break;
            default: valid = false; break;
            }
            if (!valid && error->message == NULL)
                fail(error, fn->span, "IR verifier: invalid block terminator");
        }
        if (block_count != fn->block_count) valid = false;
        for (uint32_t r = 0; r < fn->register_count; r++)
            if (!defined[r]) valid = false;
        if (!valid && error->message == NULL)
            fail(error, fn->span, "IR verifier: missing block or register");
        free(slot_types);
        free(types);
        free(blocks);
        free(defined);
        if (!valid) return false;
    }
    return true;
}
