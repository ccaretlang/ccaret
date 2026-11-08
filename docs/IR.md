# Intermediate representation (draft 0.1)

## Why the IR comes first

The IR exists from the first working compiler. The AST must never emit final C code. The frontend describes source semantics and knows nothing about the backend.

## Implemented initial model

The first version now uses a typed, basic-block, non-SSA IR. Each function
has typed parameters, local slots, temporaries, basic blocks and explicit
terminators. The current lowering supports the scalar subset `i32`,
`bool`, and `void`; other source types are intentionally rejected at
IR lowering, even when the semantic analyzer accepts them. SSA can be
added later if measurements show it is worthwhile.

```text
function add(a: i32, b: i32) -> i32
block entry:
    %0 = add.checked.i32 a, b
    ret %0
```

Tuple aggregates and aggregate ABI lowering are planned, not implemented.

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

## Current verifier contract

The IR implementation is in `src/ir/`:

- `cc_ir_lower` turns a semantically checked AST into a module of typed
  functions, local slots, virtual registers and basic blocks.
- `cc_ir_verify` independently rejects invalid function IDs, slot and
  register declarations, use-before-definition, cross-block register uses,
  incorrect instruction/result types, bad calls and missing or invalid
  block terminators/targets.
- `cc_ir_print` produces a deterministic text form for inspection and
  testing; `ccaret emit-ir file.caret` prints it to stdout, with optional
  `-o path`.

Blocks are terminated by `ret`, `br`, `condbr`, or `trap`. A virtual
register may only be used in its defining block. A source value that
survives a change in control flow is explicitly stored in a slot and
reloaded in the destination block. Boolean `&&` and `||` lower into
conditional branches and short-circuit properly.

This first verifier checks types and local block structure but does not yet
prove memory safety, arbitrary dominance/liveness or target ABI layout.

## Backend contract

Every backend receives a documented IR form and must preserve observable C^ semantics. No backend may reinterpret errors, memory or integer overflow. Backend choice must not affect source-level correctness.

## Tests and open questions

Tests now cover `return 42`, calls, recursion, arithmetic, branches,
loops, short-circuit control flow, snapshots and intentionally corrupted
IR. Tuple aggregates, `match`, cleanup, aliasing, ABI lowering, and
full IR optimization remain future work.
