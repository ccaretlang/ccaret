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

## Build

```bash
make
./build/ccaret --version
```
