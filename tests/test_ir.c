#include "backend/c/emitter.h"
#include "ir/ir.h"
#include "parser/parser.h"
#include "semantic/semantic.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    CcSourceManager sources;
    CcFileId file_id;
    CcArena arena;
    CcLexer lexer;
    CcParser parser;
    CcIrModule *module;
} Fixture;

static void fixture_init(Fixture *f, const char *source)
{
    CcSourceError source_error;
    cc_source_manager_init(&f->sources);
    assert(cc_source_add_memory(&f->sources, "IR test",
               (const unsigned char *)source, strlen(source),
               &f->file_id, &source_error));
    const CcSourceFile *file = cc_source_get(&f->sources, f->file_id);
    assert(file != NULL);
    cc_arena_init(&f->arena);
    cc_lexer_init(&f->lexer, file, f->file_id);
    cc_parser_init(&f->parser, &f->lexer, &f->arena);
    f->module = NULL;
}

static void fixture_destroy(Fixture *f)
{
    cc_arena_destroy(&f->arena);
    cc_source_manager_destroy(&f->sources);
}

static void lower(Fixture *f)
{
    CcFunctionDeclaration *program = cc_parser_parse_program(&f->parser);
    assert(!f->parser.failed);
    CcSemanticError semantic_error;
    const CcSourceFile *file = cc_source_get(&f->sources, f->file_id);
    assert(cc_semantic_check(file, program, &f->arena, &semantic_error));
    CcIrError error;
    assert(cc_ir_lower(file, program, &f->arena, &f->module, &error));
    assert(error.message == NULL);
    assert(cc_ir_verify(f->module, &error));
}

static void expect_invalid(CcIrModule *module, const char *message)
{
    CcIrError error;
    assert(!cc_ir_verify(module, &error));
    assert(error.message != NULL);
    assert(strcmp(error.message, message) == 0);
    FILE *out = tmpfile();
    assert(out != NULL);
    assert(!cc_c_emit(out, module, &error));
    assert(strcmp(error.message, message) == 0);
    assert(fclose(out) == 0);
}

static void test_constant_and_print(void)
{
    Fixture f;
    fixture_init(&f, "fn main() -> i32 { return 42; }");
    lower(&f);
    assert(f.module->function_count == 1);
    CcIrFunction *fn = f.module->functions;
    assert(fn->result == CC_IR_I32);
    assert(fn->block_count == 1);
    assert(fn->register_count == 1);
    CcIrBlock *block = fn->blocks;
    assert(block->terminated);
    assert(block->terminator == CC_IR_RET);
    assert(block->first->op == CC_IR_CONST_I32);
    assert(block->first->constant == 42);
    assert(block->return_value == block->first->result);
    assert(cc_c_has_main(f.module));

    FILE *out = tmpfile();
    assert(out != NULL);
    assert(cc_ir_print(out, f.module));
    rewind(out);
    char line[128];
    assert(fgets(line, sizeof(line), out) != NULL);
    assert(strcmp(line, "fn @0() -> i32 {\n") == 0);
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);
    CcIrError error;
    assert(cc_c_emit(out, f.module, &error));
    rewind(out);
    assert(fgets(line, sizeof(line), out) != NULL);
    assert(strstr(line, "Generated from verified") != NULL);
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_bad_register_metadata(void)
{
    Fixture f;
    fixture_init(&f, "fn main() -> i32 { return 42; }");
    lower(&f);
    CcIrRegister *reg = f.module->functions->registers;
    reg->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: register declaration mismatch");
    reg->type = CC_IR_I32;
    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    fixture_destroy(&f);
}

static void test_bad_instruction_and_terminator(void)
{
    Fixture f;
    fixture_init(&f, "fn main() -> i32 { return 42; }");
    lower(&f);
    CcIrBlock *block = f.module->functions->blocks;
    CcIrInstruction *i = block->first;
    i->op = CC_IR_LOAD;
    i->slot = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: bad load slot/type");
    i->op = CC_IR_CONST_I32;
    block->terminator = CC_IR_JUMP;
    block->target = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid block terminator");
    block->terminator = CC_IR_RET;
    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    fixture_destroy(&f);
}

static void test_missing_terminator(void)
{
    Fixture f;
    fixture_init(&f, "fn main() -> i32 { return 42; }");
    lower(&f);
    f.module->functions->blocks->terminated = false;
    expect_invalid(f.module,
                   "IR verifier: missing terminator or invalid block id");
    fixture_destroy(&f);
}

static void test_bad_call_signature(void)
{
    Fixture f;
    fixture_init(&f,
        "fn identity(x: i32) -> i32 { return x; }"
        " fn main() -> i32 { return identity(42); }");
    lower(&f);
    CcIrFunction *main_fn = f.module->functions->next;
    assert(main_fn != NULL);
    CcIrInstruction *call = NULL;
    for (CcIrInstruction *i = main_fn->blocks->first; i != NULL;
         i = i->next)
        if (i->op == CC_IR_CALL) call = i;
    assert(call != NULL);
    call->callee = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid call signature");
    call->callee = 0;
    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    fixture_destroy(&f);
}

static void test_short_circuit_cfg(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 { let a: bool = true;"
        " let b: bool = false;"
        " if a == (b || true) { return 42; }"
        " else { return 1; } }");
    lower(&f);
    assert(f.module->functions->block_count >= 6);
    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    FILE *out = tmpfile();
    assert(out != NULL);
    assert(cc_c_emit(out, f.module, &error));
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_explicit_unsupported(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 { let (a, b) = (1, 2); return a + b; }");
    CcFunctionDeclaration *program = cc_parser_parse_program(&f.parser);
    assert(!f.parser.failed);
    CcSemanticError semantic_error;
    const CcSourceFile *file = cc_source_get(&f.sources, f.file_id);
    assert(cc_semantic_check(file, program, &f.arena, &semantic_error));
    CcIrError error;
    assert(!cc_ir_lower(file, program, &f.arena, &f.module, &error));
    assert(error.message != NULL);
    assert(strcmp(error.message,
           "IR lowering: tuple binding patterns not yet supported") == 0);
    assert(error.span.file_id == f.file_id);
    fixture_destroy(&f);
}

static void test_bad_inputs(void)
{
    CcIrError error;
    assert(!cc_ir_verify(NULL, &error));
    assert(strcmp(error.message, "IR verifier: invalid module") == 0);
    assert(!cc_ir_verify(NULL, NULL));
    assert(!cc_c_emit(NULL, NULL, &error));
    assert(!cc_ir_print(NULL, NULL));
}

int main(void)
{
    test_constant_and_print();
    test_bad_register_metadata();
    test_bad_instruction_and_terminator();
    test_missing_terminator();
    test_bad_call_signature();
    test_short_circuit_cfg();
    test_explicit_unsupported();
    test_bad_inputs();
    puts("IR: all tests passed");
    return 0;
}
