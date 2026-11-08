#include "parser/parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    CcSourceManager sources;
    CcFileId id;
    CcLexer lexer;
    CcArena arena;
    CcParser parser;
} ParserFixture;

static void fixture_init(ParserFixture *fixture, const char *source)
{
    CcSourceError error;

    cc_source_manager_init(&fixture->sources);
    assert(cc_source_add_memory(&fixture->sources, "tuple test",
                                (const unsigned char *)source, strlen(source),
                                &fixture->id, &error));

    const CcSourceFile *file = cc_source_get(&fixture->sources, fixture->id);

    assert(file != NULL);
    cc_lexer_init(&fixture->lexer, file, fixture->id);
    cc_arena_init(&fixture->arena);
    cc_parser_init(&fixture->parser, &fixture->lexer, &fixture->arena);
}

static void fixture_destroy(ParserFixture *fixture)
{
    cc_arena_destroy(&fixture->arena);
    cc_source_manager_destroy(&fixture->sources);
}

static void assert_span(const ParserFixture *fixture, CcSourceSpan span,
                        const char *expected)
{
    const CcSourceFile *file = cc_source_get(&fixture->sources, fixture->id);
    size_t length = strlen(expected);

    assert(file != NULL);
    assert(span.file_id == fixture->id);
    assert(span.end >= span.start);
    assert(span.end <= file->length);
    assert(span.end - span.start == length);
    assert(memcmp(file->bytes + span.start, expected, length) == 0);
}

static void test_divmod_from_spec(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn divmod(a: i32, b: i32) -> (i32, i32) { "
                 "return (a / b, a % b); } "
                 "pub fn main() -> i32 { "
                 "let (q, r) = divmod(17, 5); return q + r; }");

    CcFunctionDeclaration *divmod = cc_parser_parse_program(&fixture.parser);

    assert(divmod != NULL);
    assert(!fixture.parser.failed);
    assert(divmod->return_type != NULL);
    assert(divmod->return_type->kind == CC_AST_TYPE_TUPLE);
    assert_span(&fixture, divmod->return_type->span, "(i32, i32)");

    CcTupleTypeElement *type = divmod->return_type->tuple_elements;

    assert(type != NULL);
    assert(type->type->kind == CC_AST_TYPE_NAMED);
    assert_span(&fixture, type->type->span, "i32");
    type = type->next;
    assert(type != NULL);
    assert(type->type->kind == CC_AST_TYPE_NAMED);
    assert_span(&fixture, type->type->span, "i32");
    assert(type->next == NULL);

    CcStatement *ret = divmod->body->first;

    assert(ret != NULL);
    assert(ret->kind == CC_AST_STATEMENT_RETURN);
    assert(ret->return_expression != NULL);
    assert(ret->return_expression->kind == CC_AST_EXPRESSION_TUPLE);
    assert_span(&fixture, ret->return_expression->span, "(a / b, a % b)");

    CcTupleExpressionElement *value = ret->return_expression->tuple_elements;

    assert(value != NULL);
    assert(value->value->kind == CC_AST_EXPRESSION_BINARY);
    assert(value->value->binary.operator == CC_AST_BINARY_OPERATOR_SLASH);
    assert_span(&fixture, value->value->span, "a / b");
    value = value->next;
    assert(value != NULL);
    assert(value->value->kind == CC_AST_EXPRESSION_BINARY);
    assert(value->value->binary.operator == CC_AST_BINARY_OPERATOR_PERCENT);
    assert_span(&fixture, value->value->span, "a % b");
    assert(value->next == NULL);

    CcFunctionDeclaration *main_fn = divmod->next;

    assert(main_fn != NULL);
    assert(main_fn->is_public);
    assert(main_fn->next == NULL);

    CcStatement *binding = main_fn->body->first;

    assert(binding != NULL);
    assert(binding->kind == CC_AST_STATEMENT_LET);
    assert(!binding->let.mutable);
    assert(binding->let.name.file_id == CC_FILE_ID_INVALID);
    assert(binding->let.pattern->kind == CC_AST_PATTERN_TUPLE);
    assert_span(&fixture, binding->let.pattern->span, "(q, r)");

    CcTuplePatternElement *element = binding->let.pattern->tuple_elements;

    assert(element != NULL);
    assert(element->pattern->kind == CC_AST_PATTERN_NAME);
    assert_span(&fixture, element->pattern->name, "q");
    element = element->next;
    assert(element != NULL);
    assert(element->pattern->kind == CC_AST_PATTERN_NAME);
    assert_span(&fixture, element->pattern->name, "r");
    assert(element->next == NULL);

    assert(binding->let.initializer->kind == CC_AST_EXPRESSION_CALL);
    assert_span(&fixture, binding->let.initializer->span, "divmod(17, 5)");
    assert(binding->next != NULL);
    assert(binding->next->kind == CC_AST_STATEMENT_RETURN);
    assert(binding->next->return_expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(binding->next->next == NULL);

    fixture_destroy(&fixture);
}

static void test_tuple_types_and_grouping(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f(x: (i32), y: (i32,), z: ?[2](u8, []const u8)) "
                 "-> (*const (i32, i32), ()) {}");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcParameter *x = function->parameters;

    assert(x != NULL);
    assert(x->type->kind == CC_AST_TYPE_PARENTHESIZED);
    assert_span(&fixture, x->type->span, "(i32)");
    assert(x->type->parenthesized_inner->kind == CC_AST_TYPE_NAMED);

    CcParameter *y = x->next;

    assert(y != NULL);
    assert(y->type->kind == CC_AST_TYPE_TUPLE);
    assert_span(&fixture, y->type->span, "(i32,)");
    assert(y->type->tuple_elements != NULL);
    assert(y->type->tuple_elements->next == NULL);

    CcParameter *z = y->next;

    assert(z != NULL);
    assert(z->type->kind == CC_AST_TYPE_OPTIONAL);
    assert_span(&fixture, z->type->span, "?[2](u8, []const u8)");
    assert(z->type->optional_inner->kind == CC_AST_TYPE_ARRAY);
    assert(z->type->optional_inner->array.length->integer_literal.value == 2);
    assert(z->type->optional_inner->array.element->kind == CC_AST_TYPE_TUPLE);
    assert(z->next == NULL);

    CcTupleTypeElement *nested =
        z->type->optional_inner->array.element->tuple_elements;

    assert(nested != NULL);
    assert(nested->type->kind == CC_AST_TYPE_NAMED);
    nested = nested->next;
    assert(nested != NULL);
    assert(nested->type->kind == CC_AST_TYPE_SLICE);
    assert(nested->type->slice_element_const);
    assert(nested->next == NULL);

    CcType *return_type = function->return_type;

    assert(return_type != NULL);
    assert(return_type->kind == CC_AST_TYPE_TUPLE);
    assert_span(&fixture, return_type->span, "(*const (i32, i32), ())");

    CcTupleTypeElement *first = return_type->tuple_elements;

    assert(first != NULL);
    assert(first->type->kind == CC_AST_TYPE_POINTER);
    assert(first->type->pointer_pointee_const);
    assert(first->type->pointer_inner->kind == CC_AST_TYPE_TUPLE);
    CcTupleTypeElement *second = first->next;
    assert(second != NULL);
    assert(second->type->kind == CC_AST_TYPE_TUPLE);
    assert(second->type->tuple_elements == NULL);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_tuple_expressions_and_member_access(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f() { let x = (1, (2, 3), 4,); "
                 "return ((x.0, x.1.0)).1; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *binding = function->body->first;

    assert(binding != NULL);
    assert(binding->kind == CC_AST_STATEMENT_LET);
    assert(binding->let.pattern->kind == CC_AST_PATTERN_NAME);
    assert_span(&fixture, binding->let.name, "x");
    assert(binding->let.initializer->kind == CC_AST_EXPRESSION_TUPLE);
    assert_span(&fixture, binding->let.initializer->span, "(1, (2, 3), 4,)");

    CcTupleExpressionElement *element = binding->let.initializer->tuple_elements;

    assert(element != NULL);
    assert(element->value->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);
    element = element->next;
    assert(element != NULL);
    assert(element->value->kind == CC_AST_EXPRESSION_TUPLE);
    assert_span(&fixture, element->value->span, "(2, 3)");
    assert(element->value->tuple_elements->next->next == NULL);
    element = element->next;
    assert(element != NULL);
    assert(element->value->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(element->value->integer_literal.value == 4);
    assert(element->next == NULL);

    CcStatement *ret = binding->next;

    assert(ret != NULL);
    assert(ret->kind == CC_AST_STATEMENT_RETURN);
    assert(ret->return_expression->kind == CC_AST_EXPRESSION_TUPLE_MEMBER);
    assert_span(&fixture, ret->return_expression->span, "((x.0, x.1.0)).1");
    assert(ret->return_expression->tuple_member.index_value.value == 1);
    assert(ret->return_expression->tuple_member.operand->kind ==
           CC_AST_EXPRESSION_PARENTHESIZED);
    assert(ret->return_expression->tuple_member.operand->
           parenthesized_inner->kind == CC_AST_EXPRESSION_TUPLE);
    assert(ret->next == NULL);

    fixture_destroy(&fixture);
}

static void test_tuple_value_arity_and_call_arguments(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { consume((), (1), (1,), (1, 2,)); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    CcExpression *call = function->body->first->expression;
    assert(call->kind == CC_AST_EXPRESSION_CALL);

    CcCallArgument *argument = call->call.arguments;
    assert(argument->value->kind == CC_AST_EXPRESSION_TUPLE);
    assert(argument->value->tuple_elements == NULL);
    assert_span(&fixture, argument->value->span, "()");

    argument = argument->next;
    assert(argument->value->kind == CC_AST_EXPRESSION_PARENTHESIZED);
    assert_span(&fixture, argument->value->span, "(1)");

    argument = argument->next;
    assert(argument->value->kind == CC_AST_EXPRESSION_TUPLE);
    assert(argument->value->tuple_elements->next == NULL);
    assert_span(&fixture, argument->value->span, "(1,)");

    argument = argument->next;
    assert(argument->value->kind == CC_AST_EXPRESSION_TUPLE);
    assert(argument->value->tuple_elements->next != NULL);
    assert(argument->value->tuple_elements->next->next == NULL);
    assert_span(&fixture, argument->value->span, "(1, 2,)");
    assert(argument->next == NULL);

    fixture_destroy(&fixture);
}

static void test_nested_tuple_patterns_and_annotations(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f() { let ((a, b), c): ((i32, i32), ?i32) = "
                 "((1, 2), some(3)); var (d,): (i32,) = (4,); "
                 "let () = (); let (e) = 5; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *binding = function->body->first;

    assert(binding->kind == CC_AST_STATEMENT_LET);
    assert(!binding->let.mutable);
    assert(binding->let.pattern->kind == CC_AST_PATTERN_TUPLE);
    assert_span(&fixture, binding->let.pattern->span, "((a, b), c)");
    assert(binding->let.type->kind == CC_AST_TYPE_TUPLE);
    assert_span(&fixture, binding->let.type->span, "((i32, i32), ?i32)");
    assert(binding->let.initializer->kind == CC_AST_EXPRESSION_TUPLE);
    assert_span(&fixture, binding->let.initializer->span, "((1, 2), some(3))");

    CcTuplePatternElement *first = binding->let.pattern->tuple_elements;

    assert(first != NULL);
    assert(first->pattern->kind == CC_AST_PATTERN_TUPLE);
    assert_span(&fixture, first->pattern->span, "(a, b)");
    assert(first->pattern->tuple_elements->pattern->kind == CC_AST_PATTERN_NAME);
    assert(first->pattern->tuple_elements->next->pattern->kind ==
           CC_AST_PATTERN_NAME);
    assert(first->pattern->tuple_elements->next->next == NULL);
    assert(first->next != NULL);
    assert(first->next->pattern->kind == CC_AST_PATTERN_NAME);
    assert_span(&fixture, first->next->pattern->name, "c");
    assert(first->next->next == NULL);

    binding = binding->next;
    assert(binding != NULL);
    assert(binding->let.mutable);
    assert(binding->let.pattern->kind == CC_AST_PATTERN_TUPLE);
    assert(binding->let.pattern->tuple_elements->next == NULL);
    assert(binding->let.type->kind == CC_AST_TYPE_TUPLE);
    assert(binding->let.initializer->kind == CC_AST_EXPRESSION_TUPLE);

    binding = binding->next;
    assert(binding != NULL);
    assert(binding->let.pattern->kind == CC_AST_PATTERN_TUPLE);
    assert(binding->let.pattern->tuple_elements == NULL);
    assert(binding->let.initializer->kind == CC_AST_EXPRESSION_TUPLE);
    assert(binding->let.initializer->tuple_elements == NULL);

    binding = binding->next;
    assert(binding != NULL);
    assert(binding->let.pattern->kind == CC_AST_PATTERN_PARENTHESIZED);
    assert_span(&fixture, binding->let.pattern->span, "(e)");
    assert(binding->let.pattern->parenthesized_inner->kind ==
           CC_AST_PATTERN_NAME);
    assert(binding->let.name.file_id == CC_FILE_ID_INVALID);
    assert(binding->next == NULL);

    fixture_destroy(&fixture);
}

static void test_tuple_syntax_errors(void)
{
    static const char *const invalid[] = {
        "fn f() -> (i32,, i32) {}",
        "fn f(x: (i32 i32)) {}",
        "fn f() -> (i32, i32 {}",
        "fn f() { return (1,, 2); }",
        "fn f() { return (1, 2; }",
        "fn f() { return (1, 2,; }",
        "fn f() { let (x,, y) = value; }",
        "fn f() { let (x, y = value; }",
        "fn f() { let (1, y) = value; }",
        "fn f() { let (,x) = value; }",
        "fn f() { let (x, y): (i32, ) = ; }",
        "fn f() { let (x, y) = (1, 2) }",
        "fn f() { var (x, y) = (1,,); }"
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        ParserFixture fixture;

        fixture_init(&fixture, invalid[i]);
        assert(cc_parser_parse_program(&fixture.parser) == NULL);
        assert(fixture.parser.failed);
        assert(fixture.parser.error != NULL);
        fixture_destroy(&fixture);
    }
}

static void test_tuple_missing_delimiters_at_eof(void)
{
    static const char *const sources[] = {
        "fn f() -> (i32",
        "fn f() -> (i32,",
        "fn f() { return (1",
        "fn f() { return (1, 2",
        "fn f() { let (a",
        "fn f() { let (a, b"
    };
    static const char *const errors[] = {
        "expected `)` after type",
        "expected a type",
        "expected `)` after expression",
        "expected `)` after tuple expression",
        "expected `)` after pattern",
        "expected `)` after tuple pattern"
    };

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        ParserFixture fixture;

        fixture_init(&fixture, sources[i]);
        assert(cc_parser_parse_program(&fixture.parser) == NULL);
        assert(fixture.parser.failed);
        assert(fixture.parser.error != NULL);
        assert(strcmp(fixture.parser.error, errors[i]) == 0);
        assert(fixture.parser.error_span.file_id == fixture.id);
        assert(fixture.parser.error_span.start == strlen(sources[i]));
        assert(fixture.parser.error_span.end == strlen(sources[i]));
        fixture_destroy(&fixture);
    }
}

int main(void)
{
    test_divmod_from_spec();
    test_tuple_types_and_grouping();
    test_tuple_expressions_and_member_access();
    test_tuple_value_arity_and_call_arguments();
    test_nested_tuple_patterns_and_annotations();
    test_tuple_syntax_errors();
    test_tuple_missing_delimiters_at_eof();

    puts("parser tuples: all tests passed");
    return 0;
}
