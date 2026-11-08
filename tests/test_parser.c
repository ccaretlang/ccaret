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

static void fixture_init(ParserFixture *fixture, const char *text)
{
    CcSourceError error;

    cc_source_manager_init(&fixture->sources);
    assert(cc_source_add_memory(&fixture->sources, "test",
                                (const unsigned char *)text, strlen(text),
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

static void test_simple_function(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn main() -> i32 {\n    return 42;\n}");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->next == NULL);
    assert(function->is_public == false);
    assert(function->span.start == 0 && function->span.end == 35);
    assert(function->name.start == 3 && function->name.end == 7);
    assert(function->parameters == NULL);
    assert(function->return_type != NULL);
    assert(function->return_type->kind == CC_AST_TYPE_NAMED);
    assert(function->return_type->span.start == 13);
    assert(function->return_type->span.end == 16);
    assert(function->body != NULL);
    assert(function->body->span.start == 17);
    assert(function->body->span.end == 35);
    assert(function->body->first != NULL);
    assert(function->body->first->kind == CC_AST_STATEMENT_RETURN);
    assert(function->body->first->next == NULL);
    assert(function->body->first->span.start == 23);
    assert(function->body->first->span.end == 33);
    assert(function->body->first->return_expression != NULL);
    assert(function->body->first->return_expression->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(function->body->first->return_expression->span.start == 30);
    assert(function->body->first->return_expression->span.end == 32);
    assert(function->body->first->return_expression->integer_literal.value == 42);
    assert(function->body->first->return_expression->integer_literal.overflow ==
           false);

    fixture_destroy(&fixture);
}

static void test_empty_block(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() {}");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->name.start == 3 && function->name.end == 4);
    assert(function->parameters == NULL);
    assert(function->return_type == NULL);
    assert(function->body != NULL);
    assert(function->body->first == NULL);

    fixture_destroy(&fixture);
}

static void test_return_no_expression(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { return; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->body != NULL);
    assert(function->body->first != NULL);
    assert(function->body->first->kind == CC_AST_STATEMENT_RETURN);
    assert(function->body->first->return_expression == NULL);

    fixture_destroy(&fixture);
}

static void test_parameters_and_identifier(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn add(a: i32, b: i32) { return a; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->parameters != NULL);
    assert(function->return_type == NULL);

    CcParameter *first = function->parameters;

    assert(first->name.start == 7 && first->name.end == 8);
    assert(first->type != NULL);
    assert(first->type->kind == CC_AST_TYPE_NAMED);
    assert(first->type->span.start == 10);
    assert(first->type->span.end == 13);
    assert(first->span.start == 7 && first->span.end == 13);

    CcParameter *second = first->next;

    assert(second != NULL);
    assert(second->name.start == 15 && second->name.end == 16);
    assert(second->type != NULL);
    assert(second->type->span.start == 18);
    assert(second->type->span.end == 21);
    assert(second->span.start == 15 && second->span.end == 21);
    assert(second->next == NULL);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_RETURN);
    assert(statement->return_expression != NULL);
    assert(statement->return_expression->kind ==
           CC_AST_EXPRESSION_IDENTIFIER);
    assert(statement->return_expression->span.start == 32);
    assert(statement->return_expression->span.end == 33);

    fixture_destroy(&fixture);
}

static void test_optional_types(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f(arg: ?i32) -> ?i32 { let value: ?i32 = some(3); return none; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcParameter *parameter = function->parameters;

    assert(parameter != NULL);
    assert(parameter->span.start == 5 && parameter->span.end == 14);
    assert(parameter->type->kind == CC_AST_TYPE_OPTIONAL);
    assert(parameter->type->span.file_id == fixture.id);
    assert(parameter->type->span.start == 10);
    assert(parameter->type->span.end == 14);
    assert(parameter->type->optional_inner != NULL);
    assert(parameter->type->optional_inner->kind == CC_AST_TYPE_NAMED);
    assert(parameter->type->optional_inner->span.start == 11);
    assert(parameter->type->optional_inner->span.end == 14);
    assert(parameter->type->optional_inner->optional_inner == NULL);
    assert(parameter->next == NULL);

    CcType *return_type = function->return_type;

    assert(return_type != NULL);
    assert(return_type->kind == CC_AST_TYPE_OPTIONAL);
    assert(return_type->span.start == 19 && return_type->span.end == 23);
    assert(return_type->optional_inner->kind == CC_AST_TYPE_NAMED);
    assert(return_type->optional_inner->span.start == 20);
    assert(return_type->optional_inner->span.end == 23);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_LET);
    assert(statement->let.type != NULL);
    assert(statement->let.type->kind == CC_AST_TYPE_OPTIONAL);
    assert(statement->let.type->span.start == 37);
    assert(statement->let.type->span.end == 41);
    assert(statement->let.type->optional_inner->kind == CC_AST_TYPE_NAMED);
    assert(statement->let.type->optional_inner->span.start == 38);
    assert(statement->let.type->optional_inner->span.end == 41);
    assert(statement->let.initializer->kind == CC_AST_EXPRESSION_SOME);
    assert(statement->next != NULL);
    assert(statement->next->kind == CC_AST_STATEMENT_RETURN);
    assert(statement->next->return_expression->kind ==
           CC_AST_EXPRESSION_NONE_LITERAL);
    assert(statement->next->next == NULL);

    fixture_destroy(&fixture);
}

static void test_nested_optional_types(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f(value: ??bool) -> ??i32 {}");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcType *parameter_type = function->parameters->type;

    assert(parameter_type->kind == CC_AST_TYPE_OPTIONAL);
    assert(parameter_type->span.start == 12 && parameter_type->span.end == 18);
    assert(parameter_type->optional_inner->kind == CC_AST_TYPE_OPTIONAL);
    assert(parameter_type->optional_inner->span.start == 13);
    assert(parameter_type->optional_inner->span.end == 18);
    assert(parameter_type->optional_inner->optional_inner->kind ==
           CC_AST_TYPE_NAMED);
    assert(parameter_type->optional_inner->optional_inner->span.start == 14);
    assert(parameter_type->optional_inner->optional_inner->span.end == 18);

    CcType *return_type = function->return_type;

    assert(return_type != NULL);
    assert(return_type->kind == CC_AST_TYPE_OPTIONAL);
    assert(return_type->span.start == 23 && return_type->span.end == 28);
    assert(return_type->optional_inner->kind == CC_AST_TYPE_OPTIONAL);
    assert(return_type->optional_inner->span.start == 24);
    assert(return_type->optional_inner->span.end == 28);
    assert(return_type->optional_inner->optional_inner->kind ==
           CC_AST_TYPE_NAMED);
    assert(return_type->optional_inner->optional_inner->span.start == 25);
    assert(return_type->optional_inner->optional_inner->span.end == 28);

    fixture_destroy(&fixture);
}

static void test_optional_type_eof(void)
{
    static const char *source = "fn f() -> ?";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(strcmp(fixture.parser.error, "expected a type") == 0);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == strlen(source));
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_pointer_types(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f(p: *i32, q: ?*u8) -> *?i32 { let x: **u8 = q; return p; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcParameter *first = function->parameters;

    assert(first != NULL);
    assert(first->span.start == 5 && first->span.end == 12);
    assert(first->type->kind == CC_AST_TYPE_POINTER);
    assert(first->type->span.file_id == fixture.id);
    assert(first->type->span.start == 8 && first->type->span.end == 12);
    assert(first->type->pointer_inner != NULL);
    assert(first->type->pointer_inner->kind == CC_AST_TYPE_NAMED);
    assert(first->type->pointer_inner->span.start == 9);
    assert(first->type->pointer_inner->span.end == 12);

    CcParameter *second = first->next;

    assert(second != NULL);
    assert(second->span.start == 14 && second->span.end == 21);
    assert(second->type->kind == CC_AST_TYPE_OPTIONAL);
    assert(second->type->span.start == 17 && second->type->span.end == 21);
    assert(second->type->optional_inner->kind == CC_AST_TYPE_POINTER);
    assert(second->type->optional_inner->span.start == 18);
    assert(second->type->optional_inner->span.end == 21);
    assert(second->type->optional_inner->pointer_inner->kind ==
           CC_AST_TYPE_NAMED);
    assert(second->type->optional_inner->pointer_inner->span.start == 19);
    assert(second->type->optional_inner->pointer_inner->span.end == 21);
    assert(second->next == NULL);

    CcType *return_type = function->return_type;

    assert(return_type != NULL);
    assert(return_type->kind == CC_AST_TYPE_POINTER);
    assert(return_type->span.start == 26 && return_type->span.end == 31);
    assert(return_type->pointer_inner->kind == CC_AST_TYPE_OPTIONAL);
    assert(return_type->pointer_inner->span.start == 27);
    assert(return_type->pointer_inner->span.end == 31);
    assert(return_type->pointer_inner->optional_inner->kind ==
           CC_AST_TYPE_NAMED);
    assert(return_type->pointer_inner->optional_inner->span.start == 28);
    assert(return_type->pointer_inner->optional_inner->span.end == 31);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_LET);
    assert(statement->let.type != NULL);
    assert(statement->let.type->kind == CC_AST_TYPE_POINTER);
    assert(statement->let.type->span.start == 41);
    assert(statement->let.type->span.end == 45);
    assert(statement->let.type->pointer_inner->kind == CC_AST_TYPE_POINTER);
    assert(statement->let.type->pointer_inner->span.start == 42);
    assert(statement->let.type->pointer_inner->span.end == 45);
    assert(statement->let.type->pointer_inner->pointer_inner->kind ==
           CC_AST_TYPE_NAMED);
    assert(statement->let.type->pointer_inner->pointer_inner->span.start == 43);
    assert(statement->let.type->pointer_inner->pointer_inner->span.end == 45);
    assert(statement->let.initializer->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(statement->next != NULL);
    assert(statement->next->kind == CC_AST_STATEMENT_RETURN);
    assert(statement->next->next == NULL);

    fixture_destroy(&fixture);
}

static void test_pointer_type_eof(void)
{
    static const char *const sources[] = {
        "fn f() -> *",
        "fn f() -> ?*"
    };

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        ParserFixture fixture;

        fixture_init(&fixture, sources[i]);

        assert(cc_parser_parse_program(&fixture.parser) == NULL);
        assert(fixture.parser.failed);
        assert(fixture.parser.error != NULL);
        assert(strcmp(fixture.parser.error, "expected a type") == 0);
        assert(fixture.parser.error_span.file_id == fixture.id);
        assert(fixture.parser.error_span.start == strlen(sources[i]));
        assert(fixture.parser.error_span.end == strlen(sources[i]));

        fixture_destroy(&fixture);
    }
}

static void test_multiple_functions(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn a() {} fn b() {}");

    CcFunctionDeclaration *first = cc_parser_parse_program(&fixture.parser);

    assert(first != NULL);
    assert(!fixture.parser.failed);
    assert(first->name.start == 3 && first->name.end == 4);

    CcFunctionDeclaration *second = first->next;

    assert(second != NULL);
    assert(second->name.start == 13 && second->name.end == 14);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_public_function(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "pub fn f(x: i32) -> i32 { return x; } fn g() {}");

    CcFunctionDeclaration *first = cc_parser_parse_program(&fixture.parser);

    assert(first != NULL);
    assert(!fixture.parser.failed);
    assert(first->is_public);
    assert(first->span.file_id == fixture.id);
    assert(first->span.start == 0 && first->span.end == 37);
    assert(first->name.start == 7 && first->name.end == 8);
    assert(first->parameters != NULL);
    assert(first->parameters->span.start == 9);
    assert(first->parameters->span.end == 15);
    assert(first->parameters->next == NULL);
    assert(first->return_type != NULL);
    assert(first->return_type->span.start == 20);
    assert(first->return_type->span.end == 23);
    assert(first->body != NULL);
    assert(first->body->span.start == 24 && first->body->span.end == 37);
    assert(first->body->first != NULL);
    assert(first->body->first->kind == CC_AST_STATEMENT_RETURN);

    CcFunctionDeclaration *second = first->next;

    assert(second != NULL);
    assert(!second->is_public);
    assert(second->span.start == 38 && second->span.end == 47);
    assert(second->name.start == 41 && second->name.end == 42);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_public_function_eof(void)
{
    static const char *source = "pub";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(strcmp(fixture.parser.error, "expected function declaration") == 0);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == strlen(source));
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_binary_precedence(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn main() -> i32 { return 1 + 2 * 3; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcExpression *expression = function->body->first->return_expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->binary.operator == CC_AST_BINARY_OPERATOR_PLUS);
    assert(expression->binary.left->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(expression->binary.right->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->binary.right->binary.operator ==
           CC_AST_BINARY_OPERATOR_STAR);

    fixture_destroy(&fixture);
}

static void test_parenthesized_expression(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn main() -> i32 { return (1 + 2) * 3; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcExpression *expression = function->body->first->return_expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->binary.operator == CC_AST_BINARY_OPERATOR_STAR);
    assert(expression->binary.left->kind ==
           CC_AST_EXPRESSION_PARENTHESIZED);
    assert(expression->binary.left->parenthesized_inner != NULL);
    assert(expression->binary.left->parenthesized_inner->kind ==
           CC_AST_EXPRESSION_BINARY);
    assert(expression->binary.left->parenthesized_inner->binary.operator ==
           CC_AST_BINARY_OPERATOR_PLUS);
    assert(expression->binary.right->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);

    fixture_destroy(&fixture);
}

static void test_unary_operators(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { return -1; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcExpression *expression = function->body->first->return_expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_UNARY);
    assert(expression->unary.operator == CC_AST_UNARY_OPERATOR_MINUS);
    assert(expression->unary.operand->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(expression->unary.operand->integer_literal.value == 1);

    fixture_destroy(&fixture);
}

static void test_boolean_literals(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { if true { return false; } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_IF);
    assert(statement->next == NULL);

    CcExpression *condition = statement->if_statement->condition;

    assert(condition != NULL);
    assert(condition->kind == CC_AST_EXPRESSION_BOOLEAN_LITERAL);
    assert(condition->boolean_literal == true);
    assert(condition->span.file_id == fixture.id);
    assert(condition->span.start == 12 && condition->span.end == 16);

    CcStatement *body_statement = statement->if_statement->then_block->first;

    assert(body_statement != NULL);
    assert(body_statement->kind == CC_AST_STATEMENT_RETURN);
    assert(body_statement->next == NULL);

    CcExpression *value = body_statement->return_expression;

    assert(value != NULL);
    assert(value->kind == CC_AST_EXPRESSION_BOOLEAN_LITERAL);
    assert(value->boolean_literal == false);
    assert(value->span.file_id == fixture.id);
    assert(value->span.start == 26 && value->span.end == 31);

    fixture_destroy(&fixture);
}

static void test_boolean_operator_precedence(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let flag = !false && true; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_LET);

    CcExpression *expression = statement->let.initializer;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->binary.operator == CC_AST_BINARY_OPERATOR_AND_AND);
    assert(expression->span.start == 20 && expression->span.end == 34);

    CcExpression *left = expression->binary.left;

    assert(left != NULL);
    assert(left->kind == CC_AST_EXPRESSION_UNARY);
    assert(left->unary.operator == CC_AST_UNARY_OPERATOR_NOT);
    assert(left->unary.operand->kind == CC_AST_EXPRESSION_BOOLEAN_LITERAL);
    assert(left->unary.operand->boolean_literal == false);
    assert(left->unary.operand->span.start == 21);
    assert(left->unary.operand->span.end == 26);

    CcExpression *right = expression->binary.right;

    assert(right != NULL);
    assert(right->kind == CC_AST_EXPRESSION_BOOLEAN_LITERAL);
    assert(right->boolean_literal == true);
    assert(right->span.start == 30 && right->span.end == 34);

    fixture_destroy(&fixture);
}

static void test_boolean_expression_eof(void)
{
    static const char *source = "fn f() { return true &&";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(strcmp(fixture.parser.error, "expected expression") == 0);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == strlen(source));
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_string_literal(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let s = \"a\\n\\\"b\"; return s; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_LET);
    assert(first->let.initializer != NULL);
    assert(first->let.initializer->kind == CC_AST_EXPRESSION_STRING_LITERAL);
    assert(first->let.initializer->span.file_id == fixture.id);
    assert(first->let.initializer->span.start == 17);
    assert(first->let.initializer->span.end == 25);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_RETURN);
    assert(second->return_expression != NULL);
    assert(second->return_expression->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_string_call_arguments(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { log(\"ok\", \"\"); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->expression != NULL);
    assert(statement->expression->kind == CC_AST_EXPRESSION_CALL);

    CcCallArgument *first = statement->expression->call.arguments;

    assert(first != NULL);
    assert(first->value->kind == CC_AST_EXPRESSION_STRING_LITERAL);
    assert(first->value->span.start == 13 && first->value->span.end == 17);

    CcCallArgument *second = first->next;

    assert(second != NULL);
    assert(second->value->kind == CC_AST_EXPRESSION_STRING_LITERAL);
    assert(second->value->span.start == 19 && second->value->span.end == 21);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_unterminated_string_literal(void)
{
    static const char *source = "fn f() { return \"unterminated";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(fixture.lexer.error == CC_LEX_ERROR_UNTERMINATED_STRING);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == 16);
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_c_string_literal(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let s = c\"a\\n\"; return s; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_LET);
    assert(first->let.initializer != NULL);
    assert(first->let.initializer->kind == CC_AST_EXPRESSION_C_STRING_LITERAL);
    assert(first->let.initializer->span.file_id == fixture.id);
    assert(first->let.initializer->span.start == 17);
    assert(first->let.initializer->span.end == 23);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_RETURN);
    assert(second->return_expression->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_c_string_call_arguments(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { log(c\"ok\", \"\", c\"\"); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->expression->kind == CC_AST_EXPRESSION_CALL);

    CcCallArgument *first = statement->expression->call.arguments;

    assert(first != NULL);
    assert(first->value->kind == CC_AST_EXPRESSION_C_STRING_LITERAL);
    assert(first->value->span.start == 13 && first->value->span.end == 18);

    CcCallArgument *second = first->next;

    assert(second != NULL);
    assert(second->value->kind == CC_AST_EXPRESSION_STRING_LITERAL);
    assert(second->value->span.start == 20 && second->value->span.end == 22);

    CcCallArgument *third = second->next;

    assert(third != NULL);
    assert(third->value->kind == CC_AST_EXPRESSION_C_STRING_LITERAL);
    assert(third->value->span.start == 24 && third->value->span.end == 27);
    assert(third->next == NULL);

    fixture_destroy(&fixture);
}

static void test_unterminated_c_string_literal(void)
{
    static const char *source = "fn f() { return c\"unterminated";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(fixture.lexer.error == CC_LEX_ERROR_UNTERMINATED_STRING);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == 16);
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_byte_literal(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let ch = 'A'; return '\\n'; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_LET);
    assert(first->let.initializer != NULL);
    assert(first->let.initializer->kind == CC_AST_EXPRESSION_BYTE_LITERAL);
    assert(first->let.initializer->span.file_id == fixture.id);
    assert(first->let.initializer->span.start == 18);
    assert(first->let.initializer->span.end == 21);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_RETURN);
    assert(second->return_expression != NULL);
    assert(second->return_expression->kind == CC_AST_EXPRESSION_BYTE_LITERAL);
    assert(second->return_expression->span.start == 30);
    assert(second->return_expression->span.end == 34);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_byte_call_arguments(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { put('\\x41', '\\u{41}', '\\0'); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->expression->kind == CC_AST_EXPRESSION_CALL);

    CcCallArgument *first = statement->expression->call.arguments;

    assert(first != NULL);
    assert(first->value->kind == CC_AST_EXPRESSION_BYTE_LITERAL);
    assert(first->value->span.start == 13 && first->value->span.end == 19);

    CcCallArgument *second = first->next;

    assert(second != NULL);
    assert(second->value->kind == CC_AST_EXPRESSION_BYTE_LITERAL);
    assert(second->value->span.start == 21 && second->value->span.end == 29);

    CcCallArgument *third = second->next;

    assert(third != NULL);
    assert(third->value->kind == CC_AST_EXPRESSION_BYTE_LITERAL);
    assert(third->value->span.start == 31 && third->value->span.end == 35);
    assert(third->next == NULL);

    fixture_destroy(&fixture);
}

static void test_unterminated_byte_literal(void)
{
    static const char *source = "fn f() { return 'A";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(fixture.lexer.error == CC_LEX_ERROR_UNTERMINATED_BYTE_LITERAL);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == 16);
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_none_literal(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let x = none; return none; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_LET);
    assert(first->let.initializer != NULL);
    assert(first->let.initializer->kind == CC_AST_EXPRESSION_NONE_LITERAL);
    assert(first->let.initializer->span.file_id == fixture.id);
    assert(first->let.initializer->span.start == 17);
    assert(first->let.initializer->span.end == 21);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_RETURN);
    assert(second->return_expression != NULL);
    assert(second->return_expression->kind == CC_AST_EXPRESSION_NONE_LITERAL);
    assert(second->return_expression->span.start == 30);
    assert(second->return_expression->span.end == 34);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_none_call_argument(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { f(none, true); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->expression != NULL);
    assert(statement->expression->kind == CC_AST_EXPRESSION_CALL);

    CcCallArgument *first = statement->expression->call.arguments;

    assert(first != NULL);
    assert(first->value->kind == CC_AST_EXPRESSION_NONE_LITERAL);
    assert(first->value->span.start == 11 && first->value->span.end == 15);

    CcCallArgument *second = first->next;

    assert(second != NULL);
    assert(second->value->kind == CC_AST_EXPRESSION_BOOLEAN_LITERAL);
    assert(second->value->boolean_literal);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_none_missing_semicolon(void)
{
    static const char *source = "fn f() { return none";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == strlen(source));
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_some_expression(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let x = some(1 + 2); return some(none); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_LET);

    CcExpression *some = first->let.initializer;

    assert(some != NULL);
    assert(some->kind == CC_AST_EXPRESSION_SOME);
    assert(some->span.file_id == fixture.id);
    assert(some->span.start == 17 && some->span.end == 28);
    assert(some->some_value != NULL);
    assert(some->some_value->kind == CC_AST_EXPRESSION_BINARY);
    assert(some->some_value->binary.operator == CC_AST_BINARY_OPERATOR_PLUS);
    assert(some->some_value->span.start == 22);
    assert(some->some_value->span.end == 27);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_RETURN);
    assert(second->next == NULL);

    some = second->return_expression;

    assert(some != NULL);
    assert(some->kind == CC_AST_EXPRESSION_SOME);
    assert(some->span.start == 37 && some->span.end == 47);
    assert(some->some_value != NULL);
    assert(some->some_value->kind == CC_AST_EXPRESSION_NONE_LITERAL);
    assert(some->some_value->span.start == 42);
    assert(some->some_value->span.end == 46);

    fixture_destroy(&fixture);
}

static void test_nested_some_argument(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { use(some(some(1)), none); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->expression->kind == CC_AST_EXPRESSION_CALL);

    CcCallArgument *first = statement->expression->call.arguments;

    assert(first != NULL);
    assert(first->value->kind == CC_AST_EXPRESSION_SOME);
    assert(first->value->span.start == 13);
    assert(first->value->span.end == 26);

    CcExpression *inner = first->value->some_value;

    assert(inner != NULL);
    assert(inner->kind == CC_AST_EXPRESSION_SOME);
    assert(inner->span.start == 18 && inner->span.end == 25);
    assert(inner->some_value != NULL);
    assert(inner->some_value->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(inner->some_value->integer_literal.value == 1);

    CcCallArgument *second = first->next;

    assert(second != NULL);
    assert(second->value->kind == CC_AST_EXPRESSION_NONE_LITERAL);
    assert(second->value->span.start == 28 && second->value->span.end == 32);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_some_missing_closing_parenthesis(void)
{
    static const char *source = "fn f() { return some(1 + 2";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(strcmp(fixture.parser.error, "expected `)` after `some` value") == 0);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == strlen(source));
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_integer_overflow_is_recorded(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f() { return 18446744073709551616; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->body != NULL);
    assert(function->body->first != NULL);
    assert(function->body->first->return_expression != NULL);
    assert(function->body->first->return_expression->integer_literal.overflow ==
           true);

    fixture_destroy(&fixture);
}

static void test_let_statement_with_type(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { let x: i32 = 1 + 2; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_LET);
    assert(statement->next == NULL);
    assert(statement->span.start == 9 && statement->span.end == 28);
    assert(statement->let.mutable == false);
    assert(statement->let.name.start == 13 && statement->let.name.end == 14);
    assert(statement->let.type != NULL);
    assert(statement->let.type->kind == CC_AST_TYPE_NAMED);
    assert(statement->let.type->span.start == 16);
    assert(statement->let.type->span.end == 19);
    assert(statement->let.initializer != NULL);
    assert(statement->let.initializer->kind == CC_AST_EXPRESSION_BINARY);
    assert(statement->let.initializer->span.start == 22);
    assert(statement->let.initializer->span.end == 27);

    fixture_destroy(&fixture);
}

static void test_var_statement_inferred(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { var y = 2; return y; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_LET);
    assert(first->span.start == 9 && first->span.end == 19);
    assert(first->let.mutable == true);
    assert(first->let.name.start == 13 && first->let.name.end == 14);
    assert(first->let.type == NULL);
    assert(first->let.initializer != NULL);
    assert(first->let.initializer->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(first->let.initializer->integer_literal.value == 2);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_RETURN);
    assert(second->span.start == 20 && second->span.end == 29);
    assert(second->return_expression != NULL);
    assert(second->return_expression->kind ==
           CC_AST_EXPRESSION_IDENTIFIER);
    assert(second->return_expression->span.start == 27);
    assert(second->return_expression->span.end == 28);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_expression_statements(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { 1 + 2; x; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(first->span.start == 9 && first->span.end == 15);
    assert(first->expression != NULL);
    assert(first->expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(first->expression->span.start == 9);
    assert(first->expression->span.end == 14);
    assert(first->next != NULL);

    CcStatement *second = first->next;

    assert(second->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(second->span.start == 16 && second->span.end == 18);
    assert(second->expression != NULL);
    assert(second->expression->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(second->expression->span.start == 16);
    assert(second->expression->span.end == 17);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_assignment_statement(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { x = y + 1; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_ASSIGNMENT);
    assert(statement->next == NULL);
    assert(statement->span.start == 9 && statement->span.end == 19);
    assert(statement->assignment.operator == CC_AST_ASSIGNMENT_ASSIGN);
    assert(statement->assignment.target != NULL);
    assert(statement->assignment.target->kind ==
           CC_AST_EXPRESSION_IDENTIFIER);
    assert(statement->assignment.target->span.start == 9);
    assert(statement->assignment.target->span.end == 10);
    assert(statement->assignment.value != NULL);
    assert(statement->assignment.value->kind == CC_AST_EXPRESSION_BINARY);
    assert(statement->assignment.value->span.start == 13);
    assert(statement->assignment.value->span.end == 18);

    fixture_destroy(&fixture);
}

static void test_compound_assignment_statements(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { x += 1; y >>= 2; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_ASSIGNMENT);
    assert(first->span.start == 9 && first->span.end == 16);
    assert(first->assignment.operator == CC_AST_ASSIGNMENT_PLUS);
    assert(first->assignment.target != NULL);
    assert(first->assignment.target->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(first->assignment.target->span.start == 9);
    assert(first->assignment.target->span.end == 10);
    assert(first->assignment.value != NULL);
    assert(first->assignment.value->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(first->assignment.value->integer_literal.value == 1);
    assert(first->next != NULL);

    CcStatement *second = first->next;

    assert(second->kind == CC_AST_STATEMENT_ASSIGNMENT);
    assert(second->span.start == 17 && second->span.end == 25);
    assert(second->assignment.operator == CC_AST_ASSIGNMENT_SHIFT_RIGHT);
    assert(second->assignment.target != NULL);
    assert(second->assignment.target->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(second->assignment.target->span.start == 17);
    assert(second->assignment.target->span.end == 18);
    assert(second->assignment.value != NULL);
    assert(second->assignment.value->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(second->assignment.value->integer_literal.value == 2);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_call_expression_with_arguments(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { g(1, 2 + 3); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 21);

    CcExpression *expression = statement->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_CALL);
    assert(expression->span.start == 9 && expression->span.end == 20);
    assert(expression->call.callee != NULL);
    assert(expression->call.callee->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(expression->call.callee->span.start == 9);
    assert(expression->call.callee->span.end == 10);

    CcCallArgument *first = expression->call.arguments;

    assert(first != NULL);
    assert(first->value != NULL);
    assert(first->value->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(first->value->span.start == 11);
    assert(first->value->span.end == 12);

    CcCallArgument *second = first->next;

    assert(second != NULL);
    assert(second->value != NULL);
    assert(second->value->kind == CC_AST_EXPRESSION_BINARY);
    assert(second->value->span.start == 14);
    assert(second->value->span.end == 19);
    assert(second->next == NULL);

    fixture_destroy(&fixture);
}

static void test_call_expression_without_arguments(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { g(); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 13);

    CcExpression *expression = statement->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_CALL);
    assert(expression->span.start == 9 && expression->span.end == 12);
    assert(expression->call.callee != NULL);
    assert(expression->call.arguments == NULL);

    fixture_destroy(&fixture);
}

static void test_nested_call_expression(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { g(h(1)); }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 17);

    CcExpression *expression = statement->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_CALL);
    assert(expression->span.start == 9 && expression->span.end == 16);
    assert(expression->call.callee != NULL);
    assert(expression->call.callee->kind == CC_AST_EXPRESSION_IDENTIFIER);

    CcCallArgument *argument = expression->call.arguments;

    assert(argument != NULL);
    assert(argument->next == NULL);
    assert(argument->value != NULL);
    assert(argument->value->kind == CC_AST_EXPRESSION_CALL);
    assert(argument->value->span.start == 11);
    assert(argument->value->span.end == 15);

    fixture_destroy(&fixture);
}

static void test_member_access_chain(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { p.q.r; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 15);

    CcExpression *outer = statement->expression;

    assert(outer != NULL);
    assert(outer->kind == CC_AST_EXPRESSION_MEMBER);
    assert(outer->span.start == 9 && outer->span.end == 14);
    assert(outer->member.name.start == 13);
    assert(outer->member.name.end == 14);

    CcExpression *inner = outer->member.operand;

    assert(inner != NULL);
    assert(inner->kind == CC_AST_EXPRESSION_MEMBER);
    assert(inner->span.start == 9 && inner->span.end == 12);
    assert(inner->member.name.start == 11);
    assert(inner->member.name.end == 12);
    assert(inner->member.operand != NULL);
    assert(inner->member.operand->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(inner->member.operand->span.start == 9);
    assert(inner->member.operand->span.end == 10);

    fixture_destroy(&fixture);
}

static void test_member_call_member_chain(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { p.f(1).q; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 18);

    CcExpression *outer = statement->expression;

    assert(outer != NULL);
    assert(outer->kind == CC_AST_EXPRESSION_MEMBER);
    assert(outer->span.start == 9 && outer->span.end == 17);
    assert(outer->member.name.start == 16);
    assert(outer->member.name.end == 17);
    assert(outer->member.operand != NULL);

    CcExpression *call = outer->member.operand;

    assert(call->kind == CC_AST_EXPRESSION_CALL);
    assert(call->span.start == 9 && call->span.end == 15);
    assert(call->call.arguments != NULL);
    assert(call->call.arguments->next == NULL);
    assert(call->call.arguments->value != NULL);
    assert(call->call.arguments->value->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);

    CcExpression *callee = call->call.callee;

    assert(callee != NULL);
    assert(callee->kind == CC_AST_EXPRESSION_MEMBER);
    assert(callee->span.start == 9 && callee->span.end == 12);
    assert(callee->member.name.start == 11);
    assert(callee->member.name.end == 12);

    fixture_destroy(&fixture);
}

static void test_index_expression(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { a[0]; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 14);

    CcExpression *expression = statement->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_INDEX);
    assert(expression->span.start == 9 && expression->span.end == 13);
    assert(expression->index.operand != NULL);
    assert(expression->index.operand->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(expression->index.operand->span.start == 9);
    assert(expression->index.operand->span.end == 10);
    assert(expression->index.index != NULL);
    assert(expression->index.index->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(expression->index.index->integer_literal.value == 0);
    assert(expression->index.index->span.start == 11);
    assert(expression->index.index->span.end == 12);

    fixture_destroy(&fixture);
}

static void test_index_on_member_and_chained_index(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { p.q[1 + 1]; a[1][2]; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *first = function->body->first;

    assert(first != NULL);
    assert(first->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(first->span.start == 9 && first->span.end == 20);

    CcExpression *expression = first->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_INDEX);
    assert(expression->span.start == 9 && expression->span.end == 19);

    CcExpression *member = expression->index.operand;

    assert(member != NULL);
    assert(member->kind == CC_AST_EXPRESSION_MEMBER);
    assert(member->span.start == 9 && member->span.end == 12);
    assert(member->member.name.start == 11);
    assert(member->member.name.end == 12);

    CcExpression *index = expression->index.index;

    assert(index != NULL);
    assert(index->kind == CC_AST_EXPRESSION_BINARY);
    assert(index->span.start == 13 && index->span.end == 18);
    assert(index->binary.left != NULL);
    assert(index->binary.left->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(index->binary.left->span.start == 13);
    assert(index->binary.left->span.end == 14);
    assert(index->binary.right != NULL);
    assert(index->binary.right->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(index->binary.right->span.start == 17);
    assert(index->binary.right->span.end == 18);

    CcStatement *second = first->next;

    assert(second != NULL);
    assert(second->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(second->span.start == 21 && second->span.end == 29);

    CcExpression *outer = second->expression;

    assert(outer != NULL);
    assert(outer->kind == CC_AST_EXPRESSION_INDEX);
    assert(outer->span.start == 21 && outer->span.end == 28);
    assert(outer->index.index != NULL);
    assert(outer->index.index->kind == CC_AST_EXPRESSION_INTEGER_LITERAL);

    CcExpression *inner = outer->index.operand;

    assert(inner != NULL);
    assert(inner->kind == CC_AST_EXPRESSION_INDEX);
    assert(inner->span.start == 21 && inner->span.end == 25);
    assert(inner->index.operand != NULL);
    assert(inner->index.operand->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(inner->index.operand->span.start == 21);
    assert(inner->index.operand->span.end == 22);

    fixture_destroy(&fixture);
}

static void test_tuple_member_expressions(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { p.0.q; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 15);

    CcExpression *member = statement->expression;

    assert(member != NULL);
    assert(member->kind == CC_AST_EXPRESSION_MEMBER);
    assert(member->span.start == 9 && member->span.end == 14);
    assert(member->member.name.start == 13);
    assert(member->member.name.end == 14);

    CcExpression *tuple = member->member.operand;

    assert(tuple != NULL);
    assert(tuple->kind == CC_AST_EXPRESSION_TUPLE_MEMBER);
    assert(tuple->span.start == 9 && tuple->span.end == 12);
    assert(tuple->tuple_member.operand != NULL);
    assert(tuple->tuple_member.operand->kind ==
           CC_AST_EXPRESSION_IDENTIFIER);
    assert(tuple->tuple_member.operand->span.start == 9);
    assert(tuple->tuple_member.operand->span.end == 10);
    assert(tuple->tuple_member.index.start == 11);
    assert(tuple->tuple_member.index.end == 12);
    assert(tuple->tuple_member.index_value.value == 0);
    assert(tuple->tuple_member.index_value.overflow == false);

    fixture_destroy(&fixture);
}

static void test_if_statement(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { if x { g(); } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->body != NULL);
    assert(function->body->span.start == 7 && function->body->span.end == 24);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_IF);
    assert(statement->next == NULL);
    assert(statement->span.start == 9 && statement->span.end == 22);

    CcIf *if_statement = statement->if_statement;

    assert(if_statement != NULL);
    assert(if_statement->condition != NULL);
    assert(if_statement->condition->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(if_statement->condition->span.start == 12);
    assert(if_statement->condition->span.end == 13);
    assert(if_statement->then_block != NULL);
    assert(if_statement->then_block->span.start == 14);
    assert(if_statement->then_block->span.end == 22);
    assert(if_statement->then_block->first != NULL);
    assert(if_statement->then_block->first->kind ==
           CC_AST_STATEMENT_EXPRESSION);
    assert(if_statement->then_block->first->span.start == 16);
    assert(if_statement->then_block->first->span.end == 20);
    assert(if_statement->else_if == NULL);
    assert(if_statement->else_block == NULL);

    fixture_destroy(&fixture);
}

static void test_if_statement_with_else(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { if a { } else { } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_IF);
    assert(statement->span.start == 9 && statement->span.end == 26);

    CcIf *if_statement = statement->if_statement;

    assert(if_statement != NULL);
    assert(if_statement->condition != NULL);
    assert(if_statement->condition->span.start == 12);
    assert(if_statement->condition->span.end == 13);
    assert(if_statement->then_block != NULL);
    assert(if_statement->then_block->span.start == 14);
    assert(if_statement->then_block->span.end == 17);
    assert(if_statement->then_block->first == NULL);
    assert(if_statement->else_if == NULL);
    assert(if_statement->else_block != NULL);
    assert(if_statement->else_block->span.start == 23);
    assert(if_statement->else_block->span.end == 26);
    assert(if_statement->else_block->first == NULL);

    fixture_destroy(&fixture);
}

static void test_if_statement_else_if_chain(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f() { if a {} else if b { return 1; } else {} }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_IF);
    assert(statement->next == NULL);
    assert(statement->span.start == 9 && statement->span.end == 48);

    CcIf *outer = statement->if_statement;

    assert(outer != NULL);
    assert(outer->condition != NULL);
    assert(outer->condition->span.start == 12);
    assert(outer->condition->span.end == 13);
    assert(outer->then_block != NULL);
    assert(outer->then_block->span.start == 14);
    assert(outer->then_block->span.end == 16);
    assert(outer->then_block->first == NULL);
    assert(outer->else_block == NULL);
    assert(outer->else_if != NULL);

    CcIf *inner = outer->else_if;

    assert(inner->span.start == 22 && inner->span.end == 48);
    assert(inner->condition != NULL);
    assert(inner->condition->span.start == 25);
    assert(inner->condition->span.end == 26);
    assert(inner->then_block != NULL);
    assert(inner->then_block->span.start == 27);
    assert(inner->then_block->span.end == 40);
    assert(inner->then_block->first != NULL);
    assert(inner->then_block->first->kind == CC_AST_STATEMENT_RETURN);
    assert(inner->then_block->first->span.start == 29);
    assert(inner->then_block->first->span.end == 38);
    assert(inner->else_if == NULL);
    assert(inner->else_block != NULL);
    assert(inner->else_block->span.start == 46);
    assert(inner->else_block->span.end == 48);

    fixture_destroy(&fixture);
}

static void test_while_statement(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { while x { } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->body != NULL);
    assert(function->body->span.start == 7 && function->body->span.end == 22);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_WHILE);
    assert(statement->next == NULL);
    assert(statement->span.start == 9 && statement->span.end == 20);

    CcWhile *while_statement = statement->while_statement;

    assert(while_statement != NULL);
    assert(while_statement->condition != NULL);
    assert(while_statement->condition->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(while_statement->condition->span.start == 15);
    assert(while_statement->condition->span.end == 16);
    assert(while_statement->body != NULL);
    assert(while_statement->body->span.start == 17);
    assert(while_statement->body->span.end == 20);
    assert(while_statement->body->first == NULL);

    fixture_destroy(&fixture);
}

static void test_while_statement_with_body(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { while a { b; } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_WHILE);
    assert(statement->span.start == 9 && statement->span.end == 23);

    CcWhile *while_statement = statement->while_statement;

    assert(while_statement != NULL);
    assert(while_statement->condition != NULL);
    assert(while_statement->condition->span.start == 15);
    assert(while_statement->condition->span.end == 16);
    assert(while_statement->body != NULL);
    assert(while_statement->body->span.start == 17);
    assert(while_statement->body->span.end == 23);
    assert(while_statement->body->first != NULL);
    assert(while_statement->body->first->kind ==
           CC_AST_STATEMENT_EXPRESSION);
    assert(while_statement->body->first->span.start == 19);
    assert(while_statement->body->first->span.end == 21);

    fixture_destroy(&fixture);
}

static void test_break_and_continue_statements(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { while x { break; } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->body != NULL);
    assert(function->body->span.start == 7 && function->body->span.end == 29);

    CcStatement *while_statement = function->body->first;

    assert(while_statement != NULL);
    assert(while_statement->kind == CC_AST_STATEMENT_WHILE);
    assert(while_statement->span.start == 9 && while_statement->span.end == 27);

    CcStatement *break_statement = while_statement->while_statement->body->first;

    assert(break_statement != NULL);
    assert(break_statement->kind == CC_AST_STATEMENT_BREAK);
    assert(break_statement->next == NULL);
    assert(break_statement->span.start == 19 && break_statement->span.end == 25);

    fixture_destroy(&fixture);
}

static void test_continue_statement(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { while y { continue; } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *while_statement = function->body->first;

    assert(while_statement != NULL);
    assert(while_statement->kind == CC_AST_STATEMENT_WHILE);

    CcStatement *continue_statement =
        while_statement->while_statement->body->first;

    assert(continue_statement != NULL);
    assert(continue_statement->kind == CC_AST_STATEMENT_CONTINUE);
    assert(continue_statement->next == NULL);
    assert(continue_statement->span.start == 19);
    assert(continue_statement->span.end == 28);

    fixture_destroy(&fixture);
}

static void test_loop_statement(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { loop { } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);
    assert(function->body != NULL);
    assert(function->body->span.start == 7 && function->body->span.end == 19);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_LOOP);
    assert(statement->next == NULL);
    assert(statement->span.start == 9 && statement->span.end == 17);

    CcLoop *loop_statement = statement->loop_statement;

    assert(loop_statement != NULL);
    assert(loop_statement->body != NULL);
    assert(loop_statement->body->span.start == 14);
    assert(loop_statement->body->span.end == 17);
    assert(loop_statement->body->first == NULL);

    fixture_destroy(&fixture);
}

static void test_loop_statement_with_break(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { loop { break; } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_LOOP);
    assert(statement->span.start == 9 && statement->span.end == 24);

    CcLoop *loop_statement = statement->loop_statement;

    assert(loop_statement != NULL);
    assert(loop_statement->body != NULL);
    assert(loop_statement->body->span.start == 14);
    assert(loop_statement->body->span.end == 24);

    CcStatement *break_statement = loop_statement->body->first;

    assert(break_statement != NULL);
    assert(break_statement->kind == CC_AST_STATEMENT_BREAK);
    assert(break_statement->span.start == 16 && break_statement->span.end == 22);

    fixture_destroy(&fixture);
}

static void test_range_expression(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { 0..9; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 14);

    CcExpression *expression = statement->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->span.start == 9 && expression->span.end == 13);
    assert(expression->binary.operator == CC_AST_BINARY_OPERATOR_RANGE);
    assert(expression->binary.left != NULL);
    assert(expression->binary.left->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(expression->binary.left->span.start == 9);
    assert(expression->binary.left->span.end == 10);
    assert(expression->binary.right != NULL);
    assert(expression->binary.right->kind ==
           CC_AST_EXPRESSION_INTEGER_LITERAL);
    assert(expression->binary.right->span.start == 12);
    assert(expression->binary.right->span.end == 13);

    fixture_destroy(&fixture);
}

static void test_inclusive_range_binds_loosely(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { 1 + 2..=n; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_EXPRESSION);
    assert(statement->span.start == 9 && statement->span.end == 19);

    CcExpression *expression = statement->expression;

    assert(expression != NULL);
    assert(expression->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->span.start == 9 && expression->span.end == 18);
    assert(expression->binary.operator ==
           CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE);
    assert(expression->binary.left != NULL);
    assert(expression->binary.left->kind == CC_AST_EXPRESSION_BINARY);
    assert(expression->binary.left->span.start == 9);
    assert(expression->binary.left->span.end == 14);
    assert(expression->binary.right != NULL);
    assert(expression->binary.right->kind == CC_AST_EXPRESSION_IDENTIFIER);
    assert(expression->binary.right->span.start == 17);
    assert(expression->binary.right->span.end == 18);

    fixture_destroy(&fixture);
}

static void test_for_statement(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "fn f() { for i in 0..10 { continue; } }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_FOR);
    assert(statement->span.file_id == fixture.id);
    assert(statement->span.start == 9 && statement->span.end == 37);
    assert(statement->next == NULL);

    CcFor *for_statement = statement->for_statement;

    assert(for_statement != NULL);
    assert(for_statement->span.start == 9 && for_statement->span.end == 37);
    assert(for_statement->name.start == 13 && for_statement->name.end == 14);
    assert(for_statement->range != NULL);
    assert(for_statement->range->kind == CC_AST_EXPRESSION_BINARY);
    assert(for_statement->range->span.start == 18);
    assert(for_statement->range->span.end == 23);
    assert(for_statement->range->binary.operator == CC_AST_BINARY_OPERATOR_RANGE);
    assert(for_statement->range->binary.left->integer_literal.value == 0);
    assert(for_statement->range->binary.right->integer_literal.value == 10);
    assert(for_statement->body != NULL);
    assert(for_statement->body->span.start == 24);
    assert(for_statement->body->span.end == 37);
    assert(for_statement->body->first != NULL);
    assert(for_statement->body->first->kind == CC_AST_STATEMENT_CONTINUE);
    assert(for_statement->body->first->span.start == 26);
    assert(for_statement->body->first->span.end == 35);
    assert(for_statement->body->first->next == NULL);

    fixture_destroy(&fixture);
}

static void test_for_inclusive_range_precedence(void)
{
    ParserFixture fixture;

    fixture_init(&fixture,
                 "fn f() { for index in 1 + 2..=limit { break; } return 0; }");

    CcFunctionDeclaration *function = cc_parser_parse_program(&fixture.parser);

    assert(function != NULL);
    assert(!fixture.parser.failed);

    CcStatement *statement = function->body->first;

    assert(statement != NULL);
    assert(statement->kind == CC_AST_STATEMENT_FOR);
    assert(statement->span.start == 9 && statement->span.end == 46);

    CcFor *for_statement = statement->for_statement;

    assert(for_statement != NULL);
    assert(for_statement->name.start == 13 && for_statement->name.end == 18);
    assert(for_statement->range != NULL);
    assert(for_statement->range->span.start == 22);
    assert(for_statement->range->span.end == 35);
    assert(for_statement->range->binary.operator ==
           CC_AST_BINARY_OPERATOR_RANGE_INCLUSIVE);
    assert(for_statement->range->binary.left->kind == CC_AST_EXPRESSION_BINARY);
    assert(for_statement->range->binary.left->binary.operator ==
           CC_AST_BINARY_OPERATOR_PLUS);
    assert(for_statement->range->binary.right->kind ==
           CC_AST_EXPRESSION_IDENTIFIER);
    assert(for_statement->body != NULL);
    assert(for_statement->body->span.start == 36);
    assert(for_statement->body->span.end == 46);
    assert(for_statement->body->first->kind == CC_AST_STATEMENT_BREAK);

    CcStatement *next = statement->next;

    assert(next != NULL);
    assert(next->kind == CC_AST_STATEMENT_RETURN);
    assert(next->span.start == 47 && next->span.end == 56);
    assert(next->next == NULL);

    fixture_destroy(&fixture);
}

static void test_for_unterminated_body(void)
{
    static const char *source = "fn f() { for i in 0..10 {";
    ParserFixture fixture;

    fixture_init(&fixture, source);

    assert(cc_parser_parse_program(&fixture.parser) == NULL);
    assert(fixture.parser.failed);
    assert(fixture.parser.error != NULL);
    assert(strcmp(fixture.parser.error, "expected `}`") == 0);
    assert(fixture.parser.error_span.file_id == fixture.id);
    assert(fixture.parser.error_span.start == strlen(source));
    assert(fixture.parser.error_span.end == strlen(source));

    fixture_destroy(&fixture);
}

static void test_parse_errors(void)
{
    static const char *const invalid[] = {
        "main() {}",
        "fn () {}",
        "pub let x = 1;",
        "pub pub fn f() {}",
        "fn main( {}",
        "fn main() -> { return; }",
        "fn f(x: ?) {}",
        "fn f(x: ?" "?) {}",
        "fn f() -> ? {}",
        "fn f() { let x: ? = none; }",
        "fn f(x: *) {}",
        "fn f(x: ?*) {}",
        "fn f() -> * {}",
        "fn f() { let p: ** = none; }",
        "let x = 1;",
        "fn main() { return 42 }",
        "fn main() { return; ",
        "fn main() { blabla }",
        "fn 42() {}",
        "fn main() { return -; }",
        "fn main() { return a + ; }",
        "fn main() { return (1; }",
        "fn main() { return a + b c; }",
        "fn main() { return a b; }",
        "fn f() { return true false; }",
        "fn f() { return !; }",
        "fn f() { \"a\" \"b\"; }",
        "fn f() { return \"bad\\q\"; }",
        "fn f() { c\"a\" c\"b\"; }",
        "fn f() { return c\"bad\\q\"; }",
        "fn f() { return c \"ok\"; }",
        "fn f() { return ''; }",
        "fn f() { return 'AB'; }",
        "fn f() { return '\\q'; }",
        "fn f() { return '\\u{80}'; }",
        "fn f() { 'A' 'B'; }",
        "fn f() { none none; }",
        "fn f() { return none true; }",
        "fn f() { return none +; }",
        "fn f() { return some; }",
        "fn f() { return some(); }",
        "fn f() { return some(1, 2); }",
        "fn f() { return some(1,); }",
        "fn f() { return some 1; }",
        "fn f() { let x; }",
        "fn f() { let 3 = 1; }",
        "fn f() { let x = ; }",
        "fn f() { let x: i32 = 1",
        "fn f() { var x: = 1; }",
        "fn f() { let (x, y) = x; }",
        "fn f() { x = ; }",
        "fn f() { x = 1 }",
        "fn f() { = 1; }",
        "fn f() { x = y = 1; }",
        "fn f() { x += ; }",
        "fn f() { x += 1 }",
        "fn f() { g(1, ); }",
        "fn f() { g(1 }",
        "fn f() { g(; }",
        "fn f() { p.; }",
        "fn f() { a[]; }",
        "fn f() { a[1; }",
        "fn f() { p.-1; }",
        "fn f() { if }",
        "fn f() { if x }",
        "fn f() { if x { } else }",
        "fn f() { while }",
        "fn f() { while x }",
        "fn f() { while x { break } }",
        "fn f() { break 1; }",
        "fn f() { continue }",
        "fn f() { loop }",
        "fn f() { loop; }",
        "fn f() { for {} }",
        "fn f() { for i 0..10 {} }",
        "fn f() { for i in {} }",
        "fn f() { for i in 0..10 }",
        "fn f() { for i in 0.. {} }",
        "fn f() { 0..1..2; }",
        "fn f() { 1..; }"
    };

    ParserFixture fixture;

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        fixture_init(&fixture, invalid[i]);

        cc_parser_parse_program(&fixture.parser);

        assert(fixture.parser.failed);
        assert(fixture.parser.error != NULL);

        fixture_destroy(&fixture);
    }
}

static void test_empty_source_without_functions(void)
{
    ParserFixture fixture;

    fixture_init(&fixture, "   \n");

    CcFunctionDeclaration *functions = cc_parser_parse_program(&fixture.parser);

    assert(functions == NULL);
    assert(!fixture.parser.failed);

    fixture_destroy(&fixture);
}

int main(void)
{
    test_simple_function();
    test_empty_block();
    test_return_no_expression();
    test_parameters_and_identifier();
    test_optional_types();
    test_nested_optional_types();
    test_optional_type_eof();
    test_pointer_types();
    test_pointer_type_eof();
    test_multiple_functions();
    test_public_function();
    test_public_function_eof();
    test_integer_overflow_is_recorded();
    test_binary_precedence();
    test_parenthesized_expression();
    test_unary_operators();
    test_boolean_literals();
    test_boolean_operator_precedence();
    test_boolean_expression_eof();
    test_string_literal();
    test_string_call_arguments();
    test_unterminated_string_literal();
    test_c_string_literal();
    test_c_string_call_arguments();
    test_unterminated_c_string_literal();
    test_byte_literal();
    test_byte_call_arguments();
    test_unterminated_byte_literal();
    test_none_literal();
    test_none_call_argument();
    test_none_missing_semicolon();
    test_some_expression();
    test_nested_some_argument();
    test_some_missing_closing_parenthesis();
    test_let_statement_with_type();
    test_var_statement_inferred();
    test_expression_statements();
    test_assignment_statement();
    test_compound_assignment_statements();
    test_call_expression_with_arguments();
    test_call_expression_without_arguments();
    test_nested_call_expression();
    test_member_access_chain();
    test_member_call_member_chain();
    test_index_expression();
    test_index_on_member_and_chained_index();
    test_tuple_member_expressions();
    test_if_statement();
    test_if_statement_with_else();
    test_if_statement_else_if_chain();
    test_while_statement();
    test_while_statement_with_body();
    test_break_and_continue_statements();
    test_continue_statement();
    test_loop_statement();
    test_loop_statement_with_break();
    test_range_expression();
    test_inclusive_range_binds_loosely();
    test_for_statement();
    test_for_inclusive_range_precedence();
    test_for_unterminated_body();
    test_parse_errors();
    test_empty_source_without_functions();

    puts("parser: all tests passed");
    return 0;
}
