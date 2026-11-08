CC           := gcc
CFLAGS       := -std=c17 -O0 -g3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS     := -Isrc

TARGET 	     := build/ccaret
TEST_TARGET  := build/test_source
LEXER_TEST_TARGET  := build/test_lexer
ARENA_TEST_TARGET  := build/test_arena
PARSER_TEST_TARGET := build/test_parser
TUPLE_PARSER_TEST_TARGET := build/test_parser_tuples
SOURCES      := src/main.c src/source/source.c src/lexer/lexer.c
TEST_SOURCES := tests/test_source.c src/source/source.c
LEXER_TEST_SOURCES := tests/test_lexer.c src/lexer/lexer.c src/source/source.c
ARENA_TEST_SOURCES := tests/test_arena.c src/ast/arena.c
PARSER_TEST_SOURCES := tests/test_parser.c src/parser/parser.c src/lexer/lexer.c \
	src/ast/arena.c src/source/source.c
TUPLE_PARSER_TEST_SOURCES := tests/test_parser_tuples.c src/parser/parser.c \
	src/lexer/lexer.c src/ast/arena.c src/source/source.c
CLI_TEST     := tests/test_cli.sh


.PHONY: all check test clean

all: $(TARGET)

$(TARGET): $(SOURCES) src/source/source.h src/lexer/lexer.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $(TARGET)

$(TEST_TARGET): $(TEST_SOURCES) src/source/source.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $(TEST_TARGET)

$(LEXER_TEST_TARGET): $(LEXER_TEST_SOURCES) src/source/source.h src/lexer/lexer.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LEXER_TEST_SOURCES) -o $(LEXER_TEST_TARGET)

$(ARENA_TEST_TARGET): $(ARENA_TEST_SOURCES) src/ast/arena.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARENA_TEST_SOURCES) -o $(ARENA_TEST_TARGET)

$(PARSER_TEST_TARGET): $(PARSER_TEST_SOURCES) src/source/source.h src/lexer/lexer.h \
	src/ast/arena.h src/ast/ast.h src/parser/parser.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PARSER_TEST_SOURCES) -o $(PARSER_TEST_TARGET)

$(TUPLE_PARSER_TEST_TARGET): $(TUPLE_PARSER_TEST_SOURCES) src/source/source.h \
	src/lexer/lexer.h src/ast/arena.h src/ast/ast.h src/parser/parser.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TUPLE_PARSER_TEST_SOURCES) -o $(TUPLE_PARSER_TEST_TARGET)

test: $(TEST_TARGET) $(LEXER_TEST_TARGET) $(ARENA_TEST_TARGET) \
	$(PARSER_TEST_TARGET) $(TUPLE_PARSER_TEST_TARGET)
	./$(TEST_TARGET)
	./$(LEXER_TEST_TARGET)
	./$(ARENA_TEST_TARGET)
	./$(PARSER_TEST_TARGET)
	./$(TUPLE_PARSER_TEST_TARGET)

check: $(TARGET) $(TEST_TARGET) $(LEXER_TEST_TARGET) $(ARENA_TEST_TARGET) \
	$(PARSER_TEST_TARGET) $(TUPLE_PARSER_TEST_TARGET)
	./$(TARGET) --version
	./$(TEST_TARGET)
	./$(LEXER_TEST_TARGET)
	./$(ARENA_TEST_TARGET)
	./$(PARSER_TEST_TARGET)
	./$(TUPLE_PARSER_TEST_TARGET)
	sh $(CLI_TEST)

clean:
	rm -rf build
