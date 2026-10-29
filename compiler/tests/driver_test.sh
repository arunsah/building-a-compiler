#!/bin/sh
set -eu

compiler=${1:?usage: driver_test.sh STUDYCC}
temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/study-driver.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

fail() {
    printf 'driver test failed: %s\n' "$1" >&2
    exit 1
}

expect_status() {
    expected_status=$1
    shift

    set +e
    "$@" >"$temporary_directory/stdout" 2>"$temporary_directory/stderr"
    actual_status=$?
    set -e

    if [ "$actual_status" -ne "$expected_status" ]; then
        printf 'expected status %s, received %s\n' \
            "$expected_status" "$actual_status" >&2
        sed -n '1,80p' "$temporary_directory/stdout" >&2
        sed -n '1,80p' "$temporary_directory/stderr" >&2
        exit 1
    fi
}

expect_status 0 "$compiler" --help
grep -Fq 'studycc dump source INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump source'
grep -Fq 'studycc dump tokens INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump tokens'
grep -Fq 'studycc dump ast INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump ast'
grep -Fq 'studycc dump checked INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump checked'
grep -Fq 'studycc dump ir INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump ir'
grep -Fq 'studycc check INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe check'
grep -Fq 'studycc interpret INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe interpret'

expect_status 2 "$compiler"
grep -Fq 'Usage:' "$temporary_directory/stderr" || \
    fail 'missing arguments did not print usage'

expect_status 2 "$compiler" unknown
grep -Fq 'Usage:' "$temporary_directory/stderr" || \
    fail 'unknown command did not print usage'

expect_status 0 "$compiler" dump source examples/return-42.study
grep -Fq 'bytes: 36' "$temporary_directory/stdout" || \
    fail 'checked-in return-42 source byte count is wrong'
grep -Fq 'lines: 4' "$temporary_directory/stdout" || \
    fail 'checked-in return-42 source line count is wrong'

expect_status 0 "$compiler" dump tokens examples/return-42.study
grep -Fq '0..2  1:1..1:3  fn  `fn`' "$temporary_directory/stdout" || \
    fail 'token dump did not report fn and its range'
grep -Fq '30..32  2:12..2:14  integer  `42`  value=42' \
    "$temporary_directory/stdout" || fail 'token dump lost the integer value'
grep -Fq '36..36  4:1..4:1  eof' "$temporary_directory/stdout" || \
    fail 'token dump did not report the EOF position'

expect_status 0 "$compiler" dump ast examples/return-42.study
grep -Fq 'program  1:1..4:1' "$temporary_directory/stdout" || \
    fail 'syntax dump did not include the program range'
grep -Fq '  function main -> i32  1:1..3:2' \
    "$temporary_directory/stdout" || \
    fail 'syntax dump did not include the main function'
grep -Fq '      return  2:5..2:15' "$temporary_directory/stdout" || \
    fail 'syntax dump did not include the return statement'
grep -Fq '        integer 42  2:12..2:14' \
    "$temporary_directory/stdout" || \
    fail 'syntax dump did not preserve the integer expression'

expect_status 0 "$compiler" dump ast examples/sum-to.study
grep -Fq '  function sum_to -> i32' "$temporary_directory/stdout" || \
    fail 'syntax dump did not include sum_to'
grep -Fq '      while' "$temporary_directory/stdout" || \
    fail 'syntax dump did not include the loop'
grep -Fq '        call sum_to' "$temporary_directory/stdout" || \
    fail 'syntax dump did not include the call'

expect_status 1 "$compiler" dump ast examples/invalid/missing-semicolon.study
grep -Fq 'examples/invalid/missing-semicolon.study:3:1: error: expected' \
    "$temporary_directory/stderr" || \
    fail 'parser did not locate the missing semicolon'

expect_status 0 "$compiler" check examples/return-42.study
test ! -s "$temporary_directory/stdout" || \
    fail 'successful check wrote unexpected standard output'
test ! -s "$temporary_directory/stderr" || \
    fail 'successful check wrote unexpected standard error'

expect_status 0 "$compiler" check examples/sum-to.study
expect_status 0 "$compiler" dump checked examples/return-42.study
grep -Fq 'function main -> i32  [decl=#0, returns=yes]' \
    "$temporary_directory/stdout" || \
    fail 'checked dump did not annotate main'
grep -Fq 'integer 42  [type=i32]' "$temporary_directory/stdout" || \
    fail 'checked dump did not annotate the integer type'

expect_status 1 "$compiler" check examples/invalid/type-mismatch.study
grep -Fq 'examples/invalid/type-mismatch.study:2:12: error: return value requires i32, found bool' \
    "$temporary_directory/stderr" || \
    fail 'checker did not locate the return type mismatch'

expect_status 0 "$compiler" dump ir examples/return-42.study
grep -Fq 'function @main() -> i32 {' "$temporary_directory/stdout" || \
    fail 'IR dump did not include main'
grep -Fq '%v0: i32 = const.i32 42' "$temporary_directory/stdout" || \
    fail 'IR dump did not include the integer value'
grep -Fq 'return %v0' "$temporary_directory/stdout" || \
    fail 'IR dump did not include the return terminator'

expect_status 0 "$compiler" interpret examples/return-42.study
test ! -s "$temporary_directory/stdout" || \
    fail 'return-only interpretation wrote unexpected program output'

expect_status 0 "$compiler" interpret examples/sum-to.study
test "$(cat "$temporary_directory/stdout")" = '55' || \
    fail 'sum-to interpretation did not print 55'
test ! -s "$temporary_directory/stderr" || \
    fail 'successful interpretation wrote unexpected standard error'

missing_path="$temporary_directory/missing.study"
expect_status 1 "$compiler" dump source "$missing_path"
grep -Fq "$missing_path: error: could not open source:" \
    "$temporary_directory/stderr" || fail 'missing file diagnostic is incomplete'

empty_path="$temporary_directory/empty source.study"
: >"$empty_path"
expect_status 0 "$compiler" dump source "$empty_path"
grep -Fq "source: $empty_path" "$temporary_directory/stdout" || \
    fail 'source path with spaces was not preserved'
grep -Fq 'bytes: 0' "$temporary_directory/stdout" || \
    fail 'empty source byte count is wrong'
grep -Fq 'lines: 1' "$temporary_directory/stdout" || \
    fail 'empty source line count is wrong'
grep -Fq 'final line ending: no' "$temporary_directory/stdout" || \
    fail 'empty source final-line result is wrong'

line_path="$temporary_directory/lines.study"
printf 'a\r\nb\rc\n' >"$line_path"
expect_status 0 "$compiler" dump source "$line_path"
grep -Fq 'bytes: 7' "$temporary_directory/stdout" || \
    fail 'mixed-line-ending byte count is wrong'
grep -Fq 'lines: 4' "$temporary_directory/stdout" || \
    fail 'mixed-line-ending line count is wrong'
grep -Fq 'final line ending: yes' "$temporary_directory/stdout" || \
    fail 'mixed-line-ending final-line result is wrong'

nul_path="$temporary_directory/nul.study"
printf 'a\000b' >"$nul_path"
expect_status 0 "$compiler" dump source "$nul_path"
grep -Fq 'bytes: 3' "$temporary_directory/stdout" || \
    fail 'embedded NUL was not preserved as a source byte'
expect_status 1 "$compiler" dump tokens "$nul_path"
grep -Fq "$nul_path:1:2: error: unsupported byte 0x00" \
    "$temporary_directory/stderr" || \
    fail 'lexer did not locate the embedded NUL'

overflow_path="$temporary_directory/overflow.study"
printf '2147483648\n' >"$overflow_path"
expect_status 1 "$compiler" dump tokens "$overflow_path"
grep -Fq "$overflow_path:1:1: error: integer literal exceeds 2147483647" \
    "$temporary_directory/stderr" || \
    fail 'lexer did not reject an out-of-range integer'

large_path="$temporary_directory/large.study"
awk 'BEGIN { for (i = 0; i < 9000; i++) printf "x" }' >"$large_path"
expect_status 0 "$compiler" dump source "$large_path"
grep -Fq 'bytes: 9000' "$temporary_directory/stdout" || \
    fail 'source buffer did not grow correctly'

printf 'driver tests passed\n'
