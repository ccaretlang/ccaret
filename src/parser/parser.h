#ifndef CCARET_PARSER_PARSER_H
#define CCARET_PARSER_PARSER_H

#include "ast/arena.h"
#include "ast/ast.h"
#include "lexer/lexer.h"

typedef struct {
    CcLexer *lexer;
    CcArena *arena;
    CcToken current;
    bool failed;
    CcSourceSpan error_span;
    const char *error;
} CcParser;

void cc_parser_init(CcParser *parser, CcLexer *lexer, CcArena *arena);
CcFunctionDeclaration *cc_parser_parse_program(CcParser *parser);

#endif
