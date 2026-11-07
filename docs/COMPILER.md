# Compiler design and build strategy (draft 0.1)

## Current state

The repository has a C17 CLI skeleton in `src/main.c` and a Makefile. It does not lex, parse, type check, emit IR or compile `.caret` programs yet. The roadmap is separate from implementation status.

## Planned architecture

```text
.caret file
    -> Source Manager (file bytes, UTF-8, source spans)
    -> Lexer (token stream)
    -> Parser (recursive descent + Pratt expressions)
    -> AST (arena allocated)
    -> Symbol resolution and type checking
    -> Typed IR + verifier + lowering
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

## CLI proposal (not implemented)

```sh
ccaret --help
ccaret --version
ccaret tokens examples/hello.caret
ccaret check examples/hello.caret
ccaret emit-ir examples/hello.caret
ccaret emit-c examples/hello.caret -o build/hello.c
ccaret build examples/hello.caret -o hello
```

Commands that do not exist yet should fail with a clear message, not pretend to work.

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

Once the pipeline works, `ccaret build examples/hello.caret -o hello` should produce a binary that exits with status `42` on supported platforms.

## Performance measurements

Measure frontend time, GCC time, cold and warm build time, peak compiler memory, binary size and runtime against equivalent C. Benchmark before adding a custom backend or heavy optimization passes.
