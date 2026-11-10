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
    if (strcmp(error.message, message) != 0)
        fprintf(stderr, "expected: %s; actual: %s\n", message,
                error.message);
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

static CcIrInstruction *find_instruction(CcIrFunction *fn, CcIrOpcode opcode)
{
    for (CcIrBlock *block = fn->blocks; block != NULL; block = block->next)
        for (CcIrInstruction *i = block->first; i != NULL; i = i->next)
            if (i->op == opcode) return i;
    return NULL;
}

static void test_tuple_destructuring_and_type_interning(void)
{
    Fixture f;
    fixture_init(&f,
        "fn id(x: (i32, ?i32)) -> (i32, ?i32) { return x; }"
        "fn main() -> i32 {"
        " let (a, b): (i32, ?i32) = id((40, some(2)));"
        " let c: ?i32 = none;"
        " return a + 2; }");
    lower(&f);
    assert(f.module->type_count == 5);
    const CcIrTypeInfo *optional = cc_ir_type_info(f.module, 3);
    const CcIrTypeInfo *tuple = cc_ir_type_info(f.module, 4);
    assert(optional != NULL && optional->kind == CC_IR_TYPE_OPTIONAL);
    assert(optional->inner == CC_IR_I32);
    assert(tuple != NULL && tuple->kind == CC_IR_TYPE_TUPLE);
    assert(tuple->count == 2);
    assert(tuple->members[0] == CC_IR_I32);
    assert(tuple->members[1] == optional->id);
    assert(cc_ir_type_info(f.module, CC_IR_NONE) == NULL);
    assert(cc_ir_type_info(f.module, CC_IR_I32) == NULL);
    assert(find_instruction(f.module->functions->next, CC_IR_MAKE_TUPLE));
    assert(find_instruction(f.module->functions->next, CC_IR_GET_TUPLE));
    assert(find_instruction(f.module->functions->next, CC_IR_MAKE_SOME));
    assert(find_instruction(f.module->functions->next, CC_IR_MAKE_NONE));

    FILE *out = tmpfile();
    assert(out != NULL);
    assert(cc_ir_print(out, f.module));
    rewind(out);
    char text[8192];
    size_t n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    assert(strstr(text, "type t3 = ?i32\n") != NULL);
    assert(strstr(text, "type t4 = (i32, t3)\n") != NULL);
    assert(strstr(text, "tuple(") != NULL);
    assert(strstr(text, "tuple.get ") != NULL);
    assert(strstr(text, "some ") != NULL);
    assert(strstr(text, "none\n") != NULL);
    assert(fclose(out) == 0);

    out = tmpfile();
    assert(out != NULL);
    CcIrError error;
    assert(cc_c_emit(out, f.module, &error));
    rewind(out);
    n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    assert(strstr(text, "} cc_t_3;") != NULL);
    assert(strstr(text, "} cc_t_4;") != NULL);
    assert(strstr(text, ".has = true") != NULL);
    assert(strstr(text, ".f0 =") != NULL);
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_invalid_tuple_ir(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let (a, b): (i32, bool) = (42, true);"
        " if b { return a; } return 0; }");
    lower(&f);
    CcIrInstruction *make =
        find_instruction(f.module->functions, CC_IR_MAKE_TUPLE);
    CcIrInstruction *get =
        find_instruction(f.module->functions, CC_IR_GET_TUPLE);
    assert(make != NULL && get != NULL);
    assert(make->arg_count == 2);
    CcIrTypeInfo *type = f.module->types;
    assert(type != NULL && type->kind == CC_IR_TYPE_TUPLE);

    make->arg_count = 1;
    expect_invalid(f.module, "IR verifier: invalid tuple constructor");
    make->arg_count = 2;
    uint32_t saved_arg = make->args[0];
    make->args[0] = make->args[1];
    expect_invalid(f.module,
                   "IR verifier: undefined, cross-block or mistyped register");
    make->args[0] = saved_arg;
    make->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid tuple constructor");
    make->type = type->id;

    uint32_t saved_slot = get->slot;
    get->slot = 99;
    expect_invalid(f.module, "IR verifier: invalid tuple projection");
    get->slot = saved_slot;
    uint32_t saved_lhs = get->lhs;
    get->lhs = saved_arg;
    expect_invalid(f.module, "IR verifier: invalid tuple projection");
    get->lhs = saved_lhs;
    CcIrType saved_projection = get->type;
    get->type = saved_projection == CC_IR_I32 ? CC_IR_BOOL : CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid tuple projection");
    get->type = saved_projection;

    CcIrType saved_member = type->members[0];
    type->members[0] = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid tuple element type");
    type->members[0] = type->id;
    expect_invalid(f.module, "IR verifier: invalid tuple element type");
    type->members[0] = saved_member;
    type->inner = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid tuple type");
    type->inner = CC_IR_VOID;
    type->id = 99;
    expect_invalid(f.module, "IR verifier: invalid type table");
    type->id = 3;
    f.module->type_count++;
    expect_invalid(f.module, "IR verifier: invalid type table");
    f.module->type_count--;
    CcIrTypeInfo duplicate = *type;
    duplicate.id = 4;
    duplicate.next = NULL;
    type->next = &duplicate;
    f.module->type_count++;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    f.module->type_count--;
    type->next = NULL;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    fixture_destroy(&f);
}

static void test_invalid_optional_ir(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let flag: bool = true;"
        " let a: ?i32 = some(12);"
        " let b: ?i32 = none;"
        " return 42; }");
    lower(&f);
    CcIrInstruction *some =
        find_instruction(f.module->functions, CC_IR_MAKE_SOME);
    CcIrInstruction *none =
        find_instruction(f.module->functions, CC_IR_MAKE_NONE);
    CcIrInstruction *boolean =
        find_instruction(f.module->functions, CC_IR_CONST_BOOL);
    assert(some != NULL && none != NULL && boolean != NULL);
    CcIrTypeInfo *type = f.module->types;
    assert(type != NULL && type->kind == CC_IR_TYPE_OPTIONAL);

    uint32_t saved_lhs = some->lhs;
    some->lhs = boolean->result;
    expect_invalid(f.module, "IR verifier: invalid some constructor");
    some->lhs = saved_lhs;
    some->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid some constructor");
    some->type = type->id;
    none->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid none constructor");
    none->type = type->id;
    type->inner = type->id;
    expect_invalid(f.module, "IR verifier: invalid optional type");
    type->inner = CC_IR_VOID;
    expect_invalid(f.module, "IR verifier: invalid optional type");
    type->inner = CC_IR_I32;
    type->count = 1;
    expect_invalid(f.module, "IR verifier: invalid optional type");
    type->count = 0;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    fixture_destroy(&f);
}

static void test_optional_access_ir_and_verifier(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let present: ?i32 = some(20);"
        " let absent: ?i32 = none;"
        " let flag = present.has;"
        " return present.value + 22; }");
    lower(&f);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *some = find_instruction(fn, CC_IR_MAKE_SOME);
    CcIrInstruction *none = find_instruction(fn, CC_IR_MAKE_NONE);
    CcIrInstruction *has = find_instruction(fn, CC_IR_OPTIONAL_HAS);
    CcIrInstruction *unwrap = find_instruction(fn, CC_IR_OPTIONAL_UNWRAP);
    CcIrInstruction *number = find_instruction(fn, CC_IR_CONST_I32);
    assert(some != NULL && none != NULL && has != NULL &&
           unwrap != NULL && number != NULL);
    const CcIrTypeInfo *opt = cc_ir_type_info(f.module, some->type);
    assert(opt != NULL && opt->kind == CC_IR_TYPE_OPTIONAL);
    assert(opt->inner == CC_IR_I32);
    assert(has->type == CC_IR_BOOL && unwrap->type == CC_IR_I32);

    FILE *out = tmpfile();
    assert(out != NULL && cc_ir_print(out, f.module));
    rewind(out);
    char printed[15000];
    size_t n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "optional.has") != NULL);
    assert(strstr(printed, "optional.unwrap") != NULL);
    assert(fclose(out) == 0);

    uint32_t saved_lhs = has->lhs;
    has->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid optional access");
    has->lhs = number->result;
    expect_invalid(f.module, "IR verifier: invalid optional access");
    has->lhs = saved_lhs;

    CcIrType saved_type = has->type;
    has->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid optional access");
    has->type = saved_type;

    saved_lhs = unwrap->lhs;
    unwrap->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid optional access");
    unwrap->lhs = number->result;
    expect_invalid(f.module, "IR verifier: invalid optional access");
    unwrap->lhs = saved_lhs;
    saved_type = unwrap->type;
    unwrap->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: invalid optional access");
    unwrap->type = saved_type;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    out = tmpfile();
    assert(out != NULL && cc_c_emit(out, f.module, &error));
    rewind(out);
    n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "if (!r") != NULL);
    assert(strstr(printed, ".has) abort();") != NULL);
    assert(strstr(printed, ".value;") != NULL);
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_for_range_cfg_and_bounds_once(void)
{
    Fixture f;
    fixture_init(&f,
        "fn low() -> i32 { return 1; }"
        "fn high() -> i32 { return 3; }"
        "fn main() -> i32 { var sum: i32 = 0;"
        " for i in low()..=high() {"
        "  if i == 2 { continue; }"
        "  sum += i;"
        " } return sum; }");
    lower(&f);
    CcIrFunction *main_fn = f.module->functions->next->next;
    assert(main_fn != NULL && main_fn->block_count >= 8);
    unsigned int calls = 0, branches = 0, additions = 0;
    for (CcIrBlock *b = main_fn->blocks; b != NULL; b = b->next) {
        if (b->terminator == CC_IR_BRANCH) branches++;
        for (CcIrInstruction *i = b->first; i != NULL; i = i->next) {
            if (i->op == CC_IR_CALL) calls++;
            if (i->op == CC_IR_ADD) additions++;
        }
    }
    assert(calls == 2); /* Bounds are evaluated once, before the loop. */
    assert(branches >= 3);
    assert(additions >= 2); /* Sum update and guarded index step. */
    assert(find_instruction(main_fn, CC_IR_LE) != NULL);
    assert(find_instruction(main_fn, CC_IR_EQ) != NULL);
    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    FILE *out = tmpfile();
    assert(out != NULL);
    assert(cc_c_emit(out, f.module, &error));
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_bitwise_wrapping_and_compound_ir(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let flag: bool = true;"
        " var x: i32 = 6;"
        " x &= 3; x |= 4; x ^= 1;"
        " let a: i32 = ~x;"
        " let b: i32 = 2147483647 +% 1;"
        " let c: i32 = b -% 1;"
        " let d: i32 = c *% 3;"
        " return a + d; }");
    lower(&f);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *and = find_instruction(fn, CC_IR_BIT_AND);
    CcIrInstruction *not = find_instruction(fn, CC_IR_BIT_NOT);
    CcIrInstruction *wrap = find_instruction(fn, CC_IR_WRAP_ADD);
    CcIrInstruction *boolean = find_instruction(fn, CC_IR_CONST_BOOL);
    assert(and != NULL && not != NULL && wrap != NULL &&
           boolean != NULL);
    assert(find_instruction(fn, CC_IR_BIT_OR) != NULL);
    assert(find_instruction(fn, CC_IR_BIT_XOR) != NULL);
    assert(find_instruction(fn, CC_IR_WRAP_SUB) != NULL);
    assert(find_instruction(fn, CC_IR_WRAP_MUL) != NULL);

    uint32_t original = and->lhs;
    and->lhs = boolean->result;
    expect_invalid(f.module, "IR verifier: invalid arithmetic");
    and->lhs = original;
    CcIrType original_type = and->type;
    and->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: invalid arithmetic");
    and->type = original_type;
    original = not->lhs;
    not->lhs = boolean->result;
    expect_invalid(f.module, "IR verifier: invalid unary operation");
    not->lhs = original;
    original = wrap->rhs;
    wrap->rhs = boolean->result;
    expect_invalid(f.module, "IR verifier: invalid arithmetic");
    wrap->rhs = original;
    CcIrError error;
    assert(cc_ir_verify(f.module, &error));

    FILE *out = tmpfile();
    assert(out != NULL);
    assert(cc_ir_print(out, f.module));
    rewind(out);
    char text[8192];
    size_t n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    assert(strstr(text, "bit.and ") != NULL);
    assert(strstr(text, "bit.or ") != NULL);
    assert(strstr(text, "bit.xor ") != NULL);
    assert(strstr(text, "bit.not ") != NULL);
    assert(strstr(text, "add.wrap ") != NULL);
    assert(strstr(text, "sub.wrap ") != NULL);
    assert(strstr(text, "mul.wrap ") != NULL);
    assert(fclose(out) == 0);
    out = tmpfile();
    assert(out != NULL);
    assert(cc_c_emit(out, f.module, &error));
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_array_ir_and_verifier(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let flag: bool = true;"
        " var data: [2]i32 = [20,22];"
        " data[1] += 1;"
        " if data.len == 2 { return data[0] + data[1]; }"
        " return 0; }");
    lower(&f);
    assert(f.module->type_count == 4);
    CcIrTypeInfo *array = f.module->types;
    assert(array && array->kind == CC_IR_TYPE_ARRAY &&
           array->count == 2 && array->inner == CC_IR_I32);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *make = find_instruction(fn, CC_IR_MAKE_ARRAY);
    CcIrInstruction *get = find_instruction(fn, CC_IR_GET_ARRAY);
    CcIrInstruction *set = find_instruction(fn, CC_IR_STORE_INDEX);
    CcIrInstruction *length = find_instruction(fn, CC_IR_CONST_USIZE);
    CcIrInstruction *boolean = find_instruction(fn, CC_IR_CONST_BOOL);
    assert(make && get && set && length && boolean);
    assert(length->wide_constant == 2);

    FILE *out = tmpfile();
    assert(out != NULL);
    assert(cc_ir_print(out, f.module));
    rewind(out);
    char text[16384];
    size_t n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    assert(strstr(text, "type t3 = [2]i32") != NULL);
    assert(strstr(text, "const.usize 2") != NULL);
    assert(strstr(text, "array(") != NULL);
    assert(strstr(text, "array.get") != NULL);
    assert(strstr(text, "array.store") != NULL);
    assert(fclose(out) == 0);

    uint32_t original_count = make->arg_count;
    make->arg_count = 1;
    expect_invalid(f.module, "IR verifier: invalid array constructor");
    make->arg_count = original_count;
    uint32_t original_arg = make->args[0];
    make->args[0] = boolean->result;
    expect_invalid(f.module,
        "IR verifier: undefined, cross-block or mistyped register");
    make->args[0] = original_arg;
    get->arg_count = 4;
    expect_invalid(f.module, "IR verifier: invalid array projection");
    get->arg_count = 2;
    uint32_t original_index = get->rhs;
    get->rhs = boolean->result;
    expect_invalid(f.module, "IR verifier: invalid array projection");
    get->rhs = original_index;
    uint32_t original_slot = set->slot;
    set->slot = 0; /* Boolean local, not an array. */
    expect_invalid(f.module, "IR verifier: invalid array indexed store");
    set->slot = original_slot;
    set->arg_count = 1;
    expect_invalid(f.module, "IR verifier: invalid array indexed store");
    set->arg_count = 2;
    CcIrType original_type = length->type;
    length->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: bad usize constant");
    length->type = original_type;

    array->count = 65537;
    expect_invalid(f.module, "IR verifier: invalid array type");
    array->count = 2;
    array->inner = array->id;
    expect_invalid(f.module, "IR verifier: invalid array type");
    array->inner = CC_IR_I32;
    CcIrType fake_member = CC_IR_I32;
    array->members = &fake_member;
    expect_invalid(f.module, "IR verifier: invalid array type");
    array->members = NULL;
    CcIrTypeInfo duplicate = *array;
    duplicate.id = 4;
    duplicate.next = NULL;
    array->next = &duplicate;
    f.module->type_count++;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    f.module->type_count--;
    array->next = NULL;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    out = tmpfile();
    assert(out != NULL);
    assert(cc_c_emit(out, f.module, &error));
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_struct_ir_and_verifier(void)
{
    Fixture f;
    fixture_init(&f,
        "struct Pair { x: i32, y: i32 }"
        "fn main() -> i32 {"
        " let flag: bool = true;"
        " var p = Pair { y: 22, x: 20 };"
        " p.x += 1; return p.x + p.y; }");
    lower(&f);
    assert(f.module->type_count == 4);
    CcIrTypeInfo *type = f.module->types;
    assert(type != NULL && type->kind == CC_IR_TYPE_STRUCT);
    assert(type->count == 2 && type->members[0] == CC_IR_I32 &&
           type->members[1] == CC_IR_I32);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *make = find_instruction(fn, CC_IR_MAKE_STRUCT);
    CcIrInstruction *get = find_instruction(fn, CC_IR_GET_STRUCT);
    CcIrInstruction *set = find_instruction(fn, CC_IR_SET_FIELD);
    CcIrInstruction *boolean = find_instruction(fn, CC_IR_CONST_BOOL);
    assert(make && get && set && boolean);

    FILE *out = tmpfile();
    assert(out != NULL && cc_ir_print(out, f.module));
    rewind(out);
    char text[16384];
    size_t n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    assert(strstr(text, "type t3 = struct Pair { x: i32, y: i32 }") != NULL);
    assert(strstr(text, "struct(") != NULL);
    assert(strstr(text, "struct.get") != NULL);
    assert(strstr(text, "struct.set") != NULL);
    assert(fclose(out) == 0);

    uint32_t count = make->arg_count;
    make->arg_count = 1;
    expect_invalid(f.module, "IR verifier: invalid struct constructor");
    make->arg_count = count;
    uint32_t original_arg = make->args[0];
    make->args[0] = boolean->result;
    expect_invalid(f.module,
        "IR verifier: undefined, cross-block or mistyped register");
    make->args[0] = original_arg;
    uint32_t original_field = get->slot;
    get->slot = 5;
    expect_invalid(f.module, "IR verifier: invalid struct projection");
    get->slot = original_field;
    CcIrType original_type = get->type;
    get->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: invalid struct projection");
    get->type = original_type;
    uint32_t original_slot = set->slot;
    set->slot = 0; /* Boolean binding, not a struct slot. */
    expect_invalid(f.module, "IR verifier: invalid struct field store");
    set->slot = original_slot;
    uint32_t original_field_set = set->arg_count;
    set->arg_count = 99;
    expect_invalid(f.module, "IR verifier: invalid struct field store");
    set->arg_count = original_field_set;
    uint32_t original_value = set->lhs;
    set->lhs = boolean->result;
    expect_invalid(f.module, "IR verifier: invalid struct field store");
    set->lhs = original_value;

    CcIrType saved = type->members[0];
    type->members[0] = CC_IR_VOID;
    expect_invalid(f.module, "IR verifier: invalid struct field type");
    type->members[0] = saved;
    CcSourceSpan saved_name = type->field_names[0];
    type->field_names[0] = type->field_names[1];
    expect_invalid(f.module, "IR verifier: duplicate struct field");
    type->field_names[0] = saved_name;
    saved_name = type->name;
    type->name.end = (uint32_t)f.module->file->length + 1;
    expect_invalid(f.module, "IR verifier: invalid struct type");
    type->name = saved_name;
    CcIrTypeInfo duplicate = *type;
    duplicate.id = 4;
    duplicate.next = NULL;
    type->next = &duplicate;
    f.module->type_count++;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    f.module->type_count--;
    type->next = NULL;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    out = tmpfile();
    assert(out != NULL && cc_c_emit(out, f.module, &error));
    rewind(out);
    n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    assert(strstr(text, ".f0 =") != NULL);
    assert(strstr(text, "} cc_t_3;") != NULL);
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_nominal_and_forward_struct_types(void)
{
    Fixture f;
    fixture_init(&f,
        "struct Outer { child: Child }"
        "struct Child { n: i32 }"
        "struct A { n: i32 } struct B { n: i32 }"
        "fn main() -> i32 {"
        " let a = A { n: 20 }; let b = B { n: 22 };"
        " let outer = Outer { child: Child { n: 1 } };"
        " return a.n + b.n; }");
    lower(&f);
    assert(f.module->type_count == 7);
    const CcIrTypeInfo *child = NULL, *outer = NULL;
    const CcIrTypeInfo *a = NULL, *b = NULL;
    for (const CcIrTypeInfo *type = f.module->types; type != NULL;
         type = type->next) {
        const unsigned char *name = f.module->file->bytes + type->name.start;
        size_t len = type->name.end - type->name.start;
        if (len == 5 && memcmp(name, "Child", 5) == 0) child = type;
        if (len == 5 && memcmp(name, "Outer", 5) == 0) outer = type;
        if (len == 1 && name[0] == 'A') a = type;
        if (len == 1 && name[0] == 'B') b = type;
    }
    assert(child && outer && a && b);
    assert(child->kind == CC_IR_TYPE_STRUCT &&
           outer->kind == CC_IR_TYPE_STRUCT);
    assert(outer->members[0] == child->id);
    assert(a->id != b->id && a->members[0] == b->members[0]);
    fixture_destroy(&f);
}

static void test_pointer_ir_and_verifier(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let constant: i32 = 20; var value: i32 = 22;"
        " let cp: *const i32 = &constant; let p: *i32 = &value;"
        " unsafe { *p = *cp + *p; } return value; }");
    lower(&f);
    CcIrTypeInfo *readonly = f.module->types;
    CcIrTypeInfo *writable = readonly != NULL ? readonly->next : NULL;
    assert(readonly && writable);
    assert(readonly->kind == CC_IR_TYPE_POINTER && readonly->pointer_const);
    assert(writable->kind == CC_IR_TYPE_POINTER && !writable->pointer_const);
    assert(readonly->inner == CC_IR_I32 && writable->inner == CC_IR_I32);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *address = find_instruction(fn, CC_IR_ADDR_SLOT);
    CcIrInstruction *read = find_instruction(fn, CC_IR_DEREF_PTR);
    CcIrInstruction *write = find_instruction(fn, CC_IR_STORE_PTR);
    assert(address != NULL && read != NULL && write != NULL);

    FILE *out = tmpfile();
    assert(out != NULL && cc_ir_print(out, f.module));
    rewind(out);
    char printed[16384];
    size_t n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "= *const i32") != NULL);
    assert(strstr(printed, "= *i32") != NULL);
    assert(strstr(printed, "addr.slot") != NULL);
    assert(strstr(printed, "ptr.load") != NULL);
    assert(strstr(printed, "ptr.store") != NULL);
    assert(fclose(out) == 0);

    uint32_t saved_slot = address->slot;
    address->slot = fn->slot_count;
    expect_invalid(f.module, "IR verifier: invalid address-of local slot");
    address->slot = saved_slot;
    CcIrSlot *address_slot = fn->slots;
    while (address_slot != NULL && address_slot->id != saved_slot)
        address_slot = address_slot->next;
    assert(address_slot != NULL);
    address_slot->mutable = !address_slot->mutable;
    expect_invalid(f.module, "IR verifier: invalid address-of local slot");
    address_slot->mutable = !address_slot->mutable;
    CcIrType saved_type = address->type;
    address->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: invalid address-of local slot");
    address->type = saved_type;
    saved_type = read->type;
    read->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: invalid raw pointer read");
    read->type = saved_type;
    uint32_t saved_lhs = read->lhs;
    read->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid raw pointer read");
    read->lhs = saved_lhs;
    uint32_t saved_rhs = write->rhs;
    write->rhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid raw pointer write");
    write->rhs = saved_rhs;
    saved_lhs = write->lhs;
    /* The first dereference reads *cp, so its source is a *const i32. */
    write->lhs = read->lhs;
    expect_invalid(f.module, "IR verifier: invalid raw pointer write");
    write->lhs = saved_lhs;
    write->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid raw pointer write");
    write->lhs = saved_lhs;
    saved_type = write->type;
    write->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid result register");
    write->type = saved_type;

    uint32_t saved_count = readonly->count;
    readonly->count = 1;
    expect_invalid(f.module, "IR verifier: invalid raw pointer type");
    readonly->count = saved_count;
    saved_type = readonly->inner;
    readonly->inner = readonly->id;
    expect_invalid(f.module, "IR verifier: invalid raw pointer type");
    readonly->inner = saved_type;
    readonly->pointer_const = false;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    readonly->pointer_const = true;
    CcIrTypeInfo duplicate = *readonly;
    duplicate.id = f.module->type_count;
    duplicate.next = NULL;
    writable->next = &duplicate;
    f.module->type_count++;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    f.module->type_count--;
    writable->next = NULL;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    out = tmpfile();
    assert(out != NULL && cc_c_emit(out, f.module, &error));
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_slice_ir_and_verifier(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " let fixed = [20, 22]; var data = [0, 0];"
        " let frozen: []const i32 = slice(fixed);"
        " let view: []i32 = slice(data);"
        " view[0] = frozen[0];"
        " view[1] += frozen[1];"
        " let length: usize = view.len;"
        " return data[0] + data[1]; }");
    lower(&f);

    CcIrTypeInfo *readonly = NULL, *writable = NULL;
    for (CcIrTypeInfo *type = f.module->types; type != NULL;
         type = type->next) {
        if (type->kind != CC_IR_TYPE_SLICE) continue;
        if (type->slice_const) readonly = type;
        else writable = type;
    }
    assert(readonly != NULL && writable != NULL);
    assert(readonly->inner == CC_IR_I32 && writable->inner == CC_IR_I32);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *make = find_instruction(fn, CC_IR_MAKE_SLICE);
    CcIrInstruction *read = find_instruction(fn, CC_IR_GET_SLICE);
    CcIrInstruction *write = find_instruction(fn, CC_IR_STORE_SLICE);
    CcIrInstruction *length = find_instruction(fn, CC_IR_SLICE_LEN);
    assert(make != NULL && read != NULL && write != NULL &&
           length != NULL);

    FILE *out = tmpfile();
    assert(out != NULL && cc_ir_print(out, f.module));
    rewind(out);
    char printed[20000];
    size_t n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "= []const i32") != NULL);
    assert(strstr(printed, "= []i32") != NULL);
    assert(strstr(printed, "slice.from.slot") != NULL);
    assert(strstr(printed, "slice.get") != NULL);
    assert(strstr(printed, "slice.store") != NULL);
    assert(strstr(printed, "slice.len") != NULL);
    assert(fclose(out) == 0);

    uint32_t saved_slot = make->slot;
    make->slot = fn->slot_count;
    expect_invalid(f.module, "IR verifier: invalid slice constructor");
    make->slot = saved_slot;
    uint32_t saved_count = make->arg_count;
    make->arg_count = 999;
    expect_invalid(f.module, "IR verifier: invalid slice constructor");
    make->arg_count = saved_count;
    CcIrSlot *source_slot = fn->slots;
    while (source_slot != NULL && source_slot->id != make->slot)
        source_slot = source_slot->next;
    assert(source_slot != NULL && !source_slot->mutable);
    source_slot->mutable = true;
    expect_invalid(f.module, "IR verifier: invalid slice constructor");
    source_slot->mutable = false;
    CcIrType saved_type = read->type;
    read->type = CC_IR_BOOL;
    expect_invalid(f.module, "IR verifier: invalid slice read");
    read->type = saved_type;
    uint32_t saved_reg = read->lhs;
    read->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice read");
    read->lhs = saved_reg;
    saved_reg = write->lhs;
    write->lhs = read->lhs; /* The read uses the const slice. */
    expect_invalid(f.module, "IR verifier: invalid slice write");
    write->lhs = saved_reg;
    saved_reg = write->rhs;
    write->rhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice write");
    write->rhs = saved_reg;
    saved_reg = write->slot;
    write->slot = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice write");
    write->slot = saved_reg;
    saved_type = length->type;
    length->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid slice length");
    length->type = saved_type;
    saved_reg = length->lhs;
    length->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice length");
    length->lhs = saved_reg;

    saved_count = readonly->count;
    readonly->count = 3;
    expect_invalid(f.module, "IR verifier: invalid slice type");
    readonly->count = saved_count;
    saved_type = readonly->inner;
    readonly->inner = readonly->id;
    expect_invalid(f.module, "IR verifier: invalid slice type");
    readonly->inner = saved_type;
    readonly->pointer_const = true;
    expect_invalid(f.module, "IR verifier: invalid slice type");
    readonly->pointer_const = false;
    readonly->slice_const = false;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    readonly->slice_const = true;
    CcIrTypeInfo duplicate = *writable;
    duplicate.id = f.module->type_count;
    duplicate.next = NULL;
    writable->next = &duplicate;
    f.module->type_count++;
    expect_invalid(f.module, "IR verifier: duplicate composite type");
    f.module->type_count--;
    writable->next = NULL;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    out = tmpfile();
    assert(out != NULL && cc_c_emit(out, f.module, &error));
    rewind(out);
    n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "*data;") != NULL);
    assert(strstr(printed, "size_t len;") != NULL);
    assert(strstr(printed, "ccrt_index_i32") != NULL);
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_slice_subrange_ir_and_verifier(void)
{
    Fixture f;
    fixture_init(&f,
        "fn main() -> i32 {"
        " var data = [0, 20, 22, 0];"
        " let const_data = [1, 2, 3];"
        " let cold = const_data[0..1];"
        " let view: []i32 = data[1..3];"
        " let inner = view[0..2];"
        " inner[0] += cold[0];"
        " return inner[0] + inner[1]; }");
    lower(&f);
    CcIrFunction *fn = f.module->functions;
    CcIrInstruction *sub = find_instruction(fn, CC_IR_SUBSLICE);
    CcIrInstruction *array = find_instruction(fn, CC_IR_MAKE_ARRAY);
    CcIrInstruction *make = find_instruction(fn, CC_IR_MAKE_SLICE);
    assert(sub != NULL && make != NULL && array != NULL);
    assert(find_instruction(fn, CC_IR_GET_SLICE) != NULL);
    assert(find_instruction(fn, CC_IR_STORE_SLICE) != NULL);
    const CcIrTypeInfo *type = cc_ir_type_info(f.module, sub->type);
    assert(type != NULL && type->kind == CC_IR_TYPE_SLICE &&
           type->slice_const);
    const CcIrTypeInfo *make_type = cc_ir_type_info(f.module, make->type);
    assert(make_type != NULL && make_type->kind == CC_IR_TYPE_SLICE);

    FILE *out = tmpfile();
    assert(out != NULL && cc_ir_print(out, f.module));
    rewind(out);
    char printed[24000];
    size_t n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "slice.subrange") != NULL);
    assert(strstr(printed, "slice.from.slot") != NULL);
    assert(fclose(out) == 0);

    uint32_t saved_lhs = sub->lhs;
    sub->lhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice subrange");
    sub->lhs = array->result;
    expect_invalid(f.module, "IR verifier: invalid slice subrange");
    sub->lhs = saved_lhs;

    CcIrType saved_type = sub->type;
    sub->type = CC_IR_I32;
    expect_invalid(f.module, "IR verifier: invalid slice subrange");
    sub->type = saved_type;

    uint32_t saved_begin = sub->rhs;
    sub->rhs = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice subrange");
    sub->rhs = saved_begin;
    uint32_t saved_end = sub->slot;
    sub->slot = CC_IR_NONE;
    expect_invalid(f.module, "IR verifier: invalid slice subrange");
    sub->slot = saved_end;

    CcIrInstruction *inner = NULL;
    for (CcIrBlock *block = fn->blocks; block != NULL; block = block->next)
        for (CcIrInstruction *i = block->first; i != NULL; i = i->next)
            if (i->op == CC_IR_SUBSLICE && i != sub) inner = i;
    assert(inner != NULL);
    assert(inner->type != sub->type);
    sub->type = inner->type;
    expect_invalid(f.module, "IR verifier: invalid slice subrange");
    sub->type = saved_type;

    CcIrError error;
    assert(cc_ir_verify(f.module, &error));
    out = tmpfile();
    assert(out != NULL && cc_c_emit(out, f.module, &error));
    rewind(out);
    n = fread(printed, 1, sizeof(printed) - 1, out);
    printed[n] = '\0';
    assert(strstr(printed, "ccrt_subrange_i32") != NULL);
    assert(strstr(printed, ".data == NULL ? NULL") != NULL);
    assert(fclose(out) == 0);
    fixture_destroy(&f);
}

static void test_explicit_unsupported(void)
{
    Fixture f;
    fixture_init(&f,
        "fn unsupported(x: []u8) {}");
    CcFunctionDeclaration *program = cc_parser_parse_program(&f.parser);
    assert(!f.parser.failed);
    CcSemanticError semantic_error;
    const CcSourceFile *file = cc_source_get(&f.sources, f.file_id);
    assert(cc_semantic_check(file, program, &f.arena, &semantic_error));
    CcIrError error;
    assert(!cc_ir_lower(file, program, &f.arena, &f.module, &error));
    assert(error.message != NULL);
    assert(strcmp(error.message,
           "IR backend currently supports only i32, usize, bool, void, tuples, optionals, fixed arrays, structs, pointers and slices") == 0);
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
    test_tuple_destructuring_and_type_interning();
    test_invalid_tuple_ir();
    test_invalid_optional_ir();
    test_optional_access_ir_and_verifier();
    test_for_range_cfg_and_bounds_once();
    test_bitwise_wrapping_and_compound_ir();
    test_array_ir_and_verifier();
    test_struct_ir_and_verifier();
    test_nominal_and_forward_struct_types();
    test_pointer_ir_and_verifier();
    test_slice_ir_and_verifier();
    test_slice_subrange_ir_and_verifier();
    test_explicit_unsupported();
    test_bad_inputs();
    puts("IR: all tests passed");
    return 0;
}
