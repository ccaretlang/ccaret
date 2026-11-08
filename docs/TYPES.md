# Type system (draft 0.1)

> These are proposed rules. An initial semantic checker exists; see
> [SEMANTIC.md](SEMANTIC.md) for the implemented subset and its limits.

## Core rules

- Static, strong type checking. Inference is local, not global trait or constraint solving.
- No implicit lossy numeric conversions. Integers are not truthy.
- `let` is an immutable binding, `var` is mutable, `const` is compile time, `static var` is static storage.
- Locals must be initialized before they are read. Heap allocation is never automatic.
- Aggregates copy by value. Copying a struct that holds a pointer copies the pointer, not the allocation.

## Primitive types

| Kind | Types |
|---|---|
| Signed integers | `i8`, `i16`, `i32`, `i64`, `isize` |
| Unsigned integers | `u8`, `u16`, `u32`, `u64`, `usize` |
| Floating point | `f32`, `f64` |
| Logic and none | `bool`, `void` |
| C interop aliases | `c_char`, `c_int`, `c_long` (target-specific) |

`usize` and `isize` match the target pointer width. The storage size of `bool` depends on the target layout. The size and alignment of C-compatible primitives follow the target ABI.

An untyped integer literal takes its type from context, or `i32` when there is none. Float literals default to `f64`. A literal that does not fit its type is a compile error.

## Arithmetic

- Ordinary signed `+`, `-` and `*` trap on overflow.
- Ordinary unsigned arithmetic wraps modulo 2^N.
- `+%`, `-%` and `*%` wrap explicitly for both signed and unsigned integers.
- Integer division by zero traps. Signed minimum divided by `-1` traps.
- A shift past the operand width traps. The exact signed right-shift rule is still open.
- `value as T` is an explicit cast. Checked versus truncating behavior must be decided before implementation. `truncate<T>(value)` is the planned intrinsic for intentional narrowing.

These rules must hold at every GCC optimization level. The backend must not rely on the undefined behavior of signed overflow in the generated C.

## Composite types

| Form | Meaning |
|---|---|
| `[N]T` | Fixed array, elements inline |
| `[]T`, `[]const T` | Non-owning slice: pointer plus length |
| `*T`, `*const T` | Raw pointer. A non-null representation does not prove validity |
| `?*T`, `?T` | Optional pointer or value |
| `(T, U)` | Tuple and multiple return values |
| `Result<T,E>` | Intrinsic tagged result |
| `fn(T) -> U` | Function pointer. Calling convention still to be specified |
| `struct`, `enum`, `union` | Named aggregate types |

Arrays and slices are different types. Any implicit array to slice conversion needs an exact rule. A slice does not own memory and cannot stop a use-after-free.

In the bootstrap fixed-array subset, `[N]T` values copy by value,
including when passed to and returned from functions. Array elements
can be indexed with an `i32` expression; negative or out-of-bounds
indices trap. A mutable local array supports element assignment and
`i32` compound element assignments. Its length is a `usize` value.
The current C17 backend limits `N` to 65536, independently of the
language's eventual layout and allocation limits.

## Structs, enums and unions

```caret
struct Vec2 { x: f32, y: f32, }
enum Message { Quit, Move(Vec2), Text([]const u8), }
@repr(c) union Bits { word: u32, bytes: [4]u8, }
```

The bootstrap compiler supports **nominal** struct declarations with
named fields, empty structs and `Name { field: expression, ... }` value
construction. All fields must be initialized exactly once. Two distinct
struct names are incompatible even with identical fields. Struct
values copy by value, including parameters and return values; a
mutable local can update one of its fields. Struct types may nest
inside fixed arrays, tuples, optionals and other structs. Recursive
by-value structures (direct or indirect) are rejected; pointers to
recursive types await the later pointer milestone.

Across local modules, `pub struct` exports a nominal type and `pub`
on fields marks them visible. The current linker allows external use of
a public struct only if **all** its fields are public. Opaque imports
and selective field access are not yet supported. Public function
signatures cannot expose private or nonexportable struct types. This
is a bootstrap compiler limitation, not the final privacy model.

An enum with a payload is a tagged sum and supports exhaustive `match`. A raw union has no tag. Reading a field that does not hold a valid value is the programmer's responsibility. Fields are private unless marked `pub`.

Default aggregate layout is controlled by the compiler. `@repr(c)` asks for the target C layout. Layout constraints are in [ABI.md](ABI.md).

## Generics (planned)

```caret
fn identity<T>(value: T) -> T { return value; }
struct Pair<T,U> { first: T, second: U, }
```

The backend monomorphizes and caches each concrete type combination. There are no lifetime or borrow parameters and no general trait resolver. Generic constraints and diagnostics are still open.

## Tests to write

Tests should cover literal type inference, invalid and narrowing casts, struct copies, tuple destructuring, enum exhaustiveness, optional layouts, invalid pointer operations, and target ABI layout.
