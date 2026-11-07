CC      := gcc
CFLAGS  := -std=c17 -O0 -g3 -Wall -Wextra -Wpedantic -Werror

TARGET  := build/ccaret
SOURCES := src/main.c

.PHONY: $(TARGET)

all: $(TARGET)

$(TARGET): $(SOURCES)
	mkdir -p build
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

check: $(TARGET)
	./$(TARGET) --version

clean:
	rm -rf build
