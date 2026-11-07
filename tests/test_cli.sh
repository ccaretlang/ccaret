#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="$root/build/ccaret"
fixture="$root/tests/fixtures/tokens_sample.caret"
expected="$root/tests/fixtures/tokens_sample.expected"
output="$root/build/tokens_sample.out"

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

echo "cli: all tests passed"
