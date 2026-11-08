#ifndef CCARET_IR_IR_H
#define CCARET_IR_IR_H

#include "ast/ast.h"
#include "source/source.h"
#include "ast/arena.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CC_IR_NONE UINT32_MAX

typedef enum {
    CC_IR_VOID,
    CC_IR_I32,
    CC_IR_BOOL
} CcIrType;

typedef enum {
    CC_IR_CONST_I32,
    CC_IR_CONST_BOOL,
    CC_IR_LOAD,
    CC_IR_STORE,
    CC_IR_NEG,
    CC_IR_NOT,
    CC_IR_ADD,
    CC_IR_SUB,
    CC_IR_MUL,
    CC_IR_DIV,
    CC_IR_MOD,
    CC_IR_EQ,
    CC_IR_NE,
    CC_IR_LT,
    CC_IR_LE,
    CC_IR_GT,
    CC_IR_GE,
    CC_IR_CALL
} CcIrOpcode;

typedef enum {
    CC_IR_RET,
    CC_IR_JUMP,
    CC_IR_BRANCH,
    CC_IR_TRAP
} CcIrTermKind;

typedef struct CcIrInstruction CcIrInstruction;
typedef struct CcIrBlock CcIrBlock;
typedef struct CcIrSlot CcIrSlot;
typedef struct CcIrRegister CcIrRegister;
typedef struct CcIrFunction CcIrFunction;
typedef struct CcIrModule CcIrModule;

struct CcIrInstruction {
    CcIrOpcode op;
    CcSourceSpan span;
    uint32_t result;
    CcIrType type;
    uint32_t lhs;
    uint32_t rhs;
    uint32_t slot;
    int32_t constant;
    uint32_t callee;
    uint32_t arg_count;
    uint32_t *args;
    CcIrInstruction *next;
};

struct CcIrBlock {
    uint32_t id;
    CcIrInstruction *first;
    CcIrInstruction *last;
    bool terminated;
    CcIrTermKind terminator;
    uint32_t target;
    uint32_t alternate;
    uint32_t condition;
    uint32_t return_value;
    CcIrBlock *next;
};

struct CcIrSlot {
    uint32_t id;
    CcIrType type;
    CcIrSlot *next;
};

struct CcIrRegister {
    uint32_t id;
    CcIrType type;
    uint32_t block;
    CcIrRegister *next;
};

struct CcIrFunction {
    uint32_t id;
    CcSourceSpan name;
    CcSourceSpan span;
    CcIrType result;
    uint32_t param_count;
    uint32_t slot_count;
    uint32_t register_count;
    uint32_t block_count;
    CcIrType *param_types;
    CcIrSlot *slots;
    CcIrSlot *last_slot;
    CcIrRegister *registers;
    CcIrRegister *last_register;
    CcIrBlock *blocks;
    CcIrBlock *last_block;
    CcIrFunction *next;
};

struct CcIrModule {
    const CcSourceFile *file;
    CcIrFunction *functions;
    uint32_t function_count;
};

typedef struct {
    CcSourceSpan span;
    const char *message;
} CcIrError;

/* The caller owns arena, file and AST; the resulting module lives in arena. */
bool cc_ir_lower(const CcSourceFile *file,
                 const CcFunctionDeclaration *program,
                 CcArena *arena, CcIrModule **out, CcIrError *error);

/* Verify even hand-constructed IR; the C backend must not accept unchecked IR. */
bool cc_ir_verify(const CcIrModule *module, CcIrError *error);

/* Deterministic human-readable representation, for tests and debugging. */
bool cc_ir_print(FILE *output, const CcIrModule *module);

#endif
