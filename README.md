<p align="center">
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="auto" preserveAspectRatio="xMinYMin meet" viewBox="0 0 256 288"><path fill="#e04b4b" d="M255.57 84.72q0-7.26-3.13-12.76a24.4 24.4 0 0 0-9.24-9C209.17 43.32 175.1 23.76 141.1 4.12c-9.17-5.29-18.06-5.1-27.16.27-13.55 7.99-81.35 46.84-101.55 58.54C4.06 67.76.02 75.13 0 84.71L0 203.08q0 7.09 2.99 12.51 3.07 5.58 9.38 9.26c20.21 11.7 88.02 50.54 101.56 58.53 9.11 5.38 18 5.57 27.17.27 34.02-19.64 68.08-39.2 102.1-58.81a24.3 24.3 0 0 0 9.4-9.26q2.98-5.41 2.98-12.52z"/><path fill="#7a1220" d="M128.18 143.5 2.98 215.6q3.08 5.56 9.4 9.25c20.2 11.7 88.01 50.54 101.55 58.53 9.11 5.38 18 5.57 27.17.27 34.02-19.64 68.08-39.2 102.1-58.81a24.3 24.3 0 0 0 9.4-9.26z"/><path fill="#7a1220" d="M91.1 164.86a42.24 42.24 0 0 0 73.52-.24l-36.44-21.11z"/><path fill="#b3262e" d="M255.57 84.72q0-7.26-3.13-12.76L128.19 143.5l124.41 72.07q3-5.41 3-12.51s0-78.9-.02-118.35"/><path fill="#fff" d="m184.6 168.3 32.4-48.6 32.4 48.6h-18.9L217 148.1l-13.5 20.2z"/><path fill="#fff" d="M164.62 164.62a42.24 42.24 0 0 1-73.52.24 42 42 0 0 1-5.57-20.97 42.26 42.26 0 0 1 78.81-21.2l36.94-21.27a84.87 84.87 0 1 0 .22 84.57z"/></svg>
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

Early development. Compiler frontend is not yet implemented.

## Build

```bash
make
./build/ccaret --version
```
