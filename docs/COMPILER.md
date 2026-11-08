# Compiler design and build strategy (draft 0.1)

## Current state

The repository has a C17 CLI, UTF-8 source manager, lexer, arena-allocated
AST and parser, initial semantic checker, typed non-SSA IR with verifier,
and a C17 backend. The `ccaret tokens`, `check`, `emit-ir`, `emit-c`
and `build` commands work for their documented subset. Source programs
using `i32`, `bool`, `void`, nested tuple values and optional
`?T` construction/propagation, `i32` bitwise and wrapping arithmetic,
checked compound updates, integer `for` ranges, fixed arrays `[N]T`
with checked indexing, nominal struct values and locally imported
`pub` functions/structs and scoped raw-pointer `unsafe` operations
can become GCC-compiled executables. The frontend supports additional
syntax and types that are **not yet lowerable**, including optional
unwrapping/pattern matching. See
[SEMANTIC.md](SEMANTIC.md) and [BACKEND_C.md](BACKEND_C.md).
The local module linker builds an acyclic dependency graph, applies
AST-guided namespace resolution and submits an internal canonical source
unit to the same semantic/IR/verifier/backend pipeline. Original file
locations are preserved in a diagnostic map; see [MODULES.md](MODULES.md).

## Planned architecture

```text
.caret file
    -> Source Manager (file bytes, UTF-8, source spans)
    -> Lexer (token stream)
    -> Parser (recursive descent + Pratt expressions)
    -> AST (arena allocated)
    -> Symbol resolution and type checking
    -> Typed IR lowering + verifier
    -> C backend (generated C17)
    -> GCC driver
    -> Native executable
```

The C backend is a bootstrap choice, not the definition of the language. The IR is there from v0.1 so a future QBE, LLVM or native backend does not have to rewrite the frontend.

## Source tree

```text
src/
  main.c
  lexer/
  parser/
  semantic/
  ir/
  backend/c/
  driver/
include/
std/
tests/
examples/
docs/
```

## CLI proposal (partially implemented)

```sh
ccaret --help
ccaret --version
ccaret tokens examples/hello.caret
ccaret check examples/hello.caret
ccaret emit-ir examples/hello.caret
ccaret emit-c examples/hello.caret -o build/hello.c
ccaret build examples/hello.caret -o hello
```

All five commands listed above are implemented, with the limitations
in [BACKEND_C.md](BACKEND_C.md). Unsupported constructs fail explicitly.

## C implementation conventions

- C17, strict warnings, and documented ownership for every heap buffer.
- Tokens keep source spans instead of copied strings. The source manager owns the buffers.
- Arena allocation for the AST and temporary semantic data, string interning for identifiers, stable integer IDs for symbols, types and IR objects.
- Avoid quadratic concatenation, quadratic lookup and unnecessary copies. Test error paths and out-of-memory handling.
- Invoke GCC through an argument vector, not an unsanitized shell string.
- Keep diagnostics tagged by stage: lexer, parser, semantic analyzer, IR verifier, GCC driver, linker.
- Keep generated C around for debugging and give generated symbols deterministic names.

## First end-to-end test

```caret
fn main() -> i32 {
    return 42;
}
```

`ccaret build examples/hello.caret -o hello` produces a binary that
exits with status `42` on supported platforms. The regression suite also
compiles and executes `tests/fixtures/check_valid.caret`, where `divmod`
returns a tuple, with exit status `5`.

## Performance measurements

Measure frontend time, GCC time, cold and warm build time, peak compiler memory, binary size and runtime against equivalent C. Benchmark before adding a custom backend or heavy optimization passes.
