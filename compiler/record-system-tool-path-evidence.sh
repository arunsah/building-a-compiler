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
temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/study-native-record.XXXXXX")
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
    printf 'assembler=%s\n' "$(command -v as || printf unavailable)"
    printf 'linker=%s\n' "$(command -v ld || printf unavailable)"
} >"$output_directory/environment.txt"

{
    printf '%s\n' 'make BUILD_DIR=<temporary>/strict test'
    printf '%s\n' 'make BUILD_DIR=<temporary>/sanitize \'
    printf '%s\n' "  CFLAGS='<strict flags> -fsanitize=address,undefined -fno-omit-frame-pointer' \\"
    printf '%s\n' "  LDFLAGS='-fsanitize=address,undefined' test"
    printf '%s\n' '<temporary>/strict/studycc emit asm examples/return-42.study'
    printf '%s\n' '<temporary>/strict/studycc emit asm examples/sum-to.study'
    printf '%s\n' '<temporary>/strict/studycc interpret examples/sum-to.study'
    printf '%s\n' '<temporary>/strict/studycc interpret examples/operations.study'
    printf '%s\n' '<temporary>/strict/studycc build examples/return-42.study -o <temporary>/return-42'
    printf '%s\n' '<temporary>/strict/studycc build examples/sum-to.study -o <temporary>/sum-to'
    printf '%s\n' '<temporary>/strict/studycc build examples/operations.study -o <temporary>/operations'
    printf '%s\n' 'file <temporary>/sum-to'
    printf '%s\n' 'readelf -h -l -S <temporary>/sum-to'
    printf '%s\n' 'nm --numeric-sort <temporary>/sum-to'
    printf '%s\n' 'objdump -d <temporary>/sum-to'
} >"$output_directory/commands.txt"

(
    cd "$script_directory"
    make BUILD_DIR="$temporary_directory/strict" test >/dev/null
)
{
    printf 'strict-warning-tests=passed\n'
    printf 'assembly-emission-tests=passed\n'
    printf 'system-tool-adapter-tests=passed\n'
    printf 'output-publication-tests=passed\n'
} >"$output_directory/tests.txt"

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

strict_compiler="$temporary_directory/strict/studycc"
(
    cd "$script_directory"
    "$strict_compiler" emit asm examples/return-42.study
) >"$output_directory/return-42.s"
(
    cd "$script_directory"
    "$strict_compiler" emit asm examples/sum-to.study
) >"$output_directory/sum-to.s"
(
    cd "$script_directory"
    "$strict_compiler" interpret examples/sum-to.study
) >"$output_directory/sum-to.interpreter-output.txt"
(
    cd "$script_directory"
    "$strict_compiler" interpret examples/operations.study
) >"$output_directory/operations.interpreter-output.txt"

if [ "$(cat "$output_directory/sum-to.interpreter-output.txt")" != '55' ]; then
    printf 'expected sum-to.study to print 55\n' >&2
    exit 1
fi

if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] &&
    command -v as >/dev/null 2>&1 &&
    command -v ld >/dev/null 2>&1 &&
    command -v file >/dev/null 2>&1 &&
    command -v readelf >/dev/null 2>&1 &&
    command -v nm >/dev/null 2>&1 &&
    command -v objdump >/dev/null 2>&1 &&
    as --version 2>/dev/null | grep -Fq 'GNU assembler'; then
    return_executable="$temporary_directory/return-42"
    sum_executable="$temporary_directory/sum-to"
    operations_executable="$temporary_directory/operations"

    (
        cd "$script_directory"
        "$strict_compiler" build examples/return-42.study \
            -o "$return_executable"
        "$strict_compiler" build examples/sum-to.study \
            -o "$sum_executable"
        "$strict_compiler" build examples/operations.study \
            -o "$operations_executable"
    )

    set +e
    "$return_executable" >"$output_directory/return-42.native-output.txt"
    return_status=$?
    "$sum_executable" >"$output_directory/sum-to.native-output.txt"
    sum_status=$?
    "$operations_executable" \
        >"$output_directory/operations.native-output.txt"
    operations_status=$?
    set -e

    if [ "$return_status" -ne 42 ] || [ "$sum_status" -ne 0 ] ||
        [ "$operations_status" -ne 37 ]; then
        printf 'native process status did not match the Study result\n' >&2
        exit 1
    fi
    if [ -s "$output_directory/return-42.native-output.txt" ]; then
        printf 'return-42.study wrote unexpected output\n' >&2
        exit 1
    fi
    cmp "$output_directory/sum-to.interpreter-output.txt" \
        "$output_directory/sum-to.native-output.txt"
    cmp "$output_directory/operations.interpreter-output.txt" \
        "$output_directory/operations.native-output.txt"

    {
        printf 'native-execution=passed\n'
        printf 'return-42-status=%s\n' "$return_status"
        printf 'sum-to-status=%s\n' "$sum_status"
        printf 'operations-status=%s\n' "$operations_status"
        printf 'interpreter-native-output=identical\n'
    } >"$output_directory/native-results.txt"

    file "$sum_executable" >"$output_directory/sum-to.file.txt"
    readelf -h "$sum_executable" >"$output_directory/sum-to.elf-header.txt"
    readelf -l "$sum_executable" \
        >"$output_directory/sum-to.program-headers.txt"
    readelf -S "$sum_executable" \
        >"$output_directory/sum-to.section-headers.txt"
    nm --numeric-sort "$sum_executable" \
        >"$output_directory/sum-to.symbols.txt"
    objdump -d "$sum_executable" \
        >"$output_directory/sum-to.disassembly.txt"

    grep -Fq 'Class:                             ELF64' \
        "$output_directory/sum-to.elf-header.txt"
    grep -Fq 'Type:                              EXEC' \
        "$output_directory/sum-to.elf-header.txt"
    grep -Fq 'Machine:                           Advanced Micro Devices X86-64' \
        "$output_directory/sum-to.elf-header.txt"
    if grep -Fq 'INTERP' "$output_directory/sum-to.program-headers.txt"; then
        printf 'native output unexpectedly requires a dynamic interpreter\n' >&2
        exit 1
    fi
else
    {
        printf 'native-execution=not-run\n'
        printf 'reason=host-is-not-linux-x86-64-with-gnu-binutils\n'
    } >"$output_directory/native-results.txt"
fi

if command -v sha256sum >/dev/null 2>&1; then
    (
        cd "$output_directory"
        find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort |
            while IFS= read -r file; do
                sha256sum "$file"
            done
    ) >"$output_directory/SHA256SUMS"
else
    (
        cd "$output_directory"
        find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort |
            while IFS= read -r file; do
                shasum -a 256 "$file"
            done
    ) >"$output_directory/SHA256SUMS"
fi

printf 'recorded system-tool path evidence in %s\n' "$output_directory"
