# Optionals and error handling (draft 0.1)

## Errors are values

Recoverable errors are ordinary values. C^ has no exception unwinding. A fatal `trap` is separate from a recoverable error and can terminate the process without running cleanup.

## `?T`: optional value

- `some(value)` holds a `T`. `none` means there is no value.
- In the executable bootstrap, `optional.has` reports presence as
  `bool`, while `optional.value` extracts `T` and **aborts** if
  the optional is `none`. Neither uses an implicit conversion.
  This checked, trap-on-absence access is **not** a recoverable error
  handler; optional chaining is still planned.
  The member names are bootstrap spellings, not frozen syntax.
- `?*T` can use a null representation when the target defines one. A general `?T` may need a tag.

```caret
fn find(bytes: []const u8, wanted: u8) -> ?usize {
    for i in 0..bytes.len {
        if bytes[i] == wanted { return some(i); }
    }
    return none;
}
```

For a currently executable `i32` optional:

```caret
fn main() -> i32 {
    let value: ?i32 = some(42);
    if value.has { return value.value; }
    return 0;
}
```

An executable, exhaustive optional `match` statement can instead bind
the payload without inspecting `.has` or manually calling `.value`:

```caret
fn extract(item: ?i32) -> i32 {
    match item {
        some(value) => { return value; },
        none => { return 0; },
    }
}
```

Exactly one arm of each variant is required. A duplicate or missing arm,
non-optional scrutinee, incompatible tuple destructuring or use of a
`some` binder outside its arm is a compile-time error. The scrutinee
evaluates once; there is no payload read on the `none` path.
Tagged enum statements are also implemented; generic `Result`,
`try` and `match` expressions are not.

## User-defined tagged enums

The executable subset allows nominal enums with unit and typed-payload
variants and exhaustive statement matching:

```caret
enum ParseError { Empty, Invalid(i32) }
enum Outcome { Success(i32), Failure(ParseError) }

fn result() -> Outcome { return Outcome.Success(42); }

fn main() -> i32 {
    match result() {
        Outcome.Success(n) => { return n; },
        Outcome.Failure(error) => {
            match error {
                ParseError.Empty => { return 0; },
                ParseError.Invalid(code) => { return code; },
            }
        },
    }
}
```

Each variant must be covered once, payloads are immutable and arm-local,
and the scrutinee evaluates once. The C17 implementation checks the
discriminant before reading any payload. Tagged user enums do not yet
provide automatic error propagation, must-use checking, a stable C ABI,
or the generic `Result<T,E>` intrinsic.

## `Result<T,E>`: success or failure

Conceptually:

```caret
enum Result<T,E> { Ok(T), Err(E), }
```

The compiler may implement it as an intrinsic before user-defined generics exist. `Result` and `?T` are not interchangeable.

```caret
enum ParseError { Empty, InvalidDigit, Overflow, }

fn parse_number(input: []const u8) -> Result<u32, ParseError> {
    if input.len == 0 { return .Err(.Empty); }
    // parsing omitted
    return .Ok(123);
}

fn double_number(input: []const u8) -> Result<u32, ParseError> {
    let n = try parse_number(input);
    return .Ok(n +% n);
}
```

`try expression` extracts `Ok(T)` or returns `Err(E)` from the enclosing function, running the matching cleanup paths. For now, the enclosing function's error type must match. Implicit error conversion is not planned.

## `match` and ignored results

```caret
let value = match parse_number(data) {
    .Ok(n) => n,
    .Err(_) => 0,
};
```

A bootstrap `match` on a nominal enum must cover each variant exactly
once. The planned `Result` syntax may eventually allow explicit wildcard
arms; neither wildcard matching nor ignored-result checking is implemented.
Exact must-use enforcement and the `_ = value` syntax remain open.

## Open questions

- Should `try foo() else .OtherError` exist? If so, how does the mapping lower to IR?
- Constructor syntax for `Result<void,E>`.
- Must the error type be an enum? Are result values always trivially copyable?
- Fallible cleanup inside `defer` and `errdefer`.
- How exhaustiveness should work with future generic tagged unions.
