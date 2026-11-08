#include "ast/arena.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_empty_and_null(void)
{
    CcArena arena;

    cc_arena_init(&arena);

    assert(arena.head == NULL);

    cc_arena_destroy(&arena);
    assert(arena.head == NULL);

    cc_arena_init(NULL);
    cc_arena_destroy(NULL);
    assert(cc_arena_alloc(NULL, 16) == NULL);
}

static void test_allocations(void)
{
    CcArena arena;

    cc_arena_init(&arena);

    int *first = cc_arena_alloc(&arena, sizeof(*first));
    int *second = cc_arena_alloc(&arena, sizeof(*second));
    unsigned char *bytes = cc_arena_alloc(&arena, 32);
    void *zero = cc_arena_alloc(&arena, 0);

    assert(first != NULL);
    assert(second != NULL);
    assert(bytes != NULL);
    assert(zero != NULL);
    assert(first != second);
    assert((uintptr_t)first % _Alignof(max_align_t) == 0);
    assert((uintptr_t)second % _Alignof(max_align_t) == 0);
    assert((uintptr_t)bytes % _Alignof(max_align_t) == 0);

    *first = 10;
    *second = 20;

    for (size_t i = 0; i < 32; i++)
        bytes[i] = (unsigned char)i;

    assert(*first == 10);
    assert(*second == 20);

    for (size_t i = 0; i < 32; i++)
        assert(bytes[i] == (unsigned char)i);

    cc_arena_destroy(&arena);
}

static void test_growth(void)
{
    enum { COUNT = 1024 };

    CcArena arena;
    unsigned char *blocks[COUNT];

    cc_arena_init(&arena);

    for (size_t i = 0; i < COUNT; i++) {
        blocks[i] = cc_arena_alloc(&arena, 17);

        assert(blocks[i] != NULL);

        blocks[i][0] = (unsigned char)i;
        blocks[i][16] = (unsigned char)(i + 1);
    }

    for (size_t i = 0; i < COUNT; i++) {
        assert(blocks[i][0] == (unsigned char)i);
        assert(blocks[i][16] == (unsigned char)(i + 1));
    }

    cc_arena_destroy(&arena);
}

static void test_large_and_overflow(void)
{
    CcArena arena;
    const size_t large_size = 1024 * 1024;

    cc_arena_init(&arena);

    unsigned char *large = cc_arena_alloc(&arena, large_size);

    assert(large != NULL);

    large[0] = 1;
    large[large_size - 1] = 2;

    assert(large[0] == 1);
    assert(large[large_size - 1] == 2);

    assert(cc_arena_alloc(&arena, SIZE_MAX) == NULL);

    cc_arena_destroy(&arena);
}

int main(void)
{
    test_empty_and_null();
    test_allocations();
    test_growth();
    test_large_and_overflow();

    puts("arena: all tests passed");
    return 0;
}
