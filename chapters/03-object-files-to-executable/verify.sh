#!/bin/sh

set -eu

chapter_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/compiler-chapter-03.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

c_compiler=${CC:-cc}
archiver=${AR:-ar}
common_flags="-std=c17 -Wall -Wextra -Wpedantic -Werror -O0"
source_dir="$chapter_dir/calculator"

"$c_compiler" $common_flags -c "$source_dir/main.c" -o "$build_dir/main.o"
"$c_compiler" $common_flags -c "$source_dir/add.c" -o "$build_dir/add.o"
"$c_compiler" "$build_dir/main.o" "$build_dir/add.o" -o "$build_dir/calculator"

assert_run()
{
    expected_status=$1
    expected_stdout=$2
    expected_stderr=$3
    shift 3

    stdout_file="$build_dir/stdout.txt"
    stderr_file="$build_dir/stderr.txt"

    set +e
    "$@" >"$stdout_file" 2>"$stderr_file"
    actual_status=$?
    set -e

    actual_stdout=$(cat "$stdout_file")
    actual_stderr=$(cat "$stderr_file")

    if [ "$actual_status" -ne "$expected_status" ] ||
       [ "$actual_stdout" != "$expected_stdout" ] ||
       [ "$actual_stderr" != "$expected_stderr" ]; then
        echo "Unexpected result from: $*" >&2
        printf 'status: expected %s, got %s\n' "$expected_status" "$actual_status" >&2
        printf 'stdout: expected <%s>, got <%s>\n' "$expected_stdout" "$actual_stdout" >&2
        printf 'stderr: expected <%s>, got <%s>\n' "$expected_stderr" "$actual_stderr" >&2
        exit 1
    fi
}

assert_run 0 "20 + 22 = 42" "" "$build_dir/calculator" 20 22
assert_run 0 "-7 + 2 = -5" "" "$build_dir/calculator" -7 2
assert_run 0 "0 + 0 = 0" "" "$build_dir/calculator" 0 0
assert_run 2 "" "Usage: $build_dir/calculator LEFT RIGHT" "$build_dir/calculator" 20
assert_run 2 "" "Both operands must be integers representable by int." \
    "$build_dir/calculator" twenty 22
assert_run 1 "" "The result is outside the range of int." \
    "$build_dir/calculator" 2147483647 1

"$archiver" rcs "$build_dir/libcalculator.a" "$build_dir/add.o"
"$c_compiler" "$build_dir/main.o" "$build_dir/libcalculator.a" \
    -o "$build_dir/calculator-from-archive"
assert_run 0 "20 + 22 = 42" "" "$build_dir/calculator-from-archive" 20 22

printf 'Chapter 3 example passed with %s on %s.\n' \
    "$("$c_compiler" --version | sed -n '1p')" "$(uname -m)"
