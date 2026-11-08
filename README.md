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
Given a lack of advanced knowledge in compiler theory, much of the code might be inspired by explanatory videos, online materials, and other repositories. Part of the source code was reviewed by language models and debugged by the models themselves (This saved me a lot of time when it came to debugging).

## Build

```bash
make
./build/ccaret --version
```

