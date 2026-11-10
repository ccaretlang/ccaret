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
volatile operations, pointer-to-slice conversion, FFI and `unsafe fn` are not
supported. Fixed-array-to-slice views use the separate `slice(local)` intrinsic.

The following is a *future design sketch*, not currently valid executable C^:

```caret
unsafe {
    let reg = ptr_from_int<*volatile u32>(0x40000000);
    *reg = 1;
}
```

Volatile access is not synchronization. It does not replace atomics or memory barriers where those are needed.

## Arrays and slices

`[N]T` holds N elements. `[]T` holds a pointer and a length and does not
own anything. The implemented bootstrap intrinsic `slice(array_local)`
creates a view of a **named local fixed array**, without copying or
allocating its elements. It yields `[]T` from a `var` array and
`[]const T` from a `let` array. Array temporaries are rejected.
`let view = slice(mutable_array)` allows indexed writes through the
view because the binding's mutability is distinct from element access.
Reads and writes use checked `i32` indices: negative or out-of-range
indices abort, including for zero-length slices. `view.len` is a
`usize` value; slice values can be passed, returned and nested in
other supported aggregates. `local_array[from..to]` (a named fixed-array
local) and `view[from..to]` (also on temporary slice expressions) produce
non-owning subviews. They check `0 <= from <= to <= len` at runtime
before pointer arithmetic, retaining element constness and allowing
valid empty ranges. The two `i32` endpoints are evaluated in source
order once. The exclusive upper bound `..` is required; open-ended
and inclusive ranges are not implemented here.

```caret
fn main() -> i32 {
    var values = [20, 0];
    let view: []i32 = values[0..2];
    view[1] += 22;
    return values[0] + values[1];
}
```

**Bounds checks do not establish lifetime safety.** Copying, passing or
returning a slice does not extend its array's lifetime. A slice returned
from a function owning its backing local array can dangle, and the
compiler currently has no escape/lifetime analysis or borrow checking.
The caller must keep backing storage alive and avoid invalidated aliases.
Subranges do not add a borrow checker, change backing-array lifetimes
or prevent aliasing through the original array and other views.
Mutable reborrowing rules, pointer-origin views, unchecked slice
operations and any slice ABI are not implemented.

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
