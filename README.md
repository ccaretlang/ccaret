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

Early development. The source manager and lexer are implemented; parsing and code generation are not yet implemented.

Please note that this is an educational project I’m using to improve my C skills. LLMs were used to generate the .c tests. That said, please don’t take this project too seriously.

## Build

```bash
make
./build/ccaret --version
```

