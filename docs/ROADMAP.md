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
- [x] Accurate errors for bad escapes, invalid UTF-8 and unterminated constructs.
- [x] Unit tests for every token kind and ambiguity.
- [x] `ccaret tokens path.caret` command.

## Phase 2: Parser and semantic checks

- [ ] Parse function declarations, parameter lists, types, returns and arithmetic.
- [ ] AST allocated in an arena with source spans.
- [ ] Pratt expression parser with tested precedence.
- [ ] Name resolution, primitive type checking and definite assignment.
- [x] `ccaret check` command (first semantic subset; see SEMANTIC.md).

## Phase 3: IR and executable pipeline

- [x] Define typed IR and verifier for scalars, tuples and optionals.
- [x] Lower simple functions, calls, literals, arithmetic and returns into IR.
- [x] C17 backend that reads the verified IR, not the AST directly.
- [x] GCC process invocation and diagnostic forwarding for hosted builds.
- [x] End-to-end test: compile `return 42`, run the binary, check exit status `42`.
- [x] Canonical composite type IDs, aggregate IR instructions and verified
      C17 layouts; compile and execute the tuple-returning `divmod` example.

## Phase 4: Practical C-like systems language

- [x] `let`, `var`, plain assignments, conditionals and loops for the
      supported IR subset.
- [x] Exclusive/inclusive `i32` `for` ranges, scoped iteration variables,
      `break`/`continue` and boundary-safe stepping.
- [x] Checked `i32` compound arithmetic, bitwise expressions/updates and
      explicitly wrapping `i32` arithmetic (signed shifts remain pending).
- [x] Fixed arrays, literals, by-value calling/returning, checked
      `i32` indexing and mutable-local element updates.
- [x] Nominal struct declarations, named-field literals, by-value
      calls/returns, field reads and direct mutable-local field writes.
- [x] Local one-file modules, acyclic import graph, aliases, explicit
      `pub` function/struct visibility and cross-module calls/values,
      linked through verified IR and C17.
- [ ] Full field visibility, opaque exported structs, multi-file module
      units, package/search configuration, stable module ABI and
      representation attributes.
- [x] Raw pointer subset: `*T` / `*const T`, `&local`, lexical `unsafe`
      reads/writes, typed verified IR and C17 executable pipeline.
- [ ] Pointer arithmetic, cast/provenance rules, C ABI declarations,
      function pointers, lifetime/escape diagnostics and allocator/FFI support.
- [x] Non-owning slice subset: `[]T` / `[]const T` via explicit
      `slice(local_array)`, checked indexing/writes and `.len`,
      independent verified IR and executable C17.
- [x] Checked exclusive slice subranges `a[from..to]` for named fixed
      arrays and existing slices, with element constness preserved,
      independently verified IR and C17 runtime bounds checks.
- [ ] Pointer-origin views, lifetime/alias checks, slice reborrowing
      rules, layout intrinsics and full target ABI declarations.

## Phase 5: Enums, optionals and cleanup

- [ ] Tagged enums, pattern matching and exhaustiveness.
- [x] Optional `?T`, `some`, `none` construction and value propagation
      plus `.has` inspection and checked `.value` extraction through
      independently verified IR and C17 (full pattern matching pending).
- [ ] `Result<T,E>` and `try` propagation.
- [x] Tuples, nested values, destructuring and multi-value returns.
- [ ] `defer`, `errdefer`, cleanup and branching edge cases.

## Phase 6: Advanced systems features

- [ ] Monomorphized generics, bounded constant evaluation.
- [ ] Atomics, volatile MMIO, freestanding and target ABIs.
- [ ] Portable and target-specific low-level facilities.
- [ ] Benchmark the C backend and evaluate a QBE, LLVM or native backend.
- [ ] Incremental compilation and editor tooling.

## Definition of done

A feature is done when it has a written rule, valid and invalid examples, unit and end-to-end tests, useful diagnostics, updated documentation, and no dependence on quirks of the C backend.
