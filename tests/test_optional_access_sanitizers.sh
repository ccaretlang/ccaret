#!/bin/sh
# Optional focused ASan/UBSan regression. Run after make check.
set -eu
ulimit -c 0

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out="$root/build/sanitize_optional_access"
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
        echo "optional sanitizers: $executable exited $actual, expected $expected" >&2
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

for case in optional_access_basic optional_access_none \
    optional_access_nested optional_access_struct optional_access_array \
    optional_access_slice optional_access_pointer optional_access_call \
    optional_access_tuple optional_access_bool optional_access_flow \
    optional_access_field_collision optional_access_shortcircuit; do
    "$out/ccaret" emit-c "$root/tests/fixtures/build_$case.caret" \
        -o "$out/$case.c"
    gcc $san_flags "$out/$case.c" -o "$out/$case"
    run_exit 42 "$out/$case"
done

for case in optional_access_none_trap optional_access_nested_trap \
    optional_access_call_trap; do
    "$out/ccaret" emit-c "$root/tests/fixtures/build_$case.caret" \
        -o "$out/$case.c"
    gcc $san_flags "$out/$case.c" -o "$out/$case"
    run_exit 134 "$out/$case"
done

for case in optional_access_bad_has optional_access_bad_value \
    optional_access_bad_member optional_access_bad_type \
    optional_access_none_infer; do
    if "$out/ccaret" check "$root/tests/fixtures/build_$case.caret" \
        >/dev/null 2>&1; then
        echo "optional sanitizers: invalid $case unexpectedly passed" >&2
        exit 1
    fi
done

gcc -Isrc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
    $sources -o "$out/optimized_ccaret"
"$out/optimized_ccaret" build \
    "$root/tests/fixtures/build_optional_access_flow.caret" \
    -o "$out/optimized_compiler_output"
run_exit 42 "$out/optimized_compiler_output"

echo "optional access sanitizers: all tests passed"
