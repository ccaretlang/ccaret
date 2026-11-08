# Memory, pointers and cleanup (draft 0.1)

> C^ does not promise Rust-style memory safety. Ownership is manual and depends on API contracts.

## Design

- No required garbage collector, heap allocator, finalizer, exception unwinder or runtime.
- Local variables, arrays and structs use stack and value semantics where that makes sense.
- Dynamic memory goes through explicit allocator APIs. No allocator is installed implicitly.
- Copying a struct that holds a pointer copies the address, not the referent. APIs have to document who owns an allocation and who frees it.

## Raw pointers and unsafe

The executable bootstrap subset implements `*T`, `*const T` and optional
pointer values `?*T`. `*volatile T` remains a proposal, not executable code.

A non-null raw pointer can still be dangling, out of bounds, misaligned or invalid. `*const T` stops writes through that pointer, but it does not prove exclusive ownership and does not prevent writes through another pointer.

Currently `&local` takes the address of a named local binding (parentheses
are allowed): `&var` yields `*T`, while `&let` yields `*const T`.
Address-taking itself is allowed in safe code. Pointer copying, passing and
returning are allowed; dereference reads and writes (`*p`, `*p = value`,
`*p += value` for `i32`) require a lexical `unsafe { ... }` statement.
`*const T` prevents stores *through that pointer*; `let p: *T` prevents
rebinding `p` but not writing the mutable referent. Nested pointer types,
structs, arrays and optional pointer values are supported where their
component types are supported by the C17 backend.

```caret
fn main() -> i32 {
    var number = 20;
    let ptr: *i32 = &number;
    unsafe { *ptr += 22; }
    return number;
}
```

**This is not memory safety:** `unsafe` only marks a lexically authorized
operation; neither semantic analysis nor the IR verifier proves non-nullness,
pointer provenance, ownership, aliasing, alignment or lifetime. Pointer
escape can still produce dangling pointers (including returning `&local`).
The programmer must ensure each dereference is valid. Address-of arbitrary
expressions, pointer arithmetic, integer-to-pointer casts, allocation,
volatile operations, slice conversion, FFI and `unsafe fn` are not supported.

The following is a *future design sketch*, not currently valid executable C^:

```caret
unsafe {
    let reg = ptr_from_int<*volatile u32>(0x40000000);
    *reg = 1;
}
```

Volatile access is not synchronization. It does not replace atomics or memory barriers where those are needed.

## Arrays and slices

`[N]T` holds N elements. `[]T` holds a pointer and a length and does not own anything. Creating a slice does not allocate or copy elements. Normal slice indexing checks bounds, and the check can be removed after a proof. Unchecked access is an explicit unsafe operation.

Without borrow checking, the caller has to keep the underlying allocation alive.

## `defer` and `errdefer`

- A `defer` belongs to its lexical scope and runs on ordinary exit from that scope, in reverse registration order.
- `return`, `break`, `continue` and a `try` that leaves the scope run the scope's defers.
- The `return` expression is evaluated before cleanup starts.
- `errdefer` runs when the scope exits because an error value is propagated or returned. It does not run on a successful return.
- A fatal trap, process abort, signal or foreign non-local jump does not guarantee cleanup.

```caret
fn load(a: Allocator) -> Result<[]u8, LoadError> {
    let data = try a.alloc(u8, 1024);  // API not final
    errdefer a.free(data);
    return .Ok(data);
}
```

The library API and method syntax are not final.

## Unresolved safety rules

- Whether there should be a separate safe reference type, distinct from raw pointers.
- Pointer aliasing and provenance rules, and what they mean for optimization.
- Escape analysis for obviously dangling views, and interfaces for uninitialized storage.
- API conventions for ownership transfer, allocator errors and fallible cleanup in `defer`.
- Interaction with C callbacks, `setjmp`/`longjmp`, threads and interrupts.
