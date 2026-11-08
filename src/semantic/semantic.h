#ifndef CCARET_SEMANTIC_SEMANTIC_H
#define CCARET_SEMANTIC_SEMANTIC_H

#include "ast/arena.h"
#include "ast/ast.h"
#include "source/source.h"

#include <stdbool.h>

typedef struct {
    CcSourceSpan span;
    const char *message;
} CcSemanticError;

/*
 * Checks the parsed functions without modifying the AST.
 * The caller owns file and functions. Temporary data lives in arena.
 * On failure, error contains a stable message and a source span.
 */
bool cc_semantic_check(const CcSourceFile *file,
                       const CcFunctionDeclaration *functions,
                       CcArena *arena, CcSemanticError *error);

#endif
