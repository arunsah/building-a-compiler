#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    printf 'usage: %s NEW_OUTPUT_DIRECTORY\n' "$0" >&2
    exit 2
fi

output_directory=$1
if [ -e "$output_directory" ]; then
    printf 'output already exists: %s\n' "$output_directory" >&2
    exit 1
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_directory=$(CDPATH= cd -- "$script_directory/.." && pwd)
temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/study-checker-record.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

mkdir -p "$output_directory"

if command -v git >/dev/null 2>&1 &&
    git -C "$repository_directory" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    source_revision=$(git -C "$repository_directory" rev-parse HEAD)
else
    source_revision=${SOURCE_REVISION:-unknown}
fi

{
    printf 'recorded-utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'system=%s\n' "$(uname -s)"
    printf 'release=%s\n' "$(uname -r)"
    printf 'machine=%s\n' "$(uname -m)"
    printf 'source-revision=%s\n' "$source_revision"
    printf 'cc='
    cc --version 2>&1 | sed -n '1p'
    printf 'make='
    make --version 2>&1 | sed -n '1p'
} >"$output_directory/environment.txt"

cat >"$output_directory/commands.txt" <<'EOF'
make BUILD_DIR=<temporary>/strict test
make BUILD_DIR=<temporary>/sanitize \
  CFLAGS='<strict flags> -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined' test
<temporary>/strict/studycc dump checked examples/return-42.study
<temporary>/strict/studycc check examples/invalid/type-mismatch.study
EOF

(
    cd "$script_directory"
    make BUILD_DIR="$temporary_directory/strict" test >/dev/null
)
printf 'strict-warning-tests=passed\n' >"$output_directory/tests.txt"

(
    cd "$script_directory"
    make \
        BUILD_DIR="$temporary_directory/sanitize" \
        CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Wno-format-nonliteral -Werror -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer' \
        LDFLAGS='-fsanitize=address,undefined' \
        test >/dev/null
)
printf 'address-and-undefined-sanitizers=passed\n' \
    >"$output_directory/sanitizers.txt"

(
    cd "$script_directory"
    "$temporary_directory/strict/studycc" \
        dump checked examples/return-42.study
) >"$output_directory/sample-checked.txt"

set +e
(
    cd "$script_directory"
    "$temporary_directory/strict/studycc" \
        check examples/invalid/type-mismatch.study
) >"$temporary_directory/invalid-stdout" 2>"$temporary_directory/invalid-stderr"
invalid_status=$?
set -e

if [ "$invalid_status" -ne 1 ]; then
    printf 'expected invalid sample to exit 1, received %s\n' \
        "$invalid_status" >&2
    exit 1
fi

{
    printf 'exit-status=%s\n' "$invalid_status"
    cat "$temporary_directory/invalid-stderr"
} >"$output_directory/type-mismatch.txt"

if command -v sha256sum >/dev/null 2>&1; then
    (
        cd "$output_directory"
        sha256sum commands.txt environment.txt sample-checked.txt \
            sanitizers.txt tests.txt type-mismatch.txt
    ) >"$output_directory/SHA256SUMS"
else
    (
        cd "$output_directory"
        shasum -a 256 commands.txt environment.txt sample-checked.txt \
            sanitizers.txt tests.txt type-mismatch.txt
    ) >"$output_directory/SHA256SUMS"
fi

printf 'recorded checker evidence in %s\n' "$output_directory"
