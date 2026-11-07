CC           := gcc
CFLAGS       := -std=c17 -O0 -g3 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS     := -Isrc

TARGET 	     := build/ccaret
TEST_TARGET  := build/test_source
SOURCES      := src/main.c src/source/source.c
TEST_SOURCES := tests/test_source.c src/source/source.c


.PHONY: all check test clean

all: $(TARGET)

$(TARGET): $(SOURCES) src/source/source.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $(TARGET)

$(TEST_TARGET): $(TEST_SOURCES) src/source/source.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) -o $(TEST_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

check: $(TARGET) $(TEST_TARGET)
	./$(TARGET) --version
	./$(TEST_TARGET)

clean:
	rm -rf build
