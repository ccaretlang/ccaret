# Modules and name resolution (draft 0.1)

## Why modules

Modules are semantic units, not text inclusion. There is no `#include`. The public interface comes from exported declarations, so declarations do not have to be repeated in headers.

## Proposed syntax

```caret
module app.main;

import core.mem;
import core.io as io;

pub fn main() -> i32 {
    return 0;
}
```

- Every source file starts with exactly one `module path;` declaration.
- Imports come after the module declaration and before other declarations.
- `import core.mem;` exposes the public symbols under `mem`, not in the global namespace.
- `import core.io as io;` gives an explicit alias.
- `pub` exports a declaration. Struct fields are private unless marked `pub`.
- The initial module graph is acyclic.

## Source paths

| Module | Relative source path |
|---|---|
| `app.main` | `app/main.caret` |
| `core.mem` | `core/mem.caret` |

Search roots, standard library lookup and package manifests are controlled by the driver and build configuration. The source-to-file mapping is a proposal and may change when package support arrives.

## Compilation

Resolve the dependency graph first, then build symbol tables and type check each module. Interface summaries would help incremental builds, but the first version does not need them. Generated C names are mangled, except for explicit `extern "C"` symbols.

## Open questions

Multiple source files per module; package metadata; public re-exports; namespace conflicts; static initializers; build dependency configuration; importing external C headers (likely a separate Clang-based tool).
