# C^ language specification (draft 0.1)

> Design proposal, not a stable spec or a status report. Open questions are in [DECISIONS.md](DECISIONS.md).

## The project

- Language: **C^**, pronounced **C Caret**.
- Compiler: `ccaret`. Source files: `.caret`.
- The first compiler is written in **C17** and developed on Linux/WSL2.
- Pipeline: source -> lexer -> parser/AST -> semantic analysis -> typed IR -> C17 backend -> GCC -> native executable.
- The IR is part of the design from the start. Other backends can be added later without changing language semantics.

## Goals

1. Cover the low-level work C is used for: raw memory, pointer arithmetic, layouts, ABI, callbacks, syscalls, atomics, volatile MMIO, freestanding code, custom allocators and platform interfaces.
2. Produce native programs with predictable allocations, copies, memory use and execution cost.
3. Require no VM, garbage collector, exception unwinding, global allocator or language runtime.
4. Fix C's rough edges with real modules, strong static types, non-implicit nullability, slices, tagged unions, exhaustive matching, typed errors and structured cleanup.
5. Keep the language small. Good diagnostics and fast builds matter more than heavy metaprogramming and complicated inference.
6. Define C^ semantics without depending on the C backend.

## Non-goals

- Source compatibility with C or the C preprocessor.
- Rust-level memory safety without an ownership or borrow system.
- Classes, inheritance, garbage collection, a mandatory async runtime, implicit exceptions, procedural macros, hidden allocations.
- Zero storage overhead for every construct. Slices, tagged enums and `Result` may need extra bytes.

## Example

```caret
module app.main;

fn divmod(a: i32, b: i32) -> (i32, i32) {
    return (a / b, a % b);
}

pub fn main() -> i32 {
    let (q, r) = divmod(17, 5);
    return q + r;
}
```

The caller must pass a nonzero divisor. Division overflow and trap rules are in [TYPES.md](TYPES.md).

## Language surface

- Functions: `fn`, `return`, optional `-> Type`. An omitted return type means `void`.
- Bindings: `let` immutable, `var` mutable, `const` compile time, `static` for static storage.
- Data: `struct`, raw `union`, tagged `enum`, arrays `[N]T`, slices `[]T`, tuples `(T,U)`, pointers `*T`, optionals `?T` and `Result<T,E>`.
- Control: `if`, `else`, `match`, `for`, `while`, `loop`, `break`, `continue`.
- Errors and cleanup: `defer`, `errdefer`, `try`.
- Modules: `module`, `import`, `pub`, and qualified names with `.`.
- Low-level: `extern "C"`, `@repr(c)`, `unsafe`, intrinsics, explicit allocators.
- Generics: planned, using monomorphization. No general trait solver in the first version.

## Evaluation and runtime rules

- Values and normal function arguments evaluate left to right. `&&` and `||` short circuit.
- A condition must be `bool`. There is no C-style integer truthiness.
- Assignment is a statement. It does not produce a value.
- Locals are initialized before use. There are no implicit lossy numeric conversions.
- Signed integer overflow traps. Unsigned arithmetic wraps. `+%`, `-%` and `*%` wrap explicitly. The exact wrapping rule is not settled yet.
- Division by zero traps. Signed minimum divided by `-1` traps. Slice indexing is checked, and redundant checks may be optimized away.
- In the executable bootstrap, `?T` supports `.has` inspection and
  checked `.value` extraction; accessing `.value` on `none` traps.
  Exhaustive `match` statements distinguish `some(pattern)` and `none`;
  both arms are mandatory and the scrutinee runs exactly once. The
  immutable `some` binding is scoped to that arm. Enum and `Result`
  matching, guards, wildcard arms and match expressions remain planned.
- A fatal trap does not unwind the stack and does not guarantee that deferred cleanup runs.
- Release optimization settings must not change these source-level rules.

## Runtime and libraries

Allocation, I/O, the filesystem, containers, string utilities and system interfaces belong in libraries. A freestanding program should be able to leave all of them out. The library API is not specified yet.

## What to settle next

Finish [LEXICAL.md](LEXICAL.md), [GRAMMAR.md](GRAMMAR.md), [TYPES.md](TYPES.md) and the open issues in [DECISIONS.md](DECISIONS.md) before freezing the lexer and parser APIs.
