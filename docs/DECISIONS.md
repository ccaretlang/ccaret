# Design decision log

> This file keeps design discussions from looking like settled spec. Dates come from project conversations, not language releases.

## Agreed project direction

| ID | Decision | Status |
|---|---|---|
| D-001 | Name: **C^ / C Caret**; compiler: `ccaret`; repository: `ccaretlang/ccaret`. | Agreed |
| D-002 | Initial compiler implementation language: **C17**. | Agreed |
| D-003 | A backend-independent **IR exists from the first working version**. | Agreed |
| D-004 | Initial backend generates **C17** and invokes **GCC**. Other backends may follow. | Agreed |
| D-005 | No required GC, VM or runtime. Explicit memory management and low-level control come first. | Agreed |
| D-006 | C interoperability and systems programming at C's level are core goals. | Agreed |
| D-007 | Do **not** put a Rust-style borrow checker in the initial language. | Agreed |
| D-008 | Tuple returns, optional types, typed errors, matching and deferred cleanup are wanted features. | Agreed direction; syntax and semantics open |

## Proposals that still need approval

| ID | Proposal | To settle |
|---|---|---|
| P-001 | Keywords, operators, punctuation and literal rules in `LEXICAL.md`. | Freeze grammar conflicts and token behavior. |
| P-002 | `fn`, `let`, `var`, `module`, `pub` as core syntax. | Formal EBNF, reserved word policy. |
| P-003 | Signed arithmetic traps, unsigned wraps, explicit `+%`. | Exact overflow and cast rules, target lowering. |
| P-004 | `*T` is proposed non-null raw pointer, `?*T` nullable. | Initial typed C17 pointer operations implemented; non-null guarantee, aliasing, provenance, escape safety and reference safety still open. |
| P-005 | Ordinary structs and tuples have copy semantics. | Ownership and resource contracts. |
| P-006 | `?T`, `Result<T,E>`, `try`, `defer`, `errdefer`. | `?T` now supports `some`/`none`, `.has`, checked `.value` and exhaustive statement `match` with `some(pattern)` / `none` as executable bootstrap forms; final pattern syntax, enum matching, `Result` propagation and cleanup remain open. |
| P-007 | One file per module, acyclic imports for now. | Bootstrap subset implemented for local root-relative source graphs, aliased imports and `pub` checks; final package layout, search roots and separate compilation remain open. |
| P-008 | Typed non-SSA IR with explicit blocks. | Aggregate merges and ABI lowering. |
| P-009 | UTF-8 source with ASCII identifiers for now. | Future Unicode policy. |
| P-010 | Generated C17 plus optional target-specific extensions. | Target compatibility declarations. |
| P-011 | An explicit `slice(local_array)` built-in creates non-owning `[]T` / `[]const T` views in the bootstrap C17 pipeline. | Checked, exclusive `a[from..to]` subranges now implemented for named local arrays and slice expressions; pointer-origin slices, lifetime/alias rules and final intrinsic naming remain open; no implicit array-to-slice coercion. |

## Open questions before the lexer and parser freeze

1. Are all reserved words necessary (`volatile`, `some`, and so on), or should some be contextual identifiers?
2. Exactly which literal forms, escape rules, doc comments and Unicode forms are accepted?
3. How do generic `<...>` forms coexist with `<`, `>`, `>>` in expressions?
4. Beyond the explicit bootstrap `slice(local_array)` intrinsic, should implicit conversions exist? Should safe local references exist separately from raw pointers?
5. What are the exact numeric cast, pointer provenance, aliasing and signed right-shift rules?
6. Which forms of `match` patterns and `defer`/`errdefer` blocks are legal?
7. Does `Result<void,E>` use `.Ok()`? How is error mapping expressed?
8. How are package roots, module paths and the C ABI target configured?
9. Beyond implemented lexical `unsafe { ... }` for raw pointer dereferences, which additional unsafe operations and intrinsics require it?
10. Should the compiler keep separate high-level and low-level typed IRs?

## How decisions are made

For a new language rule, add the proposal and the reason here, work through the ambiguity with examples, update the lexical, grammar, type and IR docs together, and add conformance tests once the feature is implemented. Change a status to **Agreed** only after review. Change it to **Implemented** only when repository tests pass.
