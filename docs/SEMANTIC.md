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
  `isize`/`usize`, `f32`/`f64`, and declared nominal structs;
  reject unknown type names.
- Resolve pointers, slices, optionals, tuples, and fixed arrays, preserving
  the pointee/element `const` qualifier. Array lengths are nonnegative,
  constant unsigned-integer expressions using literals, `+`, `-`, `*`,
  `/`, `%`, and parentheses. Zero denominators and overflows are rejected.
- Register all function signatures before checking bodies, allowing forward
  calls and recursion. Reject duplicate function/parameter names and calls
  with the wrong argument count or types.
- Register nominal struct types independent of declaration order, check
  duplicate type/field names, reject recursive value types and void fields,
  require every struct literal field exactly once, and check field reads,
  assignments and nominal type compatibility.
- In modular files, enforce acyclic import graphs, namespace aliases and
  `pub` visibility for imported functions/structs. Reject missing modules,
  path/name mismatches, private uses and public interfaces exposing
  private struct types. The initial imported-struct subset requires all
  of its fields to be `pub`.
- Track lexical scopes and the mutability of `let`/`var` bindings.
  Resolve identifiers, prevent duplicate bindings in the same scope, reject
  use before declaration, and check assignment type and mutability.
- Support `&name` for local address-taking, with `*const T` for `let`
  and `*T` for `var`; require a lexical `unsafe` block for `*pointer`
  reads and writes. Reject stores through `*const T`, non-pointer
  dereferences, non-lvalue address targets and mismatched pointer types.
  Nested `unsafe` blocks retain lexical scoping and do not disable type checks.
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

- Not yet supported: enums, unions, generics, C ABI aliases,
  type aliases, and function values. The current module system uses one
  file per module, local root-relative imports and `pub` visibility.
  Opaque imported structs, selective field access and public C layouts
  remain future work.
- Raw pointers do not provide lifetime, aliasing, provenance or ownership
  checks. Returning the address of a local or otherwise retaining a pointer
  past its referent's lifetime can dangle. No general escape analysis exists.
- Assignment targets are mutable local variables, single-level indexed
  elements of mutable fixed-array locals, named fields of mutable
  struct locals, or raw-pointer targets inside `unsafe`. Nested field/index
  writes, tuple members and slice writes remain unsupported.
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
