#ifndef CCARET_IR_IR_H
#define CCARET_IR_IR_H

#include "ast/ast.h"
#include "source/source.h"
#include "ast/arena.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define CC_IR_NONE UINT32_MAX
#define CC_IR_USIZE (UINT32_MAX - 1U)

typedef uint32_t CcIrType;

enum {
    CC_IR_VOID,
    CC_IR_I32,
    CC_IR_BOOL
};

typedef enum {
    CC_IR_TYPE_TUPLE,
    CC_IR_TYPE_OPTIONAL,
    CC_IR_TYPE_ARRAY,
    CC_IR_TYPE_STRUCT,
    CC_IR_TYPE_ENUM,
    CC_IR_TYPE_POINTER,
    CC_IR_TYPE_SLICE
} CcIrCompositeKind;

typedef struct CcIrTypeInfo CcIrTypeInfo;

struct CcIrTypeInfo {
    CcIrType id;
    CcIrCompositeKind kind;
    uint32_t count;
    CcIrType *members;
    CcIrType inner;
    bool pointer_const; /* Pointer target is read-only (not a const binding). */
    bool slice_const;   /* Slice element access is read-only. */
    CcSourceSpan name;          /* Nominal struct/enum name, otherwise unused. */
    CcSourceSpan *field_names;  /* Struct fields or enum variants, in order. */
    CcIrTypeInfo *next;
};

typedef enum {
    CC_IR_CONST_I32,
    CC_IR_CONST_BOOL,
    CC_IR_CONST_USIZE,
    CC_IR_LOAD,
    CC_IR_STORE,
    CC_IR_NEG,
    CC_IR_NOT,
    CC_IR_BIT_NOT,
    CC_IR_ADD,
    CC_IR_SUB,
    CC_IR_MUL,
    CC_IR_DIV,
    CC_IR_MOD,
    CC_IR_WRAP_ADD,
    CC_IR_WRAP_SUB,
    CC_IR_WRAP_MUL,
    CC_IR_BIT_AND,
    CC_IR_BIT_OR,
    CC_IR_BIT_XOR,
    CC_IR_EQ,
    CC_IR_NE,
    CC_IR_LT,
    CC_IR_LE,
    CC_IR_GT,
    CC_IR_GE,
    CC_IR_CALL,
    CC_IR_MAKE_TUPLE,
    CC_IR_GET_TUPLE,
    CC_IR_MAKE_SOME,
    CC_IR_MAKE_NONE,
    CC_IR_OPTIONAL_HAS,
    CC_IR_OPTIONAL_UNWRAP,
    CC_IR_MAKE_ENUM,
    CC_IR_ENUM_IS,
    CC_IR_ENUM_GET,
    CC_IR_MAKE_ARRAY,
    CC_IR_GET_ARRAY,
    CC_IR_STORE_INDEX,
    CC_IR_MAKE_STRUCT,
    CC_IR_GET_STRUCT,
    CC_IR_SET_FIELD,
    CC_IR_ADDR_SLOT,
    CC_IR_DEREF_PTR,
    CC_IR_STORE_PTR,
    CC_IR_MAKE_SLICE,
    CC_IR_GET_SLICE,
    CC_IR_STORE_SLICE,
    CC_IR_SLICE_LEN,
    CC_IR_SUBSLICE
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
    uint64_t wide_constant;
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
    bool mutable; /* Declared local mutability, false for temps/parameters. */
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
    CcIrTypeInfo *types;
    CcIrTypeInfo *last_type;
    uint32_t type_count;
};

/* Built-in IDs 0..2 are implicit. All composite IDs are module-local. */
const CcIrTypeInfo *cc_ir_type_info(const CcIrModule *module, CcIrType type);

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
