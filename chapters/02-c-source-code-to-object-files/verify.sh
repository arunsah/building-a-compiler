#!/bin/sh

set -eu

chapter_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/compiler-chapter-02.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

c_compiler=${CC:-cc}
common_flags="-std=c17 -Wall -Wextra -Wpedantic -Werror"

"$c_compiler" $common_flags "$chapter_dir/hello.c" -o "$build_dir/hello"
output=$($build_dir/hello)

[ "$output" = "Hello, world!" ]

printf 'Chapter 2 example passed with %s on %s.\n' \
    "$("$c_compiler" --version | sed -n '1p')" "$(uname -m)"
