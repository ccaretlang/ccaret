
#ifndef CCARET_LEXER_H
#define CCARET_LEXER_H

#include "source/source.h"

typedef enum {
    CC_TOKEN_INVALID = 0,
    CC_TOKEN_EOF,

    /* Identifiers and literals. */
    CC_TOKEN_IDENTIFIER,
    CC_TOKEN_INTEGER_LITERAL,
    CC_TOKEN_FLOAT_LITERAL,
    CC_TOKEN_STRING_LITERAL,
    CC_TOKEN_C_STRING_LITERAL,
    CC_TOKEN_BYTE_LITERAL,

    /* Keywords. */
    CC_TOKEN_KW_FN,
    CC_TOKEN_KW_RETURN,
    CC_TOKEN_KW_LET,
    CC_TOKEN_KW_VAR,
    CC_TOKEN_KW_CONST,
    CC_TOKEN_KW_STATIC,
    CC_TOKEN_KW_PUB,
    CC_TOKEN_KW_TYPE,
    CC_TOKEN_KW_STRUCT,
    CC_TOKEN_KW_UNION,
    CC_TOKEN_KW_ENUM,
    CC_TOKEN_KW_IF,
    CC_TOKEN_KW_ELSE,
    CC_TOKEN_KW_MATCH,
    CC_TOKEN_KW_WHILE,
    CC_TOKEN_KW_FOR,
    CC_TOKEN_KW_IN,
    CC_TOKEN_KW_LOOP,
    CC_TOKEN_KW_BREAK,
    CC_TOKEN_KW_CONTINUE,
    CC_TOKEN_KW_MODULE,
    CC_TOKEN_KW_IMPORT,
    CC_TOKEN_KW_AS,
    CC_TOKEN_KW_EXTERN,
    CC_TOKEN_KW_UNSAFE,
    CC_TOKEN_KW_VOLATILE,
    CC_TOKEN_KW_DEFER,
    CC_TOKEN_KW_ERRDEFER,
    CC_TOKEN_KW_TRY,
    CC_TOKEN_KW_TRUE,
    CC_TOKEN_KW_FALSE,
    CC_TOKEN_KW_SOME,
    CC_TOKEN_KW_NONE,

    /* Arithmetic operators. */
    CC_TOKEN_PLUS,           /* +  */
    CC_TOKEN_MINUS,          /* -  */
    CC_TOKEN_STAR,           /* *  */
    CC_TOKEN_SLASH,          /* /  */
    CC_TOKEN_PERCENT,        /* %  */
    CC_TOKEN_PLUS_PERCENT,   /* +% */
    CC_TOKEN_MINUS_PERCENT,  /* -% */
    CC_TOKEN_STAR_PERCENT,   /* *% */

    /* Comparison operators. */
    CC_TOKEN_EQ,             /* == */
    CC_TOKEN_NE,             /* != */
    CC_TOKEN_LT,             /* <  */
    CC_TOKEN_LE,             /* <= */
    CC_TOKEN_GT,             /* >  */
    CC_TOKEN_GE,             /* >= */

    /* Boolean operators. */
    CC_TOKEN_AND_AND,        /* && */
    CC_TOKEN_OR_OR,          /* || */
    CC_TOKEN_NOT,            /* !  */

    /* Bitwise operators. */
    CC_TOKEN_AMP,            /* &  */
    CC_TOKEN_PIPE,           /* |  */
    CC_TOKEN_CARET,          /* ^  */
    CC_TOKEN_TILDE,          /* ~  */
    CC_TOKEN_SHIFT_LEFT,     /* << */
    CC_TOKEN_SHIFT_RIGHT,    /* >> */

    /* Assignment operators. */
    CC_TOKEN_ASSIGN,             /* =   */
    CC_TOKEN_PLUS_ASSIGN,        /* +=  */
    CC_TOKEN_MINUS_ASSIGN,       /* -=  */
    CC_TOKEN_STAR_ASSIGN,        /* *=  */
    CC_TOKEN_SLASH_ASSIGN,       /* /=  */
    CC_TOKEN_PERCENT_ASSIGN,     /* %=  */
    CC_TOKEN_AMP_ASSIGN,         /* &=  */
    CC_TOKEN_PIPE_ASSIGN,        /* |=  */
    CC_TOKEN_CARET_ASSIGN,       /* ^=  */
    CC_TOKEN_SHIFT_LEFT_ASSIGN,  /* <<= */
    CC_TOKEN_SHIFT_RIGHT_ASSIGN, /* >>= */

    /* Punctuation. */
    CC_TOKEN_LPAREN,         /* (   */
    CC_TOKEN_RPAREN,         /* )   */
    CC_TOKEN_LBRACE,         /* {   */
    CC_TOKEN_RBRACE,         /* }   */
    CC_TOKEN_LBRACKET,       /* [   */
    CC_TOKEN_RBRACKET,       /* ]   */
    CC_TOKEN_COMMA,          /* ,   */
    CC_TOKEN_SEMICOLON,      /* ;   */
    CC_TOKEN_COLON,          /* :   */
    CC_TOKEN_DOT,            /* .   */
    CC_TOKEN_ARROW,          /* ->  */
    CC_TOKEN_FAT_ARROW,      /* =>  */
    CC_TOKEN_QUESTION,       /* ?   */
    CC_TOKEN_DOT_DOT,        /* ..  */
    CC_TOKEN_DOT_DOT_EQUAL,  /* ..= */
    CC_TOKEN_ELLIPSIS,       /* ... */
    CC_TOKEN_AT              /* @   */
} CcTokenKind;

typedef struct {
    CcTokenKind kind;
    CcSourceSpan span;
} CcToken;

typedef struct {
    const CcSourceFile *file;
    CcFileId file_id;
    size_t offset;
} CcLexer;

void cc_lexer_init(CcLexer *lexer, const CcSourceFile *file, CcFileId file_id);
CcToken cc_lexer_next(CcLexer *lexer);
const char *cc_token_kind_name(CcTokenKind kind);

#endif
