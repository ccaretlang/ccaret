#include "ir/ir.h"

#include <inttypes.h>

static void print_type(FILE *out, CcIrType type)
{
    switch (type) {
    case CC_IR_VOID: fputs("void", out); return;
    case CC_IR_I32: fputs("i32", out); return;
    case CC_IR_BOOL: fputs("bool", out); return;
    default: fprintf(out, "t%u", type); return;
    }
}

static const char *op_name(CcIrOpcode op)
{
    switch (op) {
    case CC_IR_CONST_I32: return "const.i32";
    case CC_IR_CONST_BOOL: return "const.bool";
    case CC_IR_LOAD: return "load";
    case CC_IR_STORE: return "store";
    case CC_IR_NEG: return "neg.checked";
    case CC_IR_NOT: return "not";
    case CC_IR_ADD: return "add.checked";
    case CC_IR_SUB: return "sub.checked";
    case CC_IR_MUL: return "mul.checked";
    case CC_IR_DIV: return "div.checked";
    case CC_IR_MOD: return "mod.checked";
    case CC_IR_EQ: return "eq";
    case CC_IR_NE: return "ne";
    case CC_IR_LT: return "lt";
    case CC_IR_LE: return "le";
    case CC_IR_GT: return "gt";
    case CC_IR_GE: return "ge";
    case CC_IR_CALL: return "call";
    case CC_IR_MAKE_TUPLE: return "tuple";
    case CC_IR_GET_TUPLE: return "tuple.get";
    case CC_IR_MAKE_SOME: return "some";
    case CC_IR_MAKE_NONE: return "none";
    }
    return "invalid";
}

bool cc_ir_print(FILE *out, const CcIrModule *module)
{
    if (out == NULL || module == NULL) return false;
    for (const CcIrTypeInfo *type = module->types; type != NULL;
         type = type->next) {
        fprintf(out, "type t%u = ", type->id);
        if (type->kind == CC_IR_TYPE_OPTIONAL) {
            fputc('?', out);
            print_type(out, type->inner);
        } else {
            fputc('(', out);
            for (uint32_t m = 0; m < type->count; m++) {
                if (m != 0) fputs(", ", out);
                print_type(out, type->members[m]);
            }
            if (type->count == 1) fputc(',', out);
            fputc(')', out);
        }
        fputc('\n', out);
    }
    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        fprintf(out, "fn @%u(", fn->id);
        for (uint32_t p = 0; p < fn->param_count; p++) {
            if (p != 0) fputs(", ", out);
            print_type(out, fn->param_types[p]);
        }
        fputs(") -> ", out);
        print_type(out, fn->result);
        fputs(" {\n", out);
        for (const CcIrBlock *block = fn->blocks; block != NULL;
             block = block->next) {
            fprintf(out, "  block%u:\n", block->id);
            for (const CcIrInstruction *i = block->first; i != NULL;
                 i = i->next) {
                if (i->result != CC_IR_NONE) {
                    fprintf(out, "    %%%u:", i->result);
                    print_type(out, i->type);
                    fputs(" = ", out);
                } else fputs("    ", out);
                fprintf(out, "%s", op_name(i->op));
                switch (i->op) {
                case CC_IR_CONST_I32:
                    fprintf(out, " %" PRId32, i->constant); break;
                case CC_IR_CONST_BOOL:
                    fprintf(out, " %s", i->constant ? "true" : "false");
                    break;
                case CC_IR_LOAD:
                    fprintf(out, " slot%u", i->slot); break;
                case CC_IR_STORE:
                    fprintf(out, " slot%u, %%%u", i->slot, i->lhs); break;
                case CC_IR_NEG:
                case CC_IR_NOT:
                    fprintf(out, " %%%u", i->lhs); break;
                case CC_IR_CALL:
                    fprintf(out, " @%u(", i->callee);
                    for (uint32_t p = 0; p < i->arg_count; p++)
                        fprintf(out, "%s%%%u", p == 0 ? "" : ", ", i->args[p]);
                    fputc(')', out);
                    break;
                case CC_IR_MAKE_TUPLE:
                    fputc('(', out);
                    for (uint32_t p = 0; p < i->arg_count; p++)
                        fprintf(out, "%s%%%u", p == 0 ? "" : ", ", i->args[p]);
                    fputc(')', out);
                    break;
                case CC_IR_GET_TUPLE:
                    fprintf(out, " %%%u.%u", i->lhs, i->slot);
                    break;
                case CC_IR_MAKE_SOME:
                    fprintf(out, " %%%u", i->lhs);
                    break;
                case CC_IR_MAKE_NONE:
                    break;
                default:
                    fprintf(out, " %%%u, %%%u", i->lhs, i->rhs);
                    break;
                }
                fputc('\n', out);
            }
            switch (block->terminator) {
            case CC_IR_RET:
                if (block->return_value == CC_IR_NONE)
                    fputs("    ret\n", out);
                else fprintf(out, "    ret %%%u\n", block->return_value);
                break;
            case CC_IR_JUMP:
                fprintf(out, "    br block%u\n", block->target); break;
            case CC_IR_BRANCH:
                fprintf(out, "    condbr %%%u, block%u, block%u\n",
                        block->condition, block->target, block->alternate);
                break;
            case CC_IR_TRAP:
                fputs("    trap\n", out); break;
            }
        }
        fputs("}\n", out);
    }
    return !ferror(out);
}
