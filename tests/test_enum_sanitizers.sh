#!/bin/sh
# Tagged enum constructors and exhaustive matches: compiler, unit, C17 runtime.
set -eu
ulimit -c 0

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/build/sanitize_enum"
mkdir -p "$out"
cd "$root"

san_flags="-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie"
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1

run_exit() {
    expected="$1"
    executable="$2"
    set +e
    ( "$executable" >/dev/null 2>&1 ) 2>/dev/null
    actual=$?
    set -e
    if [ "$actual" -ne "$expected" ]; then
        echo "enum sanitizer: $executable exited $actual, expected $expected" >&2
        exit 1
    fi
}

gcc -Isrc $san_flags tests/test_parser.c src/parser/parser.c \
    src/lexer/lexer.c src/ast/arena.c src/source/source.c \
    -o "$out/test_parser"
gcc -Isrc $san_flags tests/test_semantic.c src/semantic/semantic.c \
    src/parser/parser.c src/lexer/lexer.c src/ast/arena.c src/source/source.c \
    -o "$out/test_semantic"
gcc -Isrc $san_flags tests/test_ir.c src/ir/lower.c src/ir/verify.c \
    src/ir/print.c src/backend/c/emitter.c src/semantic/semantic.c \
    src/parser/parser.c src/lexer/lexer.c src/ast/arena.c src/source/source.c \
    -o "$out/test_ir"
"$out/test_parser"
"$out/test_semantic"
"$out/test_ir"

sources="src/main.c src/source/source.c src/lexer/lexer.c src/parser/parser.c
src/ast/arena.c src/semantic/semantic.c src/ir/lower.c src/ir/verify.c
src/ir/print.c src/backend/c/emitter.c src/module/link.c"
gcc -Isrc $san_flags $sources -o "$out/ccaret"

for case in enum_basic enum_unit enum_reversed enum_tuple enum_struct \
    enum_array enum_optional enum_nested enum_copy enum_call \
    enum_call_unit enum_flow enum_shadow enum_bool enum_pointer \
    enum_slice enum_unit_payload enum_forward; do
    "$out/ccaret" emit-c "$root/tests/fixtures/build_$case.caret" \
        -o "$out/$case.c"
    gcc $san_flags "$out/$case.c" -o "$out/$case"
    run_exit 42 "$out/$case"
done

# Import-qualified enum constructors, tuple binders, calls and public ABI.
mkdir -p "$out/module/app" "$out/module/lib"
cat > "$out/module/app/main.caret" <<'EOF'
module app.main;
import lib.events as ev;
import lib.math as math;
fn main() -> i32 {
    match ev.produce() {
        ev.Event.Value((ev, v)) => { return ev + v + math.delta(); },
        ev.Event.Empty => { return 0; },
    }
}
EOF
cat > "$out/module/lib/events.caret" <<'EOF'
module lib.events;
pub enum Event { Value((i32, i32)), Empty }
pub fn produce() -> Event { return Event.Value((20, 0)); }
EOF
cat > "$out/module/lib/math.caret" <<'EOF'
module lib.math;
pub fn delta() -> i32 { return 22; }
EOF
"$out/ccaret" emit-c "$out/module/app/main.caret" -o "$out/module/linked.c"
gcc $san_flags "$out/module/linked.c" -o "$out/module/linked"
run_exit 42 "$out/module/linked"

for case in enum_missing_arm enum_duplicate_arm enum_unknown_arm \
    enum_wrong_owner enum_wrong_payload enum_unit_argument \
    enum_missing_argument enum_extra_argument enum_leak enum_immutable \
    enum_wrong_tuple enum_empty enum_duplicate_variant enum_recursive \
    enum_void_payload enum_nominal enum_nonenum enum_partial_return \
    enum_payload_pattern enum_unit_pattern enum_shadow_type; do
    if "$out/ccaret" check "$root/tests/fixtures/build_$case.caret" \
        >/dev/null 2>&1; then
        echo "enum sanitizer: invalid $case unexpectedly passed" >&2
        exit 1
    fi
done

gcc -Isrc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
    $sources -o "$out/optimized_ccaret"
"$out/optimized_ccaret" build \
    "$root/tests/fixtures/build_enum_call.caret" \
    -o "$out/optimized_compiler_output"
run_exit 42 "$out/optimized_compiler_output"

echo "enum sanitizers: all tests passed"
