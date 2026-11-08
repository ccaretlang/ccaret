# First C17 backend and executable pipeline

The C^ bootstrap compiler can now produce executables for a **restricted
scalar subset**, using only verified typed IR as input to the C backend.

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
- Arguments are lowered left-to-right before the C function call, so the
  generated C's unspecified argument evaluation order has no effect.

The C runtime helpers use `int64_t` arithmetic and checks before converting
to `int32_t`. They call `abort()` for signed addition, subtraction,
multiplication and negation overflow, division by zero, and the
`INT32_MIN / -1` case. Unreachable IR join blocks may contain explicit
`trap` terminators; unreachable code is omitted from emitted C.

## Not yet supported by the backend

Tuple values/returns/destructuring, arrays, slices, pointers, optionals,
floating-point values, user-defined types, `for` ranges, compound
assignments and most bitwise operations have not yet been lowered. Some
are parsed and semantically checked. Such constructs fail explicitly at
IR lowering, rather than producing a misleading executable.

The C17 backend currently targets a hosted POSIX/GCC environment (the
driver uses `posix_spawnp`/`waitpid`). Native Windows process launching,
a stable ABI and freestanding targets remain future work. It should not
be used for production or safety-critical compilation.

## Validation

`make check` exercises the existing frontend suites, a new IR/verifier
suite, the CLI, and real GCC-compiled executables: literals, function
calls, recursion, loops, short-circuiting, checked arithmetic traps and
paths containing spaces. The IR unit suite deliberately mutates valid
IR to ensure the verifier rejects corrupted instructions, registers,
terminators and branch targets.
