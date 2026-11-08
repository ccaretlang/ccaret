#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$root/build/ccaret"
out="$root/build/modules"
good="$root/tests/fixtures/modules/good"
mkdir -p "$out"

run_exit() {
    expected="$1"
    binary_path="$2"
    set +e
    "$binary_path" >/dev/null 2>&1
    actual=$?
    set -e
    if [ "$actual" -ne "$expected" ]; then
        echo "modules: expected exit $expected, got $actual ($binary_path)" >&2
        exit 1
    fi
}

for name in main names scoping; do
    entry="$good/app/$name.caret"
    "$binary" check "$entry"
    "$binary" emit-ir "$entry" -o "$out/$name.ir"
    "$binary" emit-c "$entry" -o "$out/$name.c"
    "$binary" build "$entry" -o "$out/$name"
    run_exit 42 "$out/$name"
    gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
        "$out/$name.c" -o "$out/optimized_$name"
    run_exit 42 "$out/optimized_$name"
done

grep -q 'call @' "$out/main.ir"
grep -q 'struct cc_m' "$out/main.ir"
grep -q 'int main(void)' "$out/main.c"
grep -q 'cc_fn_' "$out/main.c"
test "$(grep -c '^fn @' "$out/names.ir")" -eq 4
if grep -q '^module ' "$out/main.c"; then
    echo "modules: generated C unexpectedly retained module directives" >&2
    exit 1
fi

# Isolated worktrees keep each failing project independent of other cases.
new_case() {
    name="$1"
    base="$out/invalid_$name"
    mkdir -p "$base/app" "$base/lib"
    cp "$good/lib/"*.caret "$base/lib/"
}

expect_failure() {
    name="$1"
    pattern="$2"
    entry="$out/invalid_$name/app/main.caret"
    if "$binary" check "$entry" >"$out/$name.stdout" 2>"$out/$name.err"; then
        echo "modules: invalid case $name unexpectedly passed" >&2
        exit 1
    fi
    if ! grep -F "$pattern" "$out/$name.err" >/dev/null; then
        echo "modules: missing diagnostic '$pattern' for $name" >&2
        cat "$out/$name.err" >&2
        exit 1
    fi
}

new_case private_function
cat > "$out/invalid_private_function/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return math.internal(); }
EOF
expect_failure private_function 'error: module: imported declaration is private'

new_case private_struct
cat > "$out/invalid_private_struct/app/main.caret" <<'EOF'
module app.main;
import lib.shape;
fn main() -> i32 { let h: shape.Hidden = shape.Hidden { n: 1 }; return 0; }
EOF
expect_failure private_struct 'error: module: imported declaration is private'

new_case private_field
cat > "$out/invalid_private_field/app/main.caret" <<'EOF'
module app.main;
import lib.shape;
fn main() -> i32 { let p = shape.Point { x: 1, y: 2 }; return p.x; }
EOF
cat > "$out/invalid_private_field/lib/shape.caret" <<'EOF'
module lib.shape;
pub struct Point { pub x: i32, y: i32 }
EOF
expect_failure private_field 'error: module: imported struct requires public fields'

new_case exposed_private_type
cat > "$out/invalid_exposed_private_type/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_exposed_private_type/lib/math.caret" <<'EOF'
module lib.math;
struct Hidden { value: i32 }
pub fn leak() -> Hidden { return Hidden { value: 1 }; }
EOF
expect_failure exposed_private_type 'error: module: public signature exposes a private struct type'

new_case missing_import
cat > "$out/invalid_missing_import/app/main.caret" <<'EOF'
module app.main;
import lib.missing;
fn main() -> i32 { return 42; }
EOF
expect_failure missing_import 'error: module: cannot load imported module source'

new_case name_mismatch
cat > "$out/invalid_name_mismatch/app/main.caret" <<'EOF'
module app.main;
import lib.math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_name_mismatch/lib/math.caret" <<'EOF'
module lib.other;
pub fn sum(a: i32, b: i32) -> i32 { return a + b; }
EOF
expect_failure name_mismatch 'error: module: imported module name mismatch'

new_case cycle
cat > "$out/invalid_cycle/app/main.caret" <<'EOF'
module app.main;
import lib.math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_cycle/lib/math.caret" <<'EOF'
module lib.math;
import app.main as app;
pub fn f() -> i32 { return 1; }
EOF
expect_failure cycle 'error: module: cyclic module import'

new_case duplicate_alias
cat > "$out/invalid_duplicate_alias/app/main.caret" <<'EOF'
module app.main;
import lib.math as utils;
import lib.shape as utils;
fn main() -> i32 { return 42; }
EOF
expect_failure duplicate_alias 'error: module: duplicate module import alias'

new_case alias_collision
cat > "$out/invalid_alias_collision/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn math() -> i32 { return 1; }
fn main() -> i32 { return 42; }
EOF
expect_failure alias_collision 'error: module: import alias conflicts with local declaration'

new_case duplicate_top_level
cat > "$out/invalid_duplicate_top_level/app/main.caret" <<'EOF'
module app.main;
fn duplicate() -> i32 { return 1; }
struct duplicate {}
fn main() -> i32 { return 42; }
EOF
expect_failure duplicate_top_level 'error: module: duplicate top-level module declaration'

new_case no_header
cat > "$out/invalid_no_header/app/main.caret" <<'EOF'
module app.main;
import lib.math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_no_header/lib/math.caret" <<'EOF'
pub fn value() -> i32 { return 42; }
EOF
expect_failure no_header 'error: module: imported file lacks module declaration'

new_case unknown_imported
cat > "$out/invalid_unknown_imported/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return math.unknown(); }
EOF
expect_failure unknown_imported 'error: module: unknown imported declaration'

new_case unqualified
cat > "$out/invalid_unqualified/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return sum(20, 22); }
EOF
expect_failure unqualified 'error: semantic: undefined function'

new_case semantic_dependency
cat > "$out/invalid_semantic_dependency/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return math.sum(20, 22); }
EOF
cat > "$out/invalid_semantic_dependency/lib/math.caret" <<'EOF'
module lib.math;
pub fn sum(a: i32, b: i32) -> i32 { return true; }
EOF
expect_failure semantic_dependency 'lib/math.caret:2:'
grep -q 'error: semantic: type mismatch' "$out/semantic_dependency.err"

new_case parser_dependency
cat > "$out/invalid_parser_dependency/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_parser_dependency/lib/math.caret" <<'EOF'
module lib.math;
pub fn broken() -> i32 { return ); }
EOF
expect_failure parser_dependency 'lib/math.caret:2:'
grep -q 'error: parser:' "$out/parser_dependency.err"

new_case qualified_type
cat > "$out/invalid_qualified_type/app/main.caret" <<'EOF'
module app.main;
import lib.shape;
fn main() -> i32 { let p: shape.Missing = 1; return 42; }
EOF
expect_failure qualified_type 'error: module: unknown imported declaration'

new_case import_after_declaration
cat > "$out/invalid_import_after_declaration/app/main.caret" <<'EOF'
module app.main;
fn main() -> i32 { return 42; }
import lib.math;
EOF
expect_failure import_after_declaration 'error: parser: module and imports must precede declarations'

new_case reserved_identifier
cat > "$out/invalid_reserved_identifier/app/main.caret" <<'EOF'
module app.main;
import lib.math as math;
fn main() -> i32 { return cc_m1_internal(); }
EOF
expect_failure reserved_identifier 'error: module: reserved internal module identifier prefix'

new_case builtin_struct
cat > "$out/invalid_builtin_struct/app/main.caret" <<'EOF'
module app.main;
struct i32 { value: i32 }
fn main() -> i32 { return 42; }
EOF
expect_failure builtin_struct 'error: module: struct name conflicts with builtin'

new_case entry_path_mismatch
cat > "$out/invalid_entry_path_mismatch/app/main.caret" <<'EOF'
module app.renamed;
fn main() -> i32 { return 42; }
EOF
expect_failure entry_path_mismatch 'error: module: entry filename must match module path'

new_case root_invalid_header
cat > "$out/invalid_root_invalid_header/app/main.caret" <<'EOF'
module app.main
fn main() -> i32 { return 42; }
EOF
expect_failure root_invalid_header 'error: parser: expected `;` after module path'

new_case duplicate_import_realias
cat > "$out/invalid_duplicate_import_realias/app/main.caret" <<'EOF'
module app.main;
import lib.math as m;
import lib.math as m;
fn main() -> i32 { return 42; }
EOF
expect_failure duplicate_import_realias 'error: module: duplicate module import alias'

new_case shadowed_function
cat > "$out/invalid_shadowed_function/app/main.caret" <<'EOF'
module app.main;
fn local() -> i32 { return 42; }
fn main() -> i32 { let local = 1; return local(); }
EOF
expect_failure shadowed_function 'error: semantic: local variable cannot be called as a function'

new_case private_signature_fields
cat > "$out/invalid_private_signature_fields/app/main.caret" <<'EOF'
module app.main;
import lib.math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_private_signature_fields/lib/math.caret" <<'EOF'
module lib.math;
pub struct Opaque { value: i32 }
pub fn get() -> Opaque { return Opaque { value: 7 }; }
EOF
expect_failure private_signature_fields 'error: module: public signature exposes nonexportable struct'

new_case different_nominal_types
cat > "$out/invalid_different_nominal_types/app/main.caret" <<'EOF'
module app.main;
import lib.alpha as a;
import lib.beta as b;
fn main() -> i32 {
    let first: a.Item = b.Item { n: 42 };
    return first.n;
}
EOF
cat > "$out/invalid_different_nominal_types/lib/alpha.caret" <<'EOF'
module lib.alpha;
pub struct Item { pub n: i32 }
EOF
cat > "$out/invalid_different_nominal_types/lib/beta.caret" <<'EOF'
module lib.beta;
pub struct Item { pub n: i32 }
EOF
expect_failure different_nominal_types 'error: semantic: struct literal type mismatch'

new_case lexer_dependency
cat > "$out/invalid_lexer_dependency/app/main.caret" <<'EOF'
module app.main;
import lib.math;
fn main() -> i32 { return 42; }
EOF
cat > "$out/invalid_lexer_dependency/lib/math.caret" <<'EOF'
module lib.math;
pub fn helper() -> i32 { return $; }
EOF
expect_failure lexer_dependency 'lib/math.caret:2:'
grep -q 'error: lexer:' "$out/lexer_dependency.err"

echo 'modules: end-to-end and rejection tests passed'
