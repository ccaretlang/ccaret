#include "backend/c/emitter.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void emit_c_type(FILE *out, CcIrType type)
{
    switch (type) {
    case CC_IR_VOID: fputs("void", out); return;
    case CC_IR_I32: fputs("int32_t", out); return;
    case CC_IR_BOOL: fputs("bool", out); return;
    default: fprintf(out, "cc_t_%u", type); return;
    }
}

static void emit_type_definitions(FILE *out, const CcIrModule *module)
{
    for (const CcIrTypeInfo *type = module->types; type != NULL;
         type = type->next) {
        fputs("typedef struct {\n", out);
        if (type->kind == CC_IR_TYPE_OPTIONAL) {
            fputs("  bool has;\n  ", out);
            emit_c_type(out, type->inner);
            fputs(" value;\n", out);
        } else if (type->count == 0) {
            fputs("  uint8_t _unit;\n", out);
        } else {
            for (uint32_t m = 0; m < type->count; m++) {
                fputs("  ", out);
                emit_c_type(out, type->members[m]);
                fprintf(out, " f%u;\n", m);
            }
        }
        fprintf(out, "} cc_t_%u;\n\n", type->id);
    }
}

static bool is_main(const CcIrModule *module, const CcIrFunction *fn)
{
    CcSourceSpan span = fn->name;
    return span.end >= span.start &&
           span.end - span.start == 4 &&
           span.end <= module->file->length &&
           memcmp(module->file->bytes + span.start, "main", 4) == 0;
}

bool cc_c_has_main(const CcIrModule *module)
{
    if (module == NULL || module->file == NULL) return false;
    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next)
        if (is_main(module, fn) && fn->result == CC_IR_I32 &&
            fn->param_count == 0) return true;
    return false;
}

static void emit_signature(FILE *out, const CcIrFunction *fn)
{
    emit_c_type(out, fn->result);
    fprintf(out, " cc_fn_%u(", fn->id);
    if (fn->param_count == 0) fputs("void", out);
    for (uint32_t p = 0; p < fn->param_count; p++) {
        if (p != 0) fputs(", ", out);
        emit_c_type(out, fn->param_types[p]);
        fprintf(out, " a%u", p);
    }
    fputc(')', out);
}

static void emit_instruction(FILE *out, const CcIrInstruction *i)
{
    if (i->result != CC_IR_NONE)
        fprintf(out, "  r%u = ", i->result);
    else fputs("  ", out);

    switch (i->op) {
    case CC_IR_CONST_I32:
        if (i->constant == INT32_MIN) fputs("INT32_MIN", out);
        else fprintf(out, "INT32_C(%" PRId32 ")", i->constant);
        break;
    case CC_IR_CONST_BOOL: fputs(i->constant ? "true" : "false", out); break;
    case CC_IR_LOAD: fprintf(out, "s%u", i->slot); break;
    case CC_IR_STORE:
        fprintf(out, "s%u = r%u", i->slot, i->lhs); break;
    case CC_IR_NEG: fprintf(out, "ccrt_neg_i32(r%u)", i->lhs); break;
    case CC_IR_NOT: fprintf(out, "!r%u", i->lhs); break;
    case CC_IR_BIT_NOT:
        fprintf(out, "ccrt_bit_not_i32(r%u)", i->lhs); break;
    case CC_IR_ADD:
    case CC_IR_SUB:
    case CC_IR_MUL:
    case CC_IR_DIV:
    case CC_IR_MOD: {
        const char *name = i->op == CC_IR_ADD ? "add" :
                           i->op == CC_IR_SUB ? "sub" :
                           i->op == CC_IR_MUL ? "mul" :
                           i->op == CC_IR_DIV ? "div" : "mod";
        fprintf(out, "ccrt_%s_i32(r%u, r%u)", name, i->lhs, i->rhs);
        break;
    }
    case CC_IR_WRAP_ADD:
    case CC_IR_WRAP_SUB:
    case CC_IR_WRAP_MUL:
    case CC_IR_BIT_AND:
    case CC_IR_BIT_OR:
    case CC_IR_BIT_XOR: {
        const char *name =
            i->op == CC_IR_WRAP_ADD ? "wrap_add" :
            i->op == CC_IR_WRAP_SUB ? "wrap_sub" :
            i->op == CC_IR_WRAP_MUL ? "wrap_mul" :
            i->op == CC_IR_BIT_AND ? "bit_and" :
            i->op == CC_IR_BIT_OR ? "bit_or" : "bit_xor";
        fprintf(out, "ccrt_%s_i32(r%u, r%u)", name, i->lhs, i->rhs);
        break;
    }
    case CC_IR_EQ:
    case CC_IR_NE:
    case CC_IR_LT:
    case CC_IR_LE:
    case CC_IR_GT:
    case CC_IR_GE: {
        const char *operator = i->op == CC_IR_EQ ? "==" :
            i->op == CC_IR_NE ? "!=" : i->op == CC_IR_LT ? "<" :
            i->op == CC_IR_LE ? "<=" : i->op == CC_IR_GT ? ">" : ">=";
        fprintf(out, "(r%u %s r%u)", i->lhs, operator, i->rhs);
        break;
    }
    case CC_IR_CALL:
        fprintf(out, "cc_fn_%u(", i->callee);
        for (uint32_t a = 0; a < i->arg_count; a++)
            fprintf(out, "%sr%u", a == 0 ? "" : ", ", i->args[a]);
        fputc(')', out);
        break;
    case CC_IR_MAKE_TUPLE:
        fprintf(out, "(cc_t_%u){", i->type);
        if (i->arg_count == 0) fputs("0", out);
        for (uint32_t a = 0; a < i->arg_count; a++)
            fprintf(out, "%s.f%u = r%u", a == 0 ? " " : ", ",
                    a, i->args[a]);
        fputs(" }", out);
        break;
    case CC_IR_GET_TUPLE:
        fprintf(out, "r%u.f%u", i->lhs, i->slot);
        break;
    case CC_IR_MAKE_SOME:
        fprintf(out, "(cc_t_%u){ .has = true, .value = r%u }",
                i->type, i->lhs);
        break;
    case CC_IR_MAKE_NONE:
        fprintf(out, "(cc_t_%u){0}", i->type);
        break;
    }
    fputs(";\n", out);
}

static bool emit_function(FILE *out, const CcIrFunction *fn)
{
    /* Drop unreachable join blocks, e.g. the join after two returning arms.
     * Keeping their C labels triggers -Wunused-label with strict GCC flags. */
    bool *reachable = calloc(fn->block_count, sizeof(*reachable));
    if (reachable == NULL) return false;
    reachable[0] = true;
    bool changed;
    do {
        changed = false;
        for (const CcIrBlock *block = fn->blocks; block != NULL;
             block = block->next) {
            if (!reachable[block->id]) continue;
            if ((block->terminator == CC_IR_JUMP ||
                 block->terminator == CC_IR_BRANCH) &&
                !reachable[block->target]) {
                reachable[block->target] = true;
                changed = true;
            }
            if (block->terminator == CC_IR_BRANCH &&
                !reachable[block->alternate]) {
                reachable[block->alternate] = true;
                changed = true;
            }
        }
    } while (changed);

    emit_signature(out, fn);
    fputs(" {\n", out);
    for (const CcIrSlot *slot = fn->slots; slot != NULL; slot = slot->next) {
        fputs("  ", out);
        emit_c_type(out, slot->type);
        fprintf(out, " s%u = ", slot->id);
        if (slot->id < fn->param_count) fprintf(out, "a%u", slot->id);
        else if (slot->type == CC_IR_BOOL) fputs("false", out);
        else if (slot->type == CC_IR_I32) fputs("INT32_C(0)", out);
        else fprintf(out, "(cc_t_%u){0}", slot->type);
        fputs(";\n", out);
        fprintf(out, "  (void)s%u;\n", slot->id);
    }
    for (const CcIrRegister *reg = fn->registers; reg != NULL;
         reg = reg->next) {
        if (!reachable[reg->block]) continue;
        fputs("  ", out);
        emit_c_type(out, reg->type);
        fprintf(out, " r%u = ", reg->id);
        if (reg->type == CC_IR_BOOL) fputs("false", out);
        else if (reg->type == CC_IR_I32) fputs("INT32_C(0)", out);
        else fprintf(out, "(cc_t_%u){0}", reg->type);
        fputs(";\n", out);
        fprintf(out, "  (void)r%u;\n", reg->id);
    }
    fputs("  goto L0;\n", out);
    for (const CcIrBlock *block = fn->blocks; block != NULL;
         block = block->next) {
        if (!reachable[block->id]) continue;
        fprintf(out, "L%u:;\n", block->id);
        for (const CcIrInstruction *i = block->first; i != NULL; i = i->next)
            emit_instruction(out, i);
        switch (block->terminator) {
        case CC_IR_RET:
            if (block->return_value == CC_IR_NONE) fputs("  return;\n", out);
            else fprintf(out, "  return r%u;\n", block->return_value);
            break;
        case CC_IR_JUMP:
            fprintf(out, "  goto L%u;\n", block->target); break;
        case CC_IR_BRANCH:
            fprintf(out, "  if (r%u) goto L%u; else goto L%u;\n",
                    block->condition, block->target, block->alternate);
            break;
        case CC_IR_TRAP: fputs("  abort();\n", out); break;
        }
    }
    fputs("}\n\n", out);
    free(reachable);
    return !ferror(out);
}

bool cc_c_emit(FILE *out, const CcIrModule *module, CcIrError *error)
{
    if (error == NULL) return false;
    if (out == NULL) {
        error->span = (CcSourceSpan){ CC_FILE_ID_INVALID, 0, 0 };
        error->message = "C backend: invalid output stream";
        return false;
    }
    if (!cc_ir_verify(module, error)) return false;
    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        if (is_main(module, fn) &&
            (fn->result != CC_IR_I32 || fn->param_count != 0)) {
            error->span = fn->name;
            error->message = "C backend: main must be fn main() -> i32";
            return false;
        }
    }

    fputs("/* Generated from verified C^ IR. Do not edit. */\n"
          "#include <stdbool.h>\n"
          "#include <stdint.h>\n"
          "#include <limits.h>\n"
          "#include <stdlib.h>\n\n"
          "int32_t ccrt_checked_i32(int64_t value) {\n"
          "  if (value < INT32_MIN || value > INT32_MAX) abort();\n"
          "  return (int32_t)value;\n"
          "}\n"
          "int32_t ccrt_add_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_checked_i32((int64_t)a + (int64_t)b);\n"
          "}\n"
          "int32_t ccrt_sub_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_checked_i32((int64_t)a - (int64_t)b);\n"
          "}\n"
          "int32_t ccrt_mul_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_checked_i32((int64_t)a * (int64_t)b);\n"
          "}\n"
          "int32_t ccrt_neg_i32(int32_t a) {\n"
          "  return ccrt_checked_i32(-(int64_t)a);\n"
          "}\n"
          "int32_t ccrt_div_i32(int32_t a, int32_t b) {\n"
          "  if (b == 0 || (a == INT32_MIN && b == -1)) abort();\n"
          "  return a / b;\n"
          "}\n"
          "int32_t ccrt_mod_i32(int32_t a, int32_t b) {\n"
          "  if (b == 0 || (a == INT32_MIN && b == -1)) abort();\n"
          "  return a % b;\n"
          "}\n"
          "/* Reconstruct a signed i32 value without out-of-range unsigned-to-signed casts. */\n"
          "int32_t ccrt_from_bits_i32(uint32_t bits) {\n"
          "  int64_t value = (int64_t)bits;\n"
          "  if (value > INT32_MAX) value -= INT64_C(4294967296);\n"
          "  return (int32_t)value;\n"
          "}\n"
          "int32_t ccrt_wrap_add_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_from_bits_i32((uint32_t)a + (uint32_t)b);\n"
          "}\n"
          "int32_t ccrt_wrap_sub_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_from_bits_i32((uint32_t)a - (uint32_t)b);\n"
          "}\n"
          "int32_t ccrt_wrap_mul_i32(int32_t a, int32_t b) {\n"
          "  uint64_t product = (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;\n"
          "  return ccrt_from_bits_i32((uint32_t)product);\n"
          "}\n"
          "int32_t ccrt_bit_and_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_from_bits_i32((uint32_t)a & (uint32_t)b);\n"
          "}\n"
          "int32_t ccrt_bit_or_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_from_bits_i32((uint32_t)a | (uint32_t)b);\n"
          "}\n"
          "int32_t ccrt_bit_xor_i32(int32_t a, int32_t b) {\n"
          "  return ccrt_from_bits_i32((uint32_t)a ^ (uint32_t)b);\n"
          "}\n"
          "int32_t ccrt_bit_not_i32(int32_t a) {\n"
          "  return ccrt_from_bits_i32(~(uint32_t)a);\n"
          "}\n\n", out);

    emit_type_definitions(out, module);

    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        emit_signature(out, fn);
        fputs(";\n", out);
    }
    fputc('\n', out);
    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        if (!emit_function(out, fn)) {
            error->span = fn->span;
            error->message = "C backend: output write or allocation failed";
            return false;
        }
    }

    for (const CcIrFunction *fn = module->functions; fn != NULL;
         fn = fn->next) {
        if (is_main(module, fn)) {
            fprintf(out, "int main(void) { return (int)cc_fn_%u(); }\n",
                    fn->id);
            break;
        }
    }
    if (ferror(out)) {
        error->span = (CcSourceSpan){ CC_FILE_ID_INVALID, 0, 0 };
        error->message = "C backend: could not write generated C";
        return false;
    }
    return true;
}
