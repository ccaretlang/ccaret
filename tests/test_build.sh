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
build_run tuple_nested 31
build_run tuple_members 21
build_run tuple_unit 42
build_run tuple_var 42
build_run tuple_forward 42
build_run tuple_shortcircuit 42
build_run optional 42
build_run optional_nested 42
build_run tuple_bounds 42
build_run for_exclusive 14
build_run for_inclusive 15
build_run for_nested 20
build_run for_boundaries 42
build_run for_continue_max 42
build_run for_scoping 42
build_run for_boolean_bounds 42
build_run for_aggregate 42
build_run compound 42
build_run bitwise 42
build_run wrapping 42
build_run array_basic 42
build_run array_copy 42
build_run array_nested 42
build_run array_nested_arrays 42
build_run array_empty 42
build_run array_len 42
build_run array_flow 42
build_run array_mutate 42
build_run array_infer 42
build_run array_order 42
build_run array_usize 42
build_run array_computed_len 42
build_run array_index_flow 42
build_run array_optional_write 42
build_run array_nested_write 42
build_run struct_basic 42
build_run struct_copy 42
build_run struct_nested 42
build_run struct_empty 42
build_run struct_forward 42
build_run struct_array 42
build_run struct_updates 42
build_run struct_unit 42
build_run struct_nominal 42
build_run struct_condition 42
build_run struct_mutation 42
for case in slice_basic slice_const slice_call slice_bool slice_struct \
    slice_empty slice_flow slice_optional slice_array slice_nested; do
    build_run "$case" 42
done

for case in optional_access_basic optional_access_none \
    optional_access_nested optional_access_struct optional_access_array \
    optional_access_slice optional_access_pointer optional_access_call \
    optional_access_tuple optional_access_bool optional_access_flow \
    optional_access_field_collision optional_access_shortcircuit; do
    build_run "$case" 42
done

"$binary" emit-ir "$root/tests/fixtures/build_optional_access_basic.caret" \
    -o "$out/optional_access_basic.ir"
grep -q 'optional.has' "$out/optional_access_basic.ir"
grep -q 'optional.unwrap' "$out/optional_access_basic.ir"
grep -q 'if (!r' "$out/compiled_optional_access_basic.c"
grep -q '\.value;' "$out/compiled_optional_access_basic.c"

for case in subrange_basic subrange_write subrange_nested subrange_slice \
    subrange_empty subrange_flow subrange_temporary_slice \
    subrange_struct subrange_tuple subrange_bool subrange_order; do
    build_run "$case" 42
done

"$binary" emit-ir "$root/tests/fixtures/build_subrange_nested.caret" \
    -o "$out/subrange_nested.ir"
grep -q 'slice.from.slot' "$out/subrange_nested.ir"
grep -q 'slice.subrange' "$out/subrange_nested.ir"
grep -q 'slice.store' "$out/subrange_nested.ir"
grep -q 'ccrt_subrange_i32' "$out/compiled_subrange_nested.c"
grep -q 'end < begin' "$out/compiled_subrange_nested.c"
grep -q 'data == NULL ? NULL' "$out/compiled_subrange_empty.c"

"$binary" emit-ir "$root/tests/fixtures/build_slice_basic.caret" \
    -o "$out/slice_basic.ir"
grep -q '= \[\]i32' "$out/slice_basic.ir"
grep -q 'slice.from.slot' "$out/slice_basic.ir"
grep -q 'slice.get' "$out/slice_basic.ir"
grep -q 'slice.store' "$out/slice_basic.ir"
"$binary" emit-ir "$root/tests/fixtures/build_slice_empty.caret" \
    -o "$out/slice_empty.ir"
grep -q 'slice.len' "$out/slice_empty.ir"
grep -q 'size_t len;' "$out/compiled_slice_basic.c"
grep -q 'const int32_t \*data;' "$out/compiled_slice_const.c"
for case in ptr_basic ptr_const ptr_call ptr_bool ptr_nested \
    ptr_struct ptr_array ptr_optional ptr_flow; do
    build_run "$case" 42
done

"$binary" emit-ir "$root/tests/fixtures/build_ptr_basic.caret" \
    -o "$out/ptr_basic.ir"
grep -q 'addr.slot' "$out/ptr_basic.ir"
grep -q 'ptr.load' "$out/ptr_basic.ir"
grep -q 'ptr.store' "$out/ptr_basic.ir"
grep -q '= \*i32' "$out/ptr_basic.ir"
grep -q 'int32_t \*cc_t_' "$out/compiled_ptr_basic.c"
grep -q 'const int32_t \*cc_t_' "$out/compiled_ptr_const.c"

"$binary" emit-ir "$root/tests/fixtures/build_struct_copy.caret" \
    -o "$out/struct_copy.ir"
grep -q 'struct Pair { first: i32, second: i32 }' "$out/struct_copy.ir"
grep -q 'struct(' "$out/struct_copy.ir"
grep -q 'struct.get ' "$out/struct_copy.ir"
grep -q 'struct.set ' "$out/struct_copy.ir"
grep -q 'f0;' "$out/compiled_struct_basic.c"
grep -q 'uint8_t _unit;' "$out/compiled_struct_empty.c"

"$binary" emit-ir "$root/tests/fixtures/build_array_mutate.caret" \
    -o "$out/array_mutate.ir"
grep -q 'type t3 = \[3\]i32' "$out/array_mutate.ir"
grep -q 'array(' "$out/array_mutate.ir"
grep -q 'array.get' "$out/array_mutate.ir"
grep -q 'array.store' "$out/array_mutate.ir"
"$binary" emit-ir "$root/tests/fixtures/build_array_len.caret" \
    -o "$out/array_len.ir"
grep -q 'const.usize 3' "$out/array_len.ir"
grep -q 'size_t ccrt_index_i32' "$out/compiled_array_basic.c"
grep -q 'elements\[3\]' "$out/compiled_array_basic.c"
grep -q 'uint8_t _unit;' "$out/compiled_array_empty.c"

"$binary" build "$root/tests/fixtures/check_valid.caret" -o "$out/divmod"
run_exit 5 "$out/divmod"
"$binary" emit-ir "$root/tests/fixtures/check_valid.caret" \
    -o "$out/divmod.ir"
grep -q 'type t3 = (i32, i32)' "$out/divmod.ir"
grep -q 'tuple(' "$out/divmod.ir"
grep -q 'tuple.get ' "$out/divmod.ir"

"$binary" emit-ir "$root/tests/fixtures/build_optional_nested.caret" \
    -o "$out/optional_nested.ir"
grep -q 'type t3 = ?i32' "$out/optional_nested.ir"
grep -q 'some ' "$out/optional_nested.ir"
grep -q 'none' "$out/optional_nested.ir"
grep -q 'bool has;' "$out/compiled_optional.c"
grep -q 'uint8_t _unit;' "$out/compiled_tuple_unit.c"

"$binary" emit-ir "$root/tests/fixtures/build_for_boundaries.caret" \
    -o "$out/for_boundaries.ir"
grep -q 'le %' "$out/for_boundaries.ir"
grep -q 'condbr' "$out/for_boundaries.ir"
"$binary" emit-ir "$root/tests/fixtures/build_wrapping.caret" \
    -o "$out/wrapping.ir"
grep -q 'add.wrap' "$out/wrapping.ir"
grep -q 'sub.wrap' "$out/wrapping.ir"
grep -q 'mul.wrap' "$out/wrapping.ir"
"$binary" emit-ir "$root/tests/fixtures/build_bitwise.caret" \
    -o "$out/bitwise.ir"
grep -q 'bit.not' "$out/bitwise.ir"
grep -q 'bit.and' "$out/bitwise.ir"
grep -q 'bit.or' "$out/bitwise.ir"
grep -q 'bit.xor' "$out/bitwise.ir"

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

for case in hello call factorial while shortcircuit boolean \
    tuple_nested tuple_members tuple_unit tuple_var tuple_forward tuple_shortcircuit \
    optional optional_nested tuple_bounds \
    for_exclusive for_inclusive for_nested for_boundaries for_continue_max for_scoping \
    for_boolean_bounds for_aggregate compound bitwise wrapping \
    array_basic array_copy array_nested array_nested_arrays array_empty \
    array_len array_flow array_mutate array_infer array_order \
    array_usize array_computed_len array_index_flow \
    array_optional_write array_nested_write \
    struct_basic struct_copy struct_nested struct_empty struct_forward \
    struct_array struct_updates struct_unit struct_nominal \
    struct_condition struct_mutation \
    ptr_basic ptr_const ptr_call ptr_bool ptr_nested \
    ptr_struct ptr_array ptr_optional ptr_flow \
    slice_basic slice_const slice_call slice_bool slice_struct \
    slice_empty slice_flow slice_optional slice_array slice_nested \
    optional_access_basic optional_access_none \
    optional_access_nested optional_access_struct optional_access_array \
    optional_access_slice optional_access_pointer optional_access_call \
    optional_access_tuple optional_access_bool optional_access_flow \
    optional_access_field_collision optional_access_shortcircuit \
    subrange_basic subrange_write subrange_nested subrange_slice \
    subrange_empty subrange_flow subrange_temporary_slice \
    subrange_struct subrange_tuple subrange_bool subrange_order; do
    gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
        "$out/compiled_$case.c" -o "$out/optimized_$case"
    case "$case" in
        factorial) expected=120 ;;
        while) expected=30 ;;
        tuple_nested) expected=31 ;;
        tuple_members) expected=21 ;;
        for_exclusive) expected=14 ;;
        for_inclusive) expected=15 ;;
        for_nested) expected=20 ;;
        *) expected=42 ;;
    esac
    run_exit "$expected" "$out/optimized_$case"
done

gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$out/divmod.c" -o "$out/divmod_optimized"
run_exit 5 "$out/divmod_optimized"

"$binary" build "$root/tests/fixtures/build_hello.caret" \
    -o "$out/binary with spaces"
run_exit 42 "$out/binary with spaces"

( cd "$out" && "$binary" build \
    "$root/tests/fixtures/build_hello.caret" -o "-leading-name" )
run_exit 42 "$out/-leading-name"

( cd "$out" && "$binary" build \
    "$root/tests/fixtures/build_hello.caret" -o "@literal-name" )
run_exit 42 "$out/@literal-name"

for case in overflow divzero div_overflow neg_overflow \
    compound_overflow compound_divzero \
    array_negative_index array_oob_index array_empty_index \
    array_oob_write array_empty_write struct_compound_overflow \
    ptr_overflow slice_oob slice_negative slice_empty_oob \
    slice_oob_write slice_overflow \
    subrange_negative subrange_oob subrange_reversed \
    subrange_empty_oob subrange_nested_oob \
    optional_access_none_trap optional_access_nested_trap \
    optional_access_call_trap; do
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

for case in overflow compound_overflow compound_divzero \
    array_negative_index array_oob_index array_empty_index \
    array_oob_write array_empty_write struct_compound_overflow \
    ptr_overflow slice_oob slice_negative slice_empty_oob \
    slice_oob_write slice_overflow \
    subrange_negative subrange_oob subrange_reversed \
    subrange_empty_oob subrange_nested_oob \
    optional_access_none_trap optional_access_nested_trap \
    optional_access_call_trap; do
    gcc -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror \
        "$out/trap_$case.c" -o "$out/optimized_trap_$case"
    set +e
    ( "$out/optimized_trap_$case" > /dev/null 2>&1 ) 2> /dev/null
    optimized_status=$?
    set -e
    if [ "$optimized_status" -ne 134 ]; then
        echo "build: optimized $case must abort (134), got $optimized_status" >&2
        exit 1
    fi
done

if "$binary" build "$root/tests/fixtures/build_shift_unsupported.caret" \
    -o "$out/shift_unsupported" > /dev/null 2> "$out/shift_unsupported.err"; then
    echo "build: signed shifts must remain unsupported until defined" >&2
    exit 1
fi
grep -q 'error: IR: IR lowering: binary operator not yet supported' \
    "$out/shift_unsupported.err"

if "$binary" build "$root/tests/fixtures/build_unsupported.caret" \
    -o "$out/unsupported" > /dev/null 2> "$out/unsupported.err"; then
    echo "build: unsupported slice element type must fail at IR lowering" >&2
    exit 1
fi
grep -q 'error: IR: IR backend currently supports only' "$out/unsupported.err"

if "$binary" build "$root/tests/fixtures/build_array_oversize.caret" \
    -o "$out/oversize" > /dev/null 2> "$out/oversize.err"; then
    echo "build: oversize fixed array must be rejected" >&2
    exit 1
fi
grep -q 'IR lowering: array length exceeds current C17 limit' \
    "$out/oversize.err"

for case in struct_bad_return struct_recursive struct_unsupported_field; do
    if "$binary" build "$root/tests/fixtures/build_$case.caret" \
        -o "$out/unsupported_$case" > /dev/null 2> "$out/$case.err"; then
        echo "build: invalid struct case $case must fail" >&2
        exit 1
    fi
done
grep -q 'error: semantic: struct literal type mismatch' \
    "$out/struct_bad_return.err"
grep -q 'error: semantic: recursive struct value type' \
    "$out/struct_recursive.err"
grep -q 'error: IR: IR backend currently supports only' \
    "$out/struct_unsupported_field.err"

for case in ptr_unsafe_read_invalid ptr_const_store_invalid \
    ptr_address_literal_invalid; do
    if "$binary" build "$root/tests/fixtures/build_$case.caret" \
        -o "$out/invalid_$case" > /dev/null 2> "$out/$case.err"; then
        echo "build: invalid raw pointer case $case must fail" >&2
        exit 1
    fi
done
grep -q 'error: semantic: raw pointer dereference requires unsafe block' \
    "$out/ptr_unsafe_read_invalid.err"
grep -q 'error: semantic: cannot write through const raw pointer' \
    "$out/ptr_const_store_invalid.err"
grep -q 'error: semantic: address-of currently requires a named local' \
    "$out/ptr_address_literal_invalid.err"

for case in slice_const_write_invalid slice_nonarray_invalid \
    slice_temporary_invalid; do
    if "$binary" build "$root/tests/fixtures/build_$case.caret" \
        -o "$out/invalid_$case" > /dev/null 2> "$out/$case.err"; then
        echo "build: invalid slice case $case must fail" >&2
        exit 1
    fi
done
grep -q 'error: semantic: cannot write through const slice' \
    "$out/slice_const_write_invalid.err"
grep -q 'error: semantic: slice requires a fixed array' \
    "$out/slice_nonarray_invalid.err"
grep -q 'error: semantic: slice requires a named local array' \
    "$out/slice_temporary_invalid.err"

for case in subrange_inclusive_invalid subrange_temporary_array_invalid \
    subrange_bool_invalid subrange_const_write_invalid; do
    if "$binary" build "$root/tests/fixtures/build_$case.caret" \
        -o "$out/invalid_$case" > /dev/null 2> "$out/$case.err"; then
        echo "build: invalid subrange case $case must fail" >&2
        exit 1
    fi
done
grep -q 'error: semantic: slice subranges require exclusive .. bounds' \
    "$out/subrange_inclusive_invalid.err"
grep -q 'error: semantic: array subrange requires a named local array' \
    "$out/subrange_temporary_array_invalid.err"
grep -q 'error: semantic: type mismatch' "$out/subrange_bool_invalid.err"
grep -q 'error: semantic: cannot write through const slice' \
    "$out/subrange_const_write_invalid.err"

for case in optional_access_bad_has optional_access_bad_value \
    optional_access_bad_member optional_access_bad_type \
    optional_access_none_infer; do
    if "$binary" build "$root/tests/fixtures/build_$case.caret" \
        -o "$out/invalid_$case" > /dev/null 2> "$out/$case.err"; then
        echo "build: invalid optional access $case must fail" >&2
        exit 1
    fi
done
grep -q 'error: semantic: unknown member' "$out/optional_access_bad_has.err"
grep -q 'error: semantic: unknown member' "$out/optional_access_bad_value.err"
grep -q 'error: semantic: unknown optional member' \
    "$out/optional_access_bad_member.err"
grep -q 'error: semantic: type mismatch' "$out/optional_access_bad_type.err"
grep -q 'error: semantic: none requires an optional type context' \
    "$out/optional_access_none_infer.err"

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
