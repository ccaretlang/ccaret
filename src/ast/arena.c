#include "ast/arena.h"

#include <stdint.h>
#include <stdlib.h>

#define CC_ARENA_BLOCK_SIZE 4096

struct CcArenaChunk {
    CcArenaChunk *previous;
    size_t used;
    size_t capacity;
    max_align_t data[];
};

static size_t align_up(size_t value, size_t alignment)
{
    size_t remainder = value % alignment;

    if (remainder == 0) return value;

    return value + (alignment - remainder);
}

void cc_arena_init(CcArena *arena)
{
    if (arena != NULL) arena->head = NULL;
}

void cc_arena_destroy(CcArena *arena)
{
    if (arena == NULL) return;

    CcArenaChunk *chunk = arena->head;

    while (chunk != NULL) {
        CcArenaChunk *previous = chunk->previous;
        free(chunk);
        chunk = previous;
    }

    arena->head = NULL;
}

void *cc_arena_alloc(CcArena *arena, size_t size)
{
    if (arena == NULL) return NULL;

    const size_t alignment = _Alignof(max_align_t);

    if (size == 0) size = 1;

    CcArenaChunk *chunk = arena->head;
    size_t used = chunk == NULL ? 0 : align_up(chunk->used, alignment);

    if (chunk != NULL && used <= chunk->capacity &&
        size <= chunk->capacity - used) {
        void *result = (unsigned char *)chunk->data + used;

        chunk->used = used + size;
        return result;
    }

    size_t capacity = size > CC_ARENA_BLOCK_SIZE ? size : CC_ARENA_BLOCK_SIZE;

    if (capacity > SIZE_MAX - sizeof(CcArenaChunk)) return NULL;

    CcArenaChunk *created = malloc(sizeof(CcArenaChunk) + capacity);

    if (created == NULL) return NULL;

    created->previous = arena->head;
    created->used = size;
    created->capacity = capacity;
    arena->head = created;

    return created->data;
}
