#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$root/build/ccaret"
fixture="$root/tests/fixtures/tokens_sample.caret"
expected="$root/tests/fixtures/tokens_sample.expected"
output="$root/build/tokens_sample.out"
invalid="$root/tests/fixtures/tokens_invalid.caret"
error_output="$root/build/tokens_invalid.err"

"$binary" tokens "$fixture" > "$output"
diff -u "$expected" "$output"

if "$binary" tokens > /dev/null 2>&1; then
    echo "cli: expected missing path to fail" >&2
    exit 1
fi

if "$binary" tokens "$root/tests/fixtures/missing.caret" > /dev/null 2>&1; then
    echo "cli: expected missing file to fail" >&2
    exit 1
fi

if "$binary" tokens "$invalid" > /dev/null 2> "$error_output"; then
    echo "cli: expected lexical error to fail" >&2
    exit 1
fi

grep -q "tokens_invalid.caret:1:1: error: unterminated string literal" \
    "$error_output"

echo "cli: all tests passed"
