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
  `isize`/`usize`, `f32`/`f64`, and declared nominal structs/enums;
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
- Register tagged enums and their variant names independent of declaration
  order; require at least one variant, reject duplicates, recursive
  by-value enum/struct cycles and `void` payloads. Each variant has zero
  or one payload with an explicit type, including tuples and nested
  supported aggregates. Check type-correct enum constructors
  (`Enum.Empty` / `Enum.Payload(value)`), nominal type compatibility,
  by-value parameter/return propagation and invalid constructor arity.
- In modular files, enforce acyclic import graphs, namespace aliases and
  `pub` visibility for imported functions/structs/enums. Reject missing
  modules, path/name mismatches, private uses and public interfaces exposing
  private nominal types. The initial imported-struct subset requires all
  of its fields to be `pub`; imported public enum variants are available
  as a group, with no per-variant `pub` syntax.
- Track lexical scopes and the mutability of `let`/`var` bindings.
  Resolve identifiers, prevent duplicate bindings in the same scope, reject
  use before declaration, and check assignment type and mutability.
- Support `&name` for local address-taking, with `*const T` for `let`
  and `*T` for `var`; require a lexical `unsafe` block for `*pointer`
  reads and writes. Reject stores through `*const T`, non-pointer
  dereferences, non-lvalue address targets and mismatched pointer types.
  Nested `unsafe` blocks retain lexical scoping and do not disable type checks.
- Recognize the explicit `slice(name)` intrinsic for a named local
  fixed array. A `var` array produces `[]T`; a `let` array produces
  `[]const T`. No implicit conversion or temporary-array view is
  accepted. Type-check `.len`, checked indexing and indexed writes;
  const-element slices reject writes even when their binding is `var`,
  while an immutable `let` binding of `[]T` may modify its elements.
- Accept exclusive `a[from..to]` subranges with two `i32` bounds
  on a named fixed-array local or any slice-valued expression. An
  array-local's `let`/`var` status determines element constness;
  subranges from slices retain the source slice's element qualifier.
  Reject inclusive subranges, nonlocal/temporary array origins and
  incompatible bound types. Dynamic range validity is checked by
  generated code, not proven statically.
- Infer simple local types, using `i32` for an unconstrained integer
  literal. Check integer-literal range against the contextual integer type
  (including unary negation), with no implicit narrowing between concrete
  numeric types.
- Type-check arithmetic, comparisons, boolean and bitwise operations,
  tuple and fixed-array literal construction, indexed access, basic
  array/slice indexing and `.len`; fixed-array literals enforce element
  types, arity and contextual typing for `[]`.
  Check `?T` construction with `some(value)` and `none`, plus
  `optional.has` (returns `bool`) and `optional.value` (returns
  the exact payload type `T`). Reject unrecognized optional members
  and type-incompatible uses; unwrapping `none` is a runtime trap,
  not a compile-time diagnostic. Optional expressions evaluate once.
- Check tuple patterns (including nested destructuring), tuple arity,
  parameter/return types, `if`/`while` conditions, integer `for` ranges,
  and `break`/`continue` placement.
- Type-check `match` statements on optional `?T`: require exactly one
  `some(pattern)` and one `none` arm in either order, reject duplicate
  variants or missing coverage, and bind `T` immutably only within the
  `some` arm. Tuple patterns reuse `let` destructuring and arity checks.
  The scrutinee is evaluated once; checking each body preserves normal
  loop nesting, assignment and return rules. Imported references inside
  arms undergo the same module-linker namespace resolution.
- Type-check exhaustive enum `match` statements: the scrutinee must have
  a nominal enum type, every declared variant must appear **exactly once**
  and each arm must name that enum and provide a binder if and only if
  the variant has a payload. Bindings are immutable and arm-local; tuple
  destructuring obeys existing pattern arity/type checks. Unknown
  variants, duplicated arms, missing arms or mismatched enum names fail
  before IR lowering. Import-qualified enum arms are rewritten to the
  canonical linked name; the scrutinee is evaluated exactly once.
- Conservatively check that functions returning a non-`void` type return
  along every path made of `return`, complete `if`/`else` chains and
  exhaustive optional/enum matches whose arms all return.
  Nontrivial loop termination is not yet proven.

## Limits of this first pass

- Not yet supported: unions, generic enums/generics, C ABI aliases,
  type aliases, and function values. The current module system uses one
  file per module, local root-relative imports and `pub` visibility.
  Opaque imported structs, selective field access and public C layouts
  remain future work.
- Raw pointers do not provide lifetime, aliasing, provenance or ownership
  checks. Returning the address of a local or otherwise retaining a pointer
  past its referent's lifetime can dangle. No general escape analysis exists.
- Slices do not extend backing storage lifetime; copying, passing or
  returning a slice is not checked for use-after-scope. The checker
  enforces element constness and source array mutability, not aliasing.
  Subranges do not gain new ownership, provenance or borrowing guarantees.
- Assignment targets are mutable local variables, single-level indexed
  elements of mutable fixed-array locals, direct indices on named
  writable-element slice locals, named fields of mutable struct locals,
  or raw-pointer targets inside `unsafe`. Nested field/index writes,
  tuple members and direct indexed assignment into temporary subrange
  expressions (e.g. `a[1..3][0] = x`) remain unsupported. Bind the
  subrange to a named slice local and update through that binding instead.
- Float type signatures and operations on float *variables* are recognized;
  floating-point literal expressions still await parser support.
- Integer literals are checked for representable values (including the
  unannotated `i32` minimum, `-2147483648`), but full
  compile-time arithmetic evaluation, integer overflow proofs at runtime,
  and target ABI layout analysis are not implemented.
- `none` requires an optional type context; result type inference is
  deliberately local, not global constraint solving.
- Optional `.value` is always checked at runtime; there is no
  compile-time proof/flow narrowing from `.has`, optional chaining
  or implicit unwrap. `match` is a statement only, supporting
  exhaustive optional and nominal enum arms, without guards, wildcard
  arms, general nested variant patterns, match expressions or
  `Result<T,E>` propagation.
- Only the first semantic error is reported. Definite assignment across all
  paths, exhaustive control-flow analysis, liveness, constant propagation,
  and persistent typed IR are future work.

Run `make check` for lexer, parser, tuple parser, semantic and CLI tests.
The semantic suite covers valid and invalid files and the reference
`divmod`/tuple destructuring example.
