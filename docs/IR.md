# Intermediate representation (draft 0.1)

## Why the IR comes first

The IR exists from the first working compiler. The AST must never emit final C code. The frontend describes source semantics and knows nothing about the backend.

## Implemented IR model

The first version now uses a typed, basic-block, non-SSA IR. Each function
has typed parameters, local slots, temporaries, basic blocks and explicit
terminators. Lowering supports `i32`, `bool`, `void`, a restricted `usize`,
structural tuples (including zero- and one-element tuples), optional `?T`
values, fixed arrays `[N]T` and nominal `struct` values, including
nested aggregate types.
Other source types still fail explicitly at IR
lowering. SSA can be added later if measurements show it is worthwhile.

```text
function add(a: i32, b: i32) -> i32
block entry:
    %0 = add.checked.i32 a, b
    ret %0
```

Composite types have canonical, module-local IDs starting at `3` (IDs
`0`–`2` designate `void`, `i32`, `bool`; `usize` has a distinct
reserved builtin ID). Identical structural types
are interned once, but distinct nominal structs retain distinct IDs even
when their fields have identical names and types. Tuple members, optional
payloads, array elements and struct field types are
defined before the enclosing type, and the verifier rejects invalid,
forward, self-referential or duplicate composite definitions. `emit-ir`
prints type definitions such as `type t3 = (i32, bool)`,
`type t4 = ?t3` and `type t5 = struct Point { x: i32, y: i32 }`.

For source-level `module`/`import` graphs, the bootstrap linker
resolves public symbols and namespaces **before** lowering, producing
one canonical source unit that still undergoes semantic analysis and
independent IR verification. Imported function/struct names are
internalized with collision-free names; the entry module's `main`
remains the executable entry point. Type and function IDs belong to the
linked compilation unit. An independent multi-unit IR linker is not
implemented yet.

## Implemented instruction families

| Category | Operations |
|---|---|
| Values | `const.i32`, `const.bool`, `const.usize` |
| Arithmetic | Checked `add`, `sub`, `mul`, `div`, `mod`, unary `neg`; `add.wrap`, `sub.wrap`, `mul.wrap` for `i32` |
| Logic | `not`, `bit.not`, `bit.and`, `bit.or`, `bit.xor`, equality and ordered comparisons |
| Locals | `load`, `store` with typed slots |
| Aggregates | `tuple`/`tuple.get`, `array` (construct), `array.get` (checked index), `array.store` (checked mutable local write); `struct`/`struct.get`/`struct.set` (named field values) |
| Tagged values | `some`, `none` (construct optional values) |
| Calls | Direct `call`, including aggregate parameters and results |
| Control | `br`, `condbr`, `ret`, `trap` |

Tuple destructuring lowers to a sequence of typed projections and slot
bindings. Tuple elements and call arguments are evaluated left-to-right
and temporarily stored in slots before aggregate construction or calls.
This also works when a child expression emits new control-flow blocks.
Compound assignments preserve the old target value in a slot across
evaluation of the right-hand side, then perform a checked arithmetic or
bitwise operation and write the result once.

`for` ranges are lowered from syntax to CFG blocks, not represented as
first-class range values in the backend. Start and end expressions are
evaluated left-to-right exactly once into typed `i32` slots. The loop
condition is `index < end` or `index <= end`; the `continue` edge
passes through an increment block. Inclusive loops test `index == end`
before incrementing so the maximum signed endpoint never overflows.
The immutable index binding has loop-local scope.
Array elements and indices evaluate left-to-right. Array constructors
collect typed element registers after staging child expressions in local
slots, preserving order across short-circuit control flow. Array reads
carry an expected length, source register and `i32` index; local indexed
writes carry a slot, `i32` index and value. The verifier checks the
array type, arity, bounds metadata and register types before C17 output.
`.len` evaluates its base expression and yields a `const.usize`
of its static length. This milestone supports `usize` values and
equality but not general `usize` arithmetic.
The nominal `struct` constructor has one typed operand per declared
field, in declaration order. Source field initializers are evaluated
left-to-right into temporary slots before construction, even if the
source lists fields in a different order. `struct.get` reads a field
from a typed aggregate register; `struct.set` updates a named field
of a typed local struct slot, with `i32` compound updates lowered
through checked arithmetic. The verifier independently validates
nominal names, field names, member type dependencies, operand types
and field indices. Recursive by-value struct graphs are rejected.
Optional unwrapping, `match`, `try`, `defer`, and their control-flow
lowerings remain future work. Boolean short-circuiting appears explicitly
in the CFG. Traps must not become C undefined behavior.

## Passes

1. Parse into an AST with source spans.
2. Resolve names, infer and check types, assign `SymbolId` and `TypeId`.
3. Lower high-level constructs into typed IR with explicit evaluation order.
4. Verify operand types, block terminators, branch targets and local slot liveness.
5. Map verified IR aggregates to backend-local C17 representations.
6. Emit C17, invoke GCC and report diagnostics.

## Current verifier contract

The IR implementation is in `src/ir/`:

- `cc_ir_lower` turns a semantically checked AST into a module of typed
  functions, local slots, virtual registers and basic blocks.
- `cc_ir_verify` independently rejects invalid function IDs, slot and
  register declarations, use-before-definition, cross-block register uses,
  incorrect instruction/result types, bad calls, malformed aggregate
  constructors/projections, noncanonical type tables and missing or invalid
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

Tests cover `return 42`, calls, recursion, arithmetic, branches, loops,
short-circuit control flow, exclusive/inclusive range loops and edge
values, compound assignment, bitwise/wrapping arithmetic,
fixed-array constructors, nominal struct constructors/copies, fields,
forward-declared dependencies, nested values, checked reads/writes and
trap cases,
tuple/optional propagation, the executable
`divmod` tuple-return example, and intentionally corrupted IR. A stable
external ABI for aggregates, `match`, cleanup, aliasing and full IR
optimization remain future work.

Module tests also cover cross-source public calls/struct values, aliases,
dependency cycles, privacy checks, originating-file diagnostics and
optimized generated C17 builds.
