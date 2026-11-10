<p align="center">
  <img src="./assets/logo.svg" width="120" alt="C Caret Logo">
</p>

# C^ (C Caret)

## Compiler architecture

C^ source
    -> lexer
    -> parser
    -> semantic analysis
    -> intermediate representation (IR)
    -> C backend
    -> GCC
    -> native executable

## Status

Early bootstrap compiler development. The parser, semantic analyzer,
independently verified typed IR, C17 backend and GCC build driver are
implemented for a restricted executable subset. This includes tuples,
optionals, nominal structs and tagged enums with exhaustive `match`
statements, fixed arrays, scoped unsafe pointers, slice views and local
modules. See [docs/COMPILER.md](docs/COMPILER.md) and
[docs/BACKEND_C.md](docs/BACKEND_C.md) for implemented features and limits.

Please note that this is an educational project I’m using to improve my C skills. LLMs were used to generate the .c tests. That said, please don’t take this project too seriously.
Given a lack of advanced knowledge in compiler theory, much of the code might be inspired by explanatory videos, online materials, and other repositories. Part of the source code was reviewed by language models and debugged by the models themselves (This saved me a lot of time when it came to debugging).

## Build

```bash
make
./build/ccaret --version
```

