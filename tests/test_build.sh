#!/bin/sh
set -eu
ulimit -c 0

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$root/build/ccaret"
out="$root/build"

run_exit() {
    expected="$1"
    program="$2"
    set +e
    "$program" > /dev/null 2>&1
    actual=$?
    set -e
    if [ "$actual" -ne "$expected" ]; then
        echo "build: $program exited $actual, expected $expected" >&2
        exit 1
    fi
}

build_run() {
    source="$1"
    expected="$2"
    name="$out/compiled_$source"
    "$binary" build "$root/tests/fixtures/build_$source.caret" -o "$name"
    test -f "$name.c"
    test -x "$name"
    run_exit "$expected" "$name"
}

build_run hello 42
build_run call 42
build_run factorial 120
build_run while 30
build_run nested 12
build_run shortcircuit 42
build_run boolean 42
build_run void 42

"$binary" emit-ir "$root/tests/fixtures/build_hello.caret" \
    -o "$out/hello.ir"
grep -q 'fn @0() -> i32' "$out/hello.ir"
grep -q 'const.i32 42' "$out/hello.ir"
grep -q 'ret %0' "$out/hello.ir"

"$binary" emit-ir "$root/tests/fixtures/build_shortcircuit.caret" \
    -o "$out/shortcircuit.ir"
grep -q 'condbr' "$out/shortcircuit.ir"
grep -q 'call @0' "$out/shortcircuit.ir"

"$binary" emit-c "$root/tests/fixtures/build_hello.caret" \
    -o "$out/emit_hello.c"
grep -q 'ccrt_checked_i32' "$out/emit_hello.c"
gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$out/emit_hello.c" -o "$out/emit_hello"
run_exit 42 "$out/emit_hello"

for case in hello call factorial while shortcircuit boolean; do
    gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
        "$out/compiled_$case.c" -o "$out/optimized_$case"
    case "$case" in
        factorial) expected=120 ;;
        while) expected=30 ;;
        *) expected=42 ;;
    esac
    run_exit "$expected" "$out/optimized_$case"
done

"$binary" build "$root/tests/fixtures/build_hello.caret" \
    -o "$out/binary with spaces"
run_exit 42 "$out/binary with spaces"

( cd "$out" && "$binary" build \
    "$root/tests/fixtures/build_hello.caret" -o "-leading-name" )
run_exit 42 "$out/-leading-name"

( cd "$out" && "$binary" build \
    "$root/tests/fixtures/build_hello.caret" -o "@literal-name" )
run_exit 42 "$out/@literal-name"

for case in overflow divzero div_overflow neg_overflow; do
    "$binary" build "$root/tests/fixtures/build_$case.caret" \
        -o "$out/trap_$case"
    set +e
    ( "$out/trap_$case" > /dev/null 2>&1 ) 2> /dev/null
    status=$?
    set -e
    if [ "$status" -ne 134 ]; then
        echo "build: $case must abort (134), got $status" >&2
        exit 1
    fi
done

gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$out/trap_overflow.c" -o "$out/optimized_overflow"
set +e
( "$out/optimized_overflow" > /dev/null 2>&1 ) 2> /dev/null
optimized_status=$?
set -e
if [ "$optimized_status" -ne 134 ]; then
    echo "build: optimized overflow must abort (134), got $optimized_status" >&2
    exit 1
fi

if "$binary" build "$root/tests/fixtures/check_valid.caret" \
    -o "$out/unsupported" > /dev/null 2> "$out/unsupported.err"; then
    echo "build: tuple types must fail at IR lowering" >&2
    exit 1
fi
grep -q 'error: IR: IR backend currently supports only' "$out/unsupported.err"

if "$binary" build "$root/tests/fixtures/build_invalid_main.caret" \
    -o "$out/invalid_main" > /dev/null 2> "$out/invalid_main.err"; then
    echo "build: invalid main signature must fail" >&2
    exit 1
fi
grep -q 'build requires fn main() -> i32' "$out/invalid_main.err"

if "$binary" build "$root/tests/fixtures/build_hello.caret" \
    > /dev/null 2>&1; then
    echo "build: expected missing output argument to fail" >&2
    exit 1
fi

echo "build: end-to-end tests passed"
