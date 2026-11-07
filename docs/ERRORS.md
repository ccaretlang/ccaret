# Optionals and error handling (draft 0.1)

## Errors are values

Recoverable errors are ordinary values. C^ has no exception unwinding. A fatal `trap` is separate from a recoverable error and can terminate the process without running cleanup.

## `?T`: optional value

- `some(value)` holds a `T`. `none` means there is no value.
- Unwrapping is explicit, through `match` or another safe operator once one is specified.
- `?*T` can use a null representation when the target defines one. A general `?T` may need a tag.

```caret
fn find(bytes: []const u8, wanted: u8) -> ?usize {
    for i in 0..bytes.len {
        if bytes[i] == wanted { return some(i); }
    }
    return none;
}
```

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

A match on an enum or `Result` must cover every variant or use an explicit wildcard. Ignoring a result without an explicit discard should produce a diagnostic. Exact must-use enforcement and the `_ = value` syntax are still open.

## Open questions

- Should `try foo() else .OtherError` exist? If so, how does the mapping lower to IR?
- Constructor syntax for `Result<void,E>`.
- Must the error type be an enum? Are result values always trivially copyable?
- Fallible cleanup inside `defer` and `errdefer`.
- How exhaustiveness should work with future generic tagged unions.
