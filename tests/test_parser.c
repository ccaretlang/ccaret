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

static void test_parse_errors(void)
{
    static const char *const invalid[] = {
        "main() {}",
        "fn () {}",
        "fn main( {}",
        "fn main() -> { return; }",
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
        "fn f() { let x; }",
        "fn f() { let 3 = 1; }",
        "fn f() { let x = ; }",
        "fn f() { let x: i32 = 1",
        "fn f() { var x: = 1; }",
        "fn f() { let (x, y) = x; }"
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
    test_multiple_functions();
    test_integer_overflow_is_recorded();
    test_binary_precedence();
    test_parenthesized_expression();
    test_unary_operators();
    test_let_statement_with_type();
    test_var_statement_inferred();
    test_expression_statements();
    test_parse_errors();
    test_empty_source_without_functions();

    puts("parser: all tests passed");
    return 0;
}
