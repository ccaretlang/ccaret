# C^ documentation

C^ (pronounced "C Caret") is a programming language. The compiler is called `ccaret`, and source files use the `.caret` extension.

Status: draft 0.1. Most of this folder describes what the language should become. The current `ccaret` includes a source manager and lexer and can dump tokens, but it does not parse `.caret` files yet.

## Start here

| Document | What it covers |
|---|---|
| [SPEC.md](SPEC.md) | Goals, design principles, language overview |
| [LEXICAL.md](LEXICAL.md) | Keywords, operators, literals, lexical rules |
| [GRAMMAR.md](GRAMMAR.md) | Source grammar and expression precedence |
| [TYPES.md](TYPES.md) | Primitive, aggregate, pointer and generic types |
| [MEMORY.md](MEMORY.md) | Manual memory, safety boundary, `defer` and `errdefer` |
| [ERRORS.md](ERRORS.md) | `?T`, `Result<T,E>`, `try`, pattern matching |
| [MODULES.md](MODULES.md) | Modules, imports, visibility, compilation units |
| [ABI.md](ABI.md) | C ABI, layout, FFI, freestanding targets |
| [IR.md](IR.md) | Intermediate representation and lowering rules |
| [COMPILER.md](COMPILER.md) | Compiler architecture, driver, diagnostics, tests |
| [ROADMAP.md](ROADMAP.md) | Phases and implementation checklist |
| [DECISIONS.md](DECISIONS.md) | Settled direction, proposals and open questions |

## Status labels

- **Agreed**: a project-level decision. It may still be unimplemented.
- **Proposed**: a design idea that can change after review.
- **Open**: no decision yet.
- **Implemented**: only when the repository has working code and tests.

An example does not mean a feature works. Only the **Implemented** label does.

## Updating the spec

Settle the grammar, type rules, runtime behavior, error cases and tests before writing the implementation. If a significant design decision changes, record it in [DECISIONS.md](DECISIONS.md) as well.
