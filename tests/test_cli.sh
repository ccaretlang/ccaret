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

"$binary" check "$root/tests/fixtures/check_valid.caret" > /dev/null

if "$binary" check > /dev/null 2>&1; then
    echo "cli: expected missing check path to fail" >&2
    exit 1
fi

if "$binary" check "$root/tests/fixtures/missing.caret" > /dev/null 2>&1; then
    echo "cli: expected missing source file to fail" >&2
    exit 1
fi

if "$binary" check "$root/tests/fixtures/check_invalid_type.caret" \
    > /dev/null 2> "$root/build/check_invalid_type.err"; then
    echo "cli: expected semantic type error" >&2
    exit 1
fi
grep -q "check_invalid_type.caret:2:12: error: semantic: type mismatch" \
    "$root/build/check_invalid_type.err"

if "$binary" check "$root/tests/fixtures/check_invalid_parser.caret" \
    > /dev/null 2> "$root/build/check_invalid_parser.err"; then
    echo "cli: expected parser error" >&2
    exit 1
fi
grep -q 'check_invalid_parser.caret:3:1: error: parser: expected `;`' \
    "$root/build/check_invalid_parser.err"

if "$binary" check "$root/tests/fixtures/check_invalid_lexer.caret" \
    > /dev/null 2> "$root/build/check_invalid_lexer.err"; then
    echo "cli: expected lexer error" >&2
    exit 1
fi
grep -q "check_invalid_lexer.caret:2:12: error: lexer: unknown character" \
    "$root/build/check_invalid_lexer.err"

echo "cli: all tests passed"
