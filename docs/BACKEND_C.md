# C17 backend and executable pipeline

The C^ bootstrap compiler can produce executables for a **restricted typed
subset**, including structural tuples and optional values. The C backend
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
  around a type), literal integers, boolean literals and unary `-`/`!`.
- Checked `i32` `+`, `-`, `*`, `/` and `%`; equality and numeric
  comparisons; boolean `&&`/`||` with short-circuit evaluation.
- Functions with typed parameters, forward and recursive calls, explicit
  returns, local `let`/`var`, plain assignments, `if`/`else if`/
  `else`, `while`, `loop`, `break`, `continue`.
- Tuple literals and types (including `()` and `(T,)`), nested tuple
  members `value.0`/`value.1`, tuple parameters and returns,
  destructuring in `let`/`var`, and forwarding aggregate values through
  calls and variables.
- Optional `?T` types, `some(value)` and `none` construction, optional
  parameters/returns, nested optionals and optionals inside tuples (and
  tuples inside optionals). `none` requires a contextual optional type.
- Arguments are lowered left-to-right before the C function call, so the
  generated C's unspecified argument evaluation order has no effect.
  Tuple elements are similarly sequenced before construction.

Composite IR type IDs map to deterministic C17 `typedef struct` types
(`cc_t_3`, `cc_t_4`, etc.) in dependency order. Tuple elements use
`f0`, `f1`, ... fields; a zero-element tuple has a dummy byte because
empty structs are not standard C17. Optionals use a `bool has` tag and a
typed `value` payload. This is an **internal implementation layout**, not
a stable language or foreign-function ABI.

The C runtime helpers use `int64_t` arithmetic and checks before converting
to `int32_t`. They call `abort()` for signed addition, subtraction,
multiplication and negation overflow, division by zero, and the
`INT32_MIN / -1` case. Unreachable IR join blocks may contain explicit
`trap` terminators; unreachable code is omitted from emitted C.

## Not yet supported by the backend

Arrays, slices, pointers, floating-point values, user-defined types,
`for` ranges, compound assignments and most bitwise operations have not
yet been lowered. Optional inspection/unwrapping and pattern matching
are not yet implemented. Some unsupported constructs already parse and
type-check, but fail explicitly at IR lowering rather than producing a
misleading executable.

The C17 backend currently targets a hosted POSIX/GCC environment (the
driver uses `posix_spawnp`/`waitpid`). Native Windows process launching,
a stable ABI and freestanding targets remain future work. It should not
be used for production or safety-critical compilation.

## Validation

`make check` exercises the existing frontend suites, a new IR/verifier
suite, the CLI, and real GCC-compiled executables: literals, function
calls, recursion, loops, short-circuiting, checked arithmetic traps,
paths containing spaces, tuple returns/destructuring, `divmod`, nested
aggregates and optional construction/propagation. Aggregate outputs are
also compiled with strict C17 warnings at `-O2`. The IR unit suite
deliberately mutates valid IR to test refusal of malformed instructions,
types, constructors, projections and terminators.
