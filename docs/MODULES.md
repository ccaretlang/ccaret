# Local modules and imports (bootstrap subset)

Modules are separate namespaces, not textual `#include` directives. The
bootstrap compiler resolves a local acyclic dependency graph, checks explicit
imports and `pub` visibility, then links declarations into **one verified
typed IR module**. The implementation is in `src/module/link.c`. This
local linker uses AST-guided name resolution and materializes an internal
canonical source unit before the existing semantic, verifier and C17 passes.
Its generated symbol prefix is reserved, not a source-language identifier.

## Example

Project files:

```text
project/
  app/main.caret
  lib/math.caret
  lib/shape.caret
```

`project/app/main.caret`:

```caret
module app.main;
import lib.math as math;
import lib.shape;

fn main() -> i32 {
    let p: shape.Point = shape.Point { x: 20, y: 22 };
    return math.sum(p.x, p.y);
}
```

`project/lib/math.caret`:

```caret
module lib.math;
pub fn sum(a: i32, b: i32) -> i32 { return a + b; }
fn internal() -> i32 { return 99; }
```

`project/lib/shape.caret`:

```caret
module lib.shape;
pub struct Point { pub x: i32, pub y: i32 }
```

```sh
./build/ccaret check project/app/main.caret
./build/ccaret emit-ir project/app/main.caret
./build/ccaret build project/app/main.caret -o build/example
./build/example   # exit status 42
```

## Implemented rules

- Modular files start with exactly one `module a.b;` header. An import
  appears after the module header and before any top-level declarations.
  Legacy single-file inputs **without a module header** remain supported.
- Each module occupies one `<root>/a/b.caret` file. The CLI entry path
  must match its declared module path; `root` is inferred from that path.
  Imports resolve relative to the same root, not the process working
  directory.
- `import lib.math;` makes the public names available under `math`;
  `import lib.math as numbers;` uses `numbers` instead. Access requires
  the alias, e.g. `math.sum(...)`, `shape.Point` or
  `shape.Point { x: 1, y: 2 }`. Imports do not flatten names into the
  caller's scope. Imports can themselves import other modules.
- `pub fn` and `pub struct` make declarations available to imports.
  Private declarations remain usable **within their own module**.
  For this bootstrap subset, an imported public struct must have only
  `pub` fields (including when constructing or passing it); support for
  opaque imported structs and selective public field access is pending.
  Public function parameters and results cannot expose private struct
  types. The linker rejects attempts to bypass visibility.
- Each module has its own top-level function and nominal struct namespace.
  Acyclic imports, duplicate aliases, conflicting local declarations,
  missing files, path/name mismatches and collisions with reserved internal
  compiler symbols are rejected.
- Cross-module function calls and struct values keep the same source
  semantics, left-to-right evaluation and verified typed IR as local
  declarations. Types remain nominal, even if distinct modules declare
  structurally identical records.
- Source diagnostics are mapped from the compiler's internal linked unit
  back to their originating file and location.

## Current limits and open design questions

The bootstrap linker compiles the module dependency graph as **one
compilation unit**. There are no independently cached object files,
external archives, module interface artifacts, package manifests,
configurable search roots, or incremental rebuilds. Imported modules
may not re-export other imports. Multiple source files per module and
cyclic graphs are unsupported. For now a public struct with private
fields cannot be imported, even as an opaque value. This is a deliberately
restricted milestone, not a final rule about field privacy.

Public C ABI exports, struct layout attributes, pointer-based opaque
interfaces, visibility across packages and stable generated symbol names
remain to be designed. An eventual multi-file IR linker can replace the
bootstrap canonical-source linker without changing the source-level
import grammar.

See `tests/test_modules.sh` for successful multi-file executables,
`-O2 -Werror` generated-C tests and rejection/diagnostic cases.
