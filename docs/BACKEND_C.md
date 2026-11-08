# C17 backend and executable pipeline

The C^ bootstrap compiler can produce executables for a **restricted typed
subset**, including structural tuples, fixed arrays, nominal structs
and optional values. The C backend
consumes only independently verified typed IR, never the AST.

## Commands

```sh
make check
./build/ccaret check tests/fixtures/build_hello.caret
./build/ccaret emit-ir tests/fixtures/build_hello.caret
./build/ccaret emit-c tests/fixtures/build_hello.caret -o build/hello.c
./build/ccaret build tests/fixtures/build_hello.caret -o build/hello
./build/hello
```

The last command exits with status 42. `build` retains the generated C
in `<output>.c`, which can be inspected or compiled manually. GCC is
invoked with an argument vector (no shell command interpolation). A valid
entry point is `fn main() -> i32`.

## Supported today

- Scalar types `i32`, `bool`, `void` (including grouping parentheses
  around a type), literal integers, boolean literals and unary `-`/`!`/`~`.
- Checked `i32` `+`, `-`, `*`, `/` and `%`; equality and numeric
  comparisons; boolean `&&`/`||` with short-circuit evaluation. Integer
  bitwise `&`, `|`, `^` and explicit wrapping `+%`, `-%`, `*%`
  are supported for `i32`.
- Functions with typed parameters, forward and recursive calls, explicit
  returns, local `let`/`var`, assignments `=`, `+=`, `-=`, `*=`,
  `/=`, `%=`, `&=`, `|=`, `^=` (compound assignment only on `i32`),
  `if`/`else if`/`else`, `while`, `loop`, `break`, `continue`.
- `for i in start..end` (exclusive) and `for i in start..=end`
  (inclusive) for `i32` endpoints. Both bounds are evaluated exactly
  once and in source order, and the index is immutable and scoped to
  the loop body. `continue` performs the increment; `break` exits the
  loop. Inclusive iteration at `INT32_MAX` stops without overflow.
- Tuple literals and types (including `()` and `(T,)`), nested tuple
  members `value.0`/`value.1`, tuple parameters and returns,
  destructuring in `let`/`var`, and forwarding aggregate values through
  calls and variables.
- Optional `?T` types, `some(value)` and `none` construction, optional
  parameters/returns, nested optionals and optionals inside tuples (and
  tuples inside optionals). `none` requires a contextual optional type.
- Fixed arrays `[N]T` with `[x, y]` literals (including `[]` with a
  type annotation), nested element types, by-value copies, parameters,
  returns, indexed reads, single-level indexed writes to `var` locals,
  and `arr.len`. The `usize` type supports array lengths, constants,
  parameters/returns and equality (not general integer operations).
  Array indices in this backend must currently be `i32`.
  Negative or out-of-range indices call `abort()` instead of accessing
  invalid memory. Indexed `i32` compound assignments use checked
  arithmetic; values and indices are evaluated once.
- Nominal `struct Name { x: T, y: U }` declarations, forward references
  to other nonrecursive structs, literal construction with named fields
  in any source order, field reads, and updates to direct fields of
  mutable local structs. `i32` compound field assignments use checked
  arithmetic. Struct parameters and results, empty structs, value copies
  and nesting with tuples/optionals/arrays are supported. Different
  named structs are never structurally interchangeable.
- Arguments are lowered left-to-right before the C function call, so the
  generated C's unspecified argument evaluation order has no effect.
  Tuple elements are similarly sequenced before construction.

Composite IR type IDs map to deterministic C17 `typedef struct` types
(`cc_t_3`, `cc_t_4`, etc.) in dependency order. Tuple elements use
`f0`, `f1`, ... fields; a zero-element tuple has a dummy byte because
empty structs are not standard C17. Optionals use a `bool has` tag and a
typed `value` payload. Arrays use a fixed `elements[N]` field, or a
dummy byte for `N = 0`, because zero-length C arrays are not standard
C17. Array lengths are limited to 65536 in this bootstrap backend.
Nominal structs use one C17 typedef per declaration, with deterministic
`f0`, `f1`, ... fields in source declaration order; empty structs
also have a dummy byte. Struct types are emitted after their dependencies.
This is an **internal implementation layout**, not
a stable language or foreign-function ABI.

The C runtime helpers use `int64_t` arithmetic and checks before converting
to `int32_t`. They call `abort()` for signed addition, subtraction,
multiplication and negation overflow, division by zero, and the
`INT32_MIN / -1` case. Unreachable IR join blocks may contain explicit
`trap` terminators; unreachable code is omitted from emitted C.

For bootstrap `i32`, explicit wrapping operations and bitwise results
use 32-bit unsigned arithmetic and reconstruct the signed value with a
`int64_t` intermediate. This implements modulo `2^32` without
relying on undefined C signed overflow or implementation-defined
out-of-range unsigned-to-signed casts. Broader language-level
wrapping/cast decisions remain open.

## Not yet supported by the backend

Slices, pointers, floating-point values, enums, unions, type aliases,
signed shifts (`<<`, `>>`, `<<=`, `>>=`) and multi-width integer
arithmetic have not yet been lowered. Signed right-shift semantics
are still open in the language specification. Optional
inspection/unwrapping and pattern matching are not yet implemented.
Nested indexed assignment (`a[i][j] = x`), slice creation/indexing,
arbitrary `usize` arithmetic and indexed targets other than a named
mutable array local are not yet supported.
Nested struct field writes (`object.child.value = x`) are not yet
supported; directly assigned struct fields must belong to a `var`
local. Recursive struct value types are disallowed. Struct visibility
between modules and C foreign-function layout are not implemented.
Some unsupported constructs already parse and type-check, but fail
explicitly at IR lowering rather than producing a misleading executable.

The C17 backend currently targets a hosted POSIX/GCC environment (the
driver uses `posix_spawnp`/`waitpid`). Native Windows process launching,
a stable ABI and freestanding targets remain future work. It should not
be used for production or safety-critical compilation.

## Validation

`make check` exercises the existing frontend suites, a new IR/verifier
suite, the CLI, and real GCC-compiled executables: literals, function
calls, recursion, loops, short-circuiting, checked arithmetic traps,
paths containing spaces, tuple returns/destructuring, `divmod`, nested
aggregates, optional construction/propagation, both `for` range modes,
boundaries and loop control flow, compound updates, and wrapping/bitwise
arithmetic. These outputs are also compiled with strict C17 warnings at
`-O2`. Array tests exercise construction, copies, `.len`,
array-of-array/tuple/optional values, reads/writes, bounds traps and
noncanonical or corrupted IR. Nominal struct tests cover field order,
copies, forward references, parameters/returns, nested aggregates,
field writes, checked overflow traps and invalid declarations.
The IR unit suite
deliberately mutates valid IR to test refusal of malformed instructions,
types, constructors, projections and terminators.
