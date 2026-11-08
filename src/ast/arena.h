#ifndef CCARET_AST_ARENA_H
#define CCARET_AST_ARENA_H

#include <stddef.h>

typedef struct CcArenaChunk CcArenaChunk;

typedef struct {
    CcArenaChunk *head;
} CcArena;

void cc_arena_init(CcArena *arena);
void cc_arena_destroy(CcArena *arena);
void *cc_arena_alloc(CcArena *arena, size_t size);

#endif
