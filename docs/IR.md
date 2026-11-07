# Intermediate representation (draft 0.1)

## Why the IR comes first

The IR exists from the first working compiler. The AST must never emit final C code. The frontend describes source semantics and knows nothing about the backend.

## Initial model

The first version uses a typed, basic-block, non-SSA IR. Each function has typed parameters, local slots, temporaries, basic blocks and explicit terminators. SSA can be added later if measurements show it is worth the work.

```text
function add(a: i32, b: i32) -> i32
block entry:
    %0 = add.checked.i32 a, b
    ret %0
```

A function that returns a tuple can build an aggregate in the IR. The C backend lowers it to a generated struct. A later backend can choose a calling convention that fits the target.

## Instruction families

| Category | Operations |
|---|---|
| Values | `const`, `copy`, `cast` |
| Arithmetic | `add`, `sub`, `mul`, `div`, `mod`, `shift`, with explicit overflow and checked flags |
| Logic | `cmp`, bitwise operations |
| Memory | `alloca`, `load`, `store`, `addr_of`, offset and index calculations |
| Aggregates | `make_aggregate`, `extract_field`, `insert_field` |
| Tagged values | `make_variant`, `get_tag`, `extract_payload` |
| Calls | `call`, `call_indirect` |
| Control | `br`, `condbr`, `ret`, `trap` |

`match` becomes tests and branches. `try` becomes success and error branches. `defer` becomes explicit cleanup blocks and edges. Boolean short-circuiting has to appear in the control flow. Trap behavior must not turn into C undefined behavior.

## Passes

1. Parse into an AST with source spans.
2. Resolve names, infer and check types, assign `SymbolId` and `TypeId`.
3. Lower high-level constructs into typed IR with explicit evaluation order.
4. Verify operand types, block terminators, branch targets and local slot liveness.
5. Lower aggregates and ABI details as needed by the backend.
6. Emit C17, invoke GCC and report diagnostics.

## Backend contract

Every backend receives a documented IR form and must preserve observable C^ semantics. No backend may reinterpret errors, memory or integer overflow. Backend choice must not affect source-level correctness.

## Tests and open questions

Snapshot and verify the IR for `return 42`, function calls, arithmetic, tuples, conditions, `match` and cleanup. Decide whether a separate high-level typed IR is needed. Define non-SSA merge values, optimization policy, pointer aliasing and provenance, and where ABI lowering belongs.
