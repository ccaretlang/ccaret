# Lexical rules (draft 0.1)

> Proposed token set. A token is not a type name or a library function.

## Input and positions

- Source is UTF-8. The first implementation accepts ASCII identifiers, `[A-Za-z_][A-Za-z0-9_]*`. Unicode identifiers are still open.
- Case sensitive. Whitespace separates tokens and is otherwise ignored. Semicolons are explicit.
- Positions are half-open byte offsets `[start, end)` plus a file ID. Line and column are computed only for diagnostics.
- Longest match wins: `>>=` before `>>`, `..=` before `..`, `...` before `..`, `->` before `-`.
- `//` starts a line comment. `/* ... */` is a block comment. Nested block comments are still open.
- `#` has no preprocessor meaning. A `#` outside a comment or string is a lexical error.

## Keywords (33 proposed)

| Group | Keywords |
|---|---|
| Functions and values | `fn` `return` `let` `var` `const` `static` |
| Names and declarations | `pub` `type` `struct` `union` `enum` |
| Flow | `if` `else` `match` `while` `for` `in` `loop` `break` `continue` |
| Modules and ABI | `module` `import` `as` `extern` |
| Unsafe and cleanup | `unsafe` `volatile` `defer` `errdefer` `try` |
| Literals and optionals | `true` `false` `some` `none` |

Names like `i32`, `usize`, `Result`, `Ok`, `Err`, `size_of` and `Allocator` are identifiers, not lexer keywords. Whether `volatile` should be a keyword or a contextual qualifier is still open.

## Operators

| Group | Tokens |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` `+%` `-%` `*%` |
| Comparisons | `==` `!=` `<` `<=` `>` `>=` |
| Boolean | `&&` `\|\|` `!` |
| Bitwise | `&` `\|` `^` `~` `<<` `>>` |
| Assignment | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` |

The parser tells unary `*`, `&` and `-` apart from their binary forms. There are no `++` or `--` operators, and assignments do not produce values.

## Punctuation

`(` `)` `{` `}` `[` `]` `,` `;` `:` `.` `->` `=>` `?` `..` `..=` `...` `@`

`.` is used for members and module names. `?` introduces optional types. `@` introduces compiler attributes. `...` is reserved for C varargs syntax.

## Literals

| Token category | Examples | Notes |
|---|---|---|
| Integer | `123`, `0xff`, `0b1010`, `0o755`, `1_000_000` | The sign is a separate unary operator |
| Floating | `3.14`, `1.5e-3` | Base 10 initially |
| String | `"hello"` | UTF-8 bytes, no implicit terminal NUL |
| C string | `c"hello"` | UTF-8 bytes plus terminal NUL |
| Byte | `'A'`, `'\n'` | Exactly one resulting byte |
| Boolean | `true`, `false` | Keywords |
| Optional absence | `none` | Keyword |

Planned escapes: `\n`, `\r`, `\t`, `\\`, `\"`, `\0`, `\xNN`, `\u{...}`. In byte strings, a Unicode scalar escape is encoded as UTF-8. In byte literals it must encode to exactly one byte.

## Token kinds

`Identifier`, `IntegerLiteral`, `FloatLiteral`, `StringLiteral`, `CStringLiteral`, `ByteLiteral`, `EOF`, `Invalid`, plus one kind for each keyword, operator and punctuation mark.

## Lexer rules

- Scan the source once. No heap allocation per token.
- A token stores a `kind` and a source span, not a copy of the text.
- The source buffer must stay alive while tokens or the AST refer to it.
- Invalid UTF-8, bad escapes, unterminated strings or comments and unknown characters produce local diagnostics.
- Number range and type overflow are usually semantic errors. Malformed numeric spelling is a lexical error.

## Tests to write

- Empty source, whitespace, Unix and Windows line endings, BOM handling, invalid UTF-8.
- Every keyword next to a similar identifier (`ifx`, `var2`).
- Overlapping operators and punctuation (`>>=`, `..=`, `...`, `->`).
- Every integer base and separator rule. Floats versus ranges (`1..10`, `1.5`).
- Line, block and nested comments, including unterminated ones.
- Strings, escapes, unterminated strings, byte literals and C strings.
- Byte spans and error recovery.

**Open:** Unicode identifiers, BOM handling, raw and multiline strings, documentation comments, hexadecimal floats, recovery from invalid numbers.
