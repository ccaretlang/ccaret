# First semantic analysis pass

The C^ frontend has a separate semantic stage between parsing and typed IR
lowering. The command to run it is:

```sh
make check
./build/ccaret check tests/fixtures/check_valid.caret
```

The checker uses source spans to report the first problem with a file, line,
and column. It does not generate code. Its temporary types and symbol tables
are arena-allocated; it does not yet annotate the AST with persistent
`TypeId`/`SymbolId` values for the future IR.

## Implemented checks

- Resolve built-in `void`, `bool`, signed/unsigned integer types,
  `isize`/`usize`, `f32`/`f64`; reject unknown type names.
- Resolve pointers, slices, optionals, tuples, and fixed arrays, preserving
  the pointee/element `const` qualifier. Array lengths are nonnegative,
  constant unsigned-integer expressions using literals, `+`, `-`, `*`,
  `/`, `%`, and parentheses. Zero denominators and overflows are rejected.
- Register all function signatures before checking bodies, allowing forward
  calls and recursion. Reject duplicate function/parameter names and calls
  with the wrong argument count or types.
- Track lexical scopes and the mutability of `let`/`var` bindings.
  Resolve identifiers, prevent duplicate bindings in the same scope, reject
  use before declaration, and check assignment type and mutability.
- Infer simple local types, using `i32` for an unconstrained integer
  literal. Check integer-literal range against the contextual integer type
  (including unary negation), with no implicit narrowing between concrete
  numeric types.
- Type-check arithmetic, comparisons, boolean and bitwise operations,
  tuple and fixed-array literal construction, indexed access, basic
  array/slice indexing and `.len`; fixed-array literals enforce element
  types, arity and contextual typing for `[]`.
  Check `?T` construction with `some(value)` and `none`.
- Check tuple patterns (including nested destructuring), tuple arity,
  parameter/return types, `if`/`while` conditions, integer `for` ranges,
  and `break`/`continue` placement.
- Conservatively check that functions returning a non-`void` type return
  along every path made of `return` and complete `if`/`else` chains.
  Nontrivial loop termination is not yet proven.

## Limits of this first pass

- Not yet supported: user-defined structs, enums, generics, imports, C ABI
  aliases, type aliases, and function values. Named types are limited to the
  built-ins listed above.
- Pointer dereference/address-taking and other unsafe operations require a
  later safety-aware pass. The checker rejects those expressions explicitly.
- Assignment targets are mutable local variables or single-level indexed
  elements of a mutable fixed-array local. Nested indexed writes, tuple
  members and slice writes remain unsupported.
- Float type signatures and operations on float *variables* are recognized;
  floating-point literal expressions still await parser support.
- Integer literals are checked for representable values (including the
  unannotated `i32` minimum, `-2147483648`), but full
  compile-time arithmetic evaluation, integer overflow proofs at runtime,
  and target ABI layout analysis are not implemented.
- `none` requires an optional type context; result type inference is
  deliberately local, not global constraint solving.
- Only the first semantic error is reported. Definite assignment across all
  paths, exhaustive control-flow analysis, liveness, constant propagation,
  and persistent typed IR are future work.

Run `make check` for lexer, parser, tuple parser, semantic and CLI tests.
The semantic suite covers valid and invalid files and the reference
`divmod`/tuple destructuring example.
