# ABI, data layout and C interop (draft 0.1)

## Requirement

A C^ program should be able to do the same low-level work as C: OS services, kernel and embedded interfaces, raw memory, FFI, memory mapped hardware, freestanding entry points. That is the goal. The bootstrap compiler does not support all of it yet.

## Proposed C ABI

```caret
@repr(c)
pub struct CPoint {
    pub x: i32,
    pub y: i32,
}

extern "C" {
    fn puts(text: *const c_char) -> c_int;
}
```

ABI compatibility needs target rules for scalar widths, alignment, parameter passing, function pointers, varargs, struct layout and name mangling. C^ tuples, tagged enums, `?T` and `Result<T,E>` are not automatically C-compatible.

## Layout and intrinsics (planned)

- `@repr(c)` for structs and compatible unions.
- `@repr(u32)` for enum tags (proposed), plus `@align(N)`, `@packed`, `@section("...")` and `@link_name("...")`.
- `size_of<T>()`, `align_of<T>()`, `offset_of<T>(field)`, `bitcast<T>(value)` and checked or truncating conversion intrinsics.
- `@packed` fields can be unaligned. The backend must not generate invalid dereferences for them.
- Explicit raw pointer conversion, volatile access, atomics and target-specific assembly.

## FFI boundaries

Calling an external function whose preconditions cannot be checked is `unsafe`. The compiler has to support direct linking with object files and libraries, and eventually C varargs and callbacks. `setjmp`/`longjmp`, ABI-specific aggregates and platform extensions need special handling.

## C17 backend caveats

- C has its own unspecified and undefined behavior, so C^ argument evaluation order, arithmetic overflow, bounds checks, pointer casts, aggregate returns and cleanup semantics have to be lowered explicitly.
- The backend may use GCC and Clang extensions only when the target configuration declares them.
- Portable C17 cannot express arbitrary assembly, register allocation or every vendor calling convention. Those need extensions or a later native backend.
- LLVM, QBE or custom backends may lower the same typed IR differently, but they must preserve C^ semantics.

## Verification checklist

Target triples; 32 and 64 bit systems; endianness; symbol visibility; packed data; callbacks and varargs; calling convention tests against compiled C; freestanding boot targets.
