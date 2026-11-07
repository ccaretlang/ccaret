# Implementation roadmap

> A checklist, not a release schedule. Mark a box complete only when the code and tests are in the repository. The language specification is still a draft.

## Phase 0: Bootstrap and specification

- [x] Create `ccaretlang/ccaret` on GitHub with `main` as the primary branch.
- [x] Add an initial C17 CLI program and Makefile.
- [x] Create placeholder source, tests, examples and documentation folders.
- [ ] Add and review this documentation in the local clone, then push it to GitHub.
- [ ] Choose and add an open-source license.
- [ ] Decide and freeze the first lexer token inventory and test cases.

## Phase 1: Source manager and lexer

- [x] File loading and source span API.
- [x] UTF-8 validation and diagnostics.
- [x] Token enum covering the agreed lexical inventory.
- [x] Handwritten lexer: whitespace, comments, identifiers, keywords, literals, punctuation, operators.
- [ ] Accurate errors for bad escapes, invalid UTF-8 and unterminated constructs.
- [x] Unit tests for every token kind and ambiguity.
- [x] `ccaret tokens path.caret` command.

## Phase 2: Parser and semantic checks

- [ ] Parse function declarations, parameter lists, types, returns and arithmetic.
- [ ] AST allocated in an arena with source spans.
- [ ] Pratt expression parser with tested precedence.
- [ ] Name resolution, primitive type checking and definite assignment.
- [ ] `ccaret check` command.

## Phase 3: IR and executable pipeline

- [ ] Define typed IR and the verifier.
- [ ] Lower simple functions, calls, literals, arithmetic and returns into IR.
- [ ] C17 backend that reads the IR, not the AST directly.
- [ ] GCC process invocation and diagnostic forwarding.
- [ ] End-to-end test: compile `return 42`, run the binary, check exit status `42`.

## Phase 4: Practical C-like systems language

- [ ] `let`, `var`, assignments, conditionals, loops.
- [ ] Structs, fixed arrays, module imports and visibility.
- [ ] Raw pointers, `unsafe`, C ABI declarations and function pointers.
- [ ] Slices, bounds checks and layout intrinsics.

## Phase 5: Enums, optionals and cleanup

- [ ] Tagged enums, pattern matching and exhaustiveness.
- [ ] Optional `?T`, `some`, `none`.
- [ ] `Result<T,E>` and `try` propagation.
- [ ] Tuples and multi-value returns.
- [ ] `defer`, `errdefer`, cleanup and branching edge cases.

## Phase 6: Advanced systems features

- [ ] Monomorphized generics, bounded constant evaluation.
- [ ] Atomics, volatile MMIO, freestanding and target ABIs.
- [ ] Portable and target-specific low-level facilities.
- [ ] Benchmark the C backend and evaluate a QBE, LLVM or native backend.
- [ ] Incremental compilation and editor tooling.

## Definition of done

A feature is done when it has a written rule, valid and invalid examples, unit and end-to-end tests, useful diagnostics, updated documentation, and no dependence on quirks of the C backend.
