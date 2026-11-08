# Grammar and precedence (draft 0.1)

> A sketch, not full EBNF. It exists to pin down the shape before parser work starts.

## Top level

```ebnf
source_file   = module_decl, import_decl*, declaration*, EOF ;
module_decl   = "module", path, ";" ;
import_decl   = "import", path, ("as", IDENTIFIER)?, ";" ;
path          = IDENTIFIER, (".", IDENTIFIER)* ;
declaration   = attribute*, "pub"?, (function_decl | struct_decl |
                union_decl | enum_decl | type_decl | const_decl |
                static_decl | extern_decl) ;
function_decl = "unsafe"?, "fn", IDENTIFIER, generic_params?,
                "(", parameters?, ")", ("->", type)?, block ;
type_decl     = "type", IDENTIFIER, "=", type, ";" ;
block         = "{", statement*, expression?, "}" ;
```

Statements, expressions, patterns, types, generics and attributes need a full grammar before the parser API is frozen.

## Statements

- `let x: T = expr;` and `var x: T = expr;`, with type inference where it is unambiguous.
- `expr = expr;`, compound assignments, expression statements.
- `return expr;`, `return;`, `break;`, `continue;`.
- `if`, `while`, `for name in range`, `loop`, `match`, nested blocks.
- `defer expr;`, `errdefer expr;` and possibly block forms. The exact forms are open.

## Expressions

- Names, literals, grouping, tuples `(a,b)`, calls `f(x)`, field access `p.x`, tuple member `p.0`, indexing `a[i]`, slicing `a[from..to]`, unary and binary operators, casts `as`, block expressions, `if` and `match` expressions.
- A block can end with an expression without `;`, and that value becomes the block value.
- An `if` used as an expression needs an `else`, and the branches must have compatible types.
- Assignment is a statement and never produces a value.

## Tuples and destructuring

Parentheses without a comma group one expression or type: `(x)` or `(T)`.
An empty pair `()` is a zero-element tuple; a comma distinguishes a
one-element tuple `(x,)` from grouping. Tuples with two or more elements
allow an optional trailing comma, e.g. `(x, y,)`. The same rules apply to
tuple expressions and tuple types, including nested forms.

The binding pattern of a `let` or `var` declaration can be a name or a
tuple of nested binding patterns: `let (x, (y, z)) = value;`. A type
annotation after the pattern describes the entire bound value:
`let (a, b): (i32, i32) = pair;`. Parentheses without a comma group a
single pattern, just as with expressions. Tuple member access `pair.0`
remains an ordinary postfix expression; consecutive accesses such as
`pair.1.0` are lexed as two integer member indices rather than as a
floating-point literal. Ordinary floats such as `1.5` are unchanged.

These are syntax rules; matching tuple arity and component types, checking
binding names and lowering tuple values are responsibilities of later phases.

## Operator precedence (highest to lowest)

| Level | Operators | Associativity |
|---|---|---|
| 1 | Call `()`, index `[]`, member `.` | Left |
| 2 | Unary `!`, `~`, `-`, `&`, `*`, `try` | Right |
| 3 | Cast `as` | Left |
| 4 | `*`, `/`, `%`, `*%` | Left |
| 5 | `+`, `-`, `+%`, `-%` | Left |
| 6 | `<<`, `>>` | Left |
| 7 | `<`, `<=`, `>`, `>=` | Non-chainable |
| 8 | `==`, `!=` | Non-chainable |
| 9 | Bitwise `&` | Left |
| 10 | Bitwise `^` | Left |
| 11 | Bitwise `\|` | Left |
| 12 | `&&` | Left |
| 13 | `\|\|` | Left |
| 14 | `..`, `..=` | Non-chainable |

Parentheses override precedence. The precedence of `try` and ranges needs targeted tests before it is final. When `>>` closes two generic argument lists, the parser has to split the token.

## Example

```caret
module app.main;

fn smaller(a: i32, b: i32) -> i32 {
    return if a < b { a } else { b };
}

pub fn main() -> i32 {
    let (x, y) = (10, 20);
    return smaller(x, y);
}
```

## Parser architecture

Recursive descent for declarations and statements, Pratt parsing for expressions. Separate parsers for types and patterns. The AST keeps source spans and is allocated in an arena.

**Open:** complete pattern grammar, `unsafe` blocks, method and lambda syntax (neither exists yet), generic argument disambiguation, `defer` grammar, recovery and diagnostics for incomplete files.
