#!/bin/sh
# Exhaustive optional match: compiler/unit tests and emitted C17 ASan/UBSan.
set -eu
ulimit -c 0

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/build/sanitize_optional_match"
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
        echo "optional match sanitizer: $executable exited $actual, expected $expected" >&2
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

for case in optional_match_some optional_match_none \
    optional_match_reversed optional_match_tuple optional_match_nested \
    optional_match_bool optional_match_array optional_match_struct \
    optional_match_slice optional_match_pointer optional_match_flow \
    optional_match_call optional_match_call_none optional_match_shadow \
    optional_match_direct \
    optional_match_unit optional_match_inner_none optional_match_loop; do
    "$out/ccaret" emit-c "$root/tests/fixtures/build_$case.caret" \
        -o "$out/$case.c"
    gcc $san_flags "$out/$case.c" -o "$out/$case"
    run_exit 42 "$out/$case"
done

# Generate and run a multi-module match under sanitizers as well.
mkdir -p "$out/module/app" "$out/module/lib"
cat > "$out/module/app/main.caret" <<'EOF'
module app.main;
import lib.option as opt;
import lib.math as math;
fn main() -> i32 {
    match opt.produce() {
        some(opt) => { return opt + math.delta(); },
        none => { return 0; },
    }
}
EOF
cat > "$out/module/lib/option.caret" <<'EOF'
module lib.option;
pub fn produce() -> ?i32 { return some(20); }
EOF
cat > "$out/module/lib/math.caret" <<'EOF'
module lib.math;
pub fn delta() -> i32 { return 22; }
EOF
"$out/ccaret" emit-c "$out/module/app/main.caret" -o "$out/module/linked.c"
gcc $san_flags "$out/module/linked.c" -o "$out/module/linked"
run_exit 42 "$out/module/linked"

for case in optional_match_missing_some optional_match_missing_none \
    optional_match_duplicate_some optional_match_duplicate_none \
    optional_match_nonoptional optional_match_leak \
    optional_match_immutable optional_match_wrong_tuple \
    optional_match_partial_return optional_match_invalid_arm \
    optional_match_missing_arrow; do
    if "$out/ccaret" check "$root/tests/fixtures/build_$case.caret" \
        >/dev/null 2>&1; then
        echo "optional match sanitizer: invalid $case unexpectedly passed" >&2
        exit 1
    fi
done

gcc -Isrc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
    $sources -o "$out/optimized_ccaret"
"$out/optimized_ccaret" build \
    "$root/tests/fixtures/build_optional_match_call.caret" \
    -o "$out/optimized_compiler_output"
run_exit 42 "$out/optimized_compiler_output"

echo "optional match sanitizers: all tests passed"
