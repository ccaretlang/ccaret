# C17 backend and executable pipeline

The C^ bootstrap compiler can produce executables for a **restricted typed
subset**, including structural tuples, fixed arrays, nominal structs,
tagged enums and optional values. The C backend
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
  `optional.has` obtains the presence flag as `bool`;
  `optional.value` obtains the full typed payload, aborting if absent,
  including nested and composite payloads. The source is evaluated once.
- Exhaustive `match optional_expression { some(pattern) => { ... },
  none => { ... }, }` statements: an immutable payload binding (including
  tuple destructuring), order-independent arms, nested matches, fallthrough
  to following statements and fully-returning arms. Each optional value is
  evaluated once and lowered to verified IR `optional.has`/`condbr` with
  `optional.unwrap` exclusively on the `some` path. Match expressions,
  wildcard arms and guards are not implemented.
- Nominal `enum Name { Empty, Value(T), Pair((T, U)) }` declarations with
  unit or single-payload variants, type-checked constructors,
  by-value copies/parameters/returns and arbitrary nesting with already
  supported composite types. Exhaustive `match` requires one arm per
  variant, with arm-local immutable payload pattern bindings (including
  tuple destructuring), evaluated-once scrutinee and correct fallthrough
  and return behavior. Lowering emits verified `enum.make` / `enum.is` /
  `enum.get` plus ordinary branches and slots; no AST-to-C bypass.
  Recursive by-value enums are rejected, as are wildcard/guard patterns
  and general nested enum variant patterns.
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
- Local acyclic source modules with `module`, `import` (optionally
  `as`-aliased), `pub` and cross-module functions/structs/enums. Module
  resolution checks explicit names and visibility before lowering. One
  canonical linked source unit passes through the same verified IR and
  C17 emitter.
- Raw pointer types `*T` and `*const T`, including pointers to supported
  composite values, pointer parameters/returns and optional pointers.
  `&local` forms a pointer to an existing local slot, with `let` producing
  a read-only pointee and `var` a writable one. `*p` reads and direct
  `*p = value` / `*p += value` (checked `i32` compounds) require a source
  `unsafe { ... }` statement. Dereference checks are static type checks,
  not runtime null, bounds, provenance or lifetime checks.
- Non-owning `[]T` / `[]const T` views from `slice(local_array)` with
  a checked `i32` index and dynamic `usize` `.len`. Views preserve
  backing storage rather than copying it; `let` arrays yield const
  views and `var` arrays yield writable views. Named mutable-element
  slices support direct indexed writes and checked `i32` compound
  updates (even when the slice's binding is `let`). Slices can be
  passed, returned and included in supported aggregates. Their lifetime
  is not checked or extended.
- Checked exclusive subranges `array_local[from..to]` and
  `slice_expression[from..to]`, with `i32` bounds, source-order
  single evaluation, retained element constness and aliasing. Negative
  bounds, `end < begin` and `end > source.len` abort. Empty views and
  nested subranges are supported. Array temporaries and inclusive
  subranges are rejected.
- Arguments are lowered left-to-right before the C function call, so the
  generated C's unspecified argument evaluation order has no effect.
  Tuple elements are similarly sequenced before construction.

Composite IR type IDs map to deterministic C17 typedefs
(`cc_t_3`, `cc_t_4`, etc.) in dependency order. Tuple elements use
`f0`, `f1`, ... fields; a zero-element tuple has a dummy byte because
empty structs are not standard C17. Optionals use a `bool has` tag and a
typed `value` payload. Checked extraction emits an explicit
`if (!r.has) abort();` before reading `r.value`, so the uninitialized
or semantically absent payload of `none` is never accessed.
For a `match`, the emitted C branches on `has`; the IR unwrap (and its
safety guard) appears only in the `some` arm. The `none` arm never reads
the payload. Matching requires no direct AST-to-C special case.
Tagged enums use a deterministic internal C17 struct with a `uint32_t tag`
and a union containing one typed field per payload-bearing variant;
unit variants use a distinct tag and no active payload. `enum.get`
emits an explicit tag guard followed by `abort()` on mismatch, before
accessing the union member. Matching compares the tag and branches on
the verified IR condition. The internal layout is **not** a frozen C ABI.
Arrays use a fixed `elements[N]` field, or a
dummy byte for `N = 0`, because zero-length C arrays are not standard
C17. Array lengths are limited to 65536 in this bootstrap backend.
Nominal structs use one C17 typedef per declaration, with deterministic
`f0`, `f1`, ... fields in source declaration order; empty structs
also have a dummy byte. Struct types are emitted after their dependencies.
Raw pointer types use corresponding C pointer typedefs (`T *` or
`const T *`), and pointer temporaries are initialized to `NULL`.
Slices use internal `typedef struct { T *data; size_t len; }` layouts;
const-element slices use `const T *data`. Zero-length slices contain
`NULL` and length zero; any indexing traps before dereference. Positive
length slices point into a fixed-array local. Slice index checks call
`ccrt_index_i32` before C pointer indexing. `ccrt_subrange_i32`
validates bounds **before** pointer addition and length computation.
An empty `NULL`-backed slice remains `NULL` when subranged, avoiding
undefined null-pointer arithmetic; a valid zero-length range at the end
of a nonempty array may hold a one-past pointer. This does not validate
backing-storage lifetime or support a foreign ABI.
The generated code does not perform lifetime or null checks before
dereferencing. This is an **internal implementation layout**, not
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

Pointer-origin slice creation, pointer arithmetic, casts,
volatile pointers, floating-point values, unions, type aliases,
signed shifts (`<<`, `>>`, `<<=`, `>>=`) and multi-width integer
arithmetic have not yet been lowered. Signed right-shift semantics
are still open in the language specification. Match expressions,
guards, wildcard and recursive enum-pattern matching, generic
`Result<T,E>` and implicit optional chaining are not yet implemented.
Nested indexed assignment (`a[i][j] = x`), arbitrary `usize` arithmetic,
and indexed targets other than a named mutable array local or named
mutable-element slice local are not yet supported.
Nested struct field writes (`object.child.value = x`) are not yet
supported; directly assigned struct fields must belong to a `var`
local. Recursive struct/enum value types, including cycles through
raw pointers, are currently disallowed. Modules currently
use one source file per dotted path and a root inferred from the entry
filename. Imported public structs require all fields to be `pub`;
opaque imported structs, import re-exports, package search roots and
C foreign-function layout are not implemented. See [MODULES.md](MODULES.md).
Some unsupported constructs already parse and type-check, but fail
explicitly at IR lowering rather than producing a misleading executable.

The C17 backend currently targets a hosted POSIX/GCC environment (the
driver uses `posix_spawnp`/`waitpid`). Native Windows process launching,
a stable ABI and freestanding targets remain future work. It should not
be used for production or safety-critical compilation.

## Validation

`make check` also exercises optional presence and checked extraction
through IR mutation tests, correct/invalid source programs, nested
aggregates, pointer and slice payloads, imported functions, GCC `-O2`
with strict warnings and abort-on-`none` cases. Run
`sh tests/test_optional_access_sanitizers.sh` for focused ASan/UBSan
coverage of the optional inspection pipeline.

Run `sh tests/test_enum_sanitizers.sh` for ASan/UBSan coverage of the
parser/semantic/IR verifier, tagged enum construction and exhaustive
matching (including tagged unions containing tuples, optionals, arrays,
structs, pointers and slices), generated C17, a three-module executable
and a compiler compiled with strict `-O2 -Werror` flags.

`make check` runs the positive, negative, trap and module-linking
subrange fixtures under the normal compiler, including strict
`-O2 -Werror` builds of generated C. For an optional ASan/UBSan pass,
run `sh tests/test_subrange_sanitizers.sh` after `make check`:
it instruments parser/semantic/IR tests, the compiler and all
subrange fixture executables, then checks an optimized compiler build.

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
Pointer tests exercise `*const`, nested pointers, composite pointees,
optionals, function calls, CFG-sensitive pointer stores, type/IR mutation
rejection and checked-overflow aborts under optimized GCC.
Slice tests exercise mutable/const element views, aliasing of backing arrays,
zero-length slices, nested aggregates, optionals, parameter/return values,
CFG-sensitive checked compound writes, bounds aborts, malformed IR
rejection and optimized C17.
The IR unit suite
deliberately mutates valid IR to test refusal of malformed instructions,
types, constructors, projections and terminators.
Multi-file tests check optimized executables, transitive imports,
namespace isolation, visibility, dependency failures and error locations.
