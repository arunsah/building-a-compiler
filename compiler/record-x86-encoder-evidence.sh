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
temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/study-encoder-record.XXXXXX")
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
    printf 'object-wrapper=%s\n' "$(command -v objcopy || printf unavailable)"
    printf 'linker=%s\n' "$(command -v ld || printf unavailable)"
    printf 'disassembler=%s\n' "$(command -v objdump || printf unavailable)"
} >"$output_directory/environment.txt"

{
    printf '%s\n' 'make BUILD_DIR=<temporary>/strict test'
    printf '%s\n' 'make BUILD_DIR=<temporary>/sanitize \'
    printf '%s\n' "  CFLAGS='<strict flags> -fsanitize=address,undefined -fno-omit-frame-pointer' \\"
    printf '%s\n' "  LDFLAGS='-fsanitize=address,undefined' test"
    for program in return-42 sum-to operations; do
        printf '<temporary>/strict/studycc dump encoding examples/%s.study\n' "$program"
        printf '<temporary>/strict/studycc emit bin examples/%s.study > %s.bin\n' \
            "$program" "$program"
        printf '<temporary>/strict/studycc emit asm examples/%s.study > %s.s\n' \
            "$program" "$program"
    done
    printf '%s\n' '<temporary>/strict/studycc interpret examples/sum-to.study'
    printf '%s\n' '<temporary>/strict/studycc interpret examples/operations.study'
    printf '%s\n' '<temporary>/strict/studycc build examples/PROGRAM.study -o <temporary>/PROGRAM.encoded'
    printf '%s\n' '<temporary>/strict/studycc build via-asm examples/PROGRAM.study -o <temporary>/PROGRAM.via-asm'
    printf '%s\n' 'objdump -D -b binary -m i386:x86-64 -Mintel PROGRAM.bin'
    printf '%s\n' 'readelf -h -l -S -s PROGRAM.encoded'
} >"$output_directory/commands.txt"

(
    cd "$script_directory"
    make BUILD_DIR="$temporary_directory/strict" test >/dev/null
)
{
    printf 'strict-warning-tests=passed\n'
    printf 'x86-target-verifier-tests=passed\n'
    printf 'instruction-encoding-tests=passed\n'
    printf 'relative-fixup-tests=passed\n'
    printf 'binary-emission-tests=passed\n'
    printf 'system-adapter-tests=passed\n'
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
for program in return-42 sum-to operations; do
    (
        cd "$script_directory"
        "$strict_compiler" dump x86-64 "examples/$program.study"
    ) >"$output_directory/$program.x86-64.txt"
    (
        cd "$script_directory"
        "$strict_compiler" dump encoding "examples/$program.study"
    ) >"$output_directory/$program.encoding.txt"
    (
        cd "$script_directory"
        "$strict_compiler" emit bin "examples/$program.study"
    ) >"$output_directory/$program.bin"
    (
        cd "$script_directory"
        "$strict_compiler" emit asm "examples/$program.study"
    ) >"$output_directory/$program.s"
done

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
    command -v objcopy >/dev/null 2>&1 &&
    command -v ld >/dev/null 2>&1 &&
    command -v objdump >/dev/null 2>&1 &&
    command -v readelf >/dev/null 2>&1; then
    for program in return-42 sum-to operations; do
        (
            cd "$script_directory"
            "$strict_compiler" build "examples/$program.study" \
                -o "$temporary_directory/$program.encoded"
            "$strict_compiler" build via-asm "examples/$program.study" \
                -o "$temporary_directory/$program.via-asm"
        )

        objdump -D -b binary -m i386:x86-64 -Mintel \
            "$output_directory/$program.bin" \
            >"$output_directory/$program.encoded-disassembly.txt"
        objdump -d -Mintel "$temporary_directory/$program.via-asm" \
            >"$output_directory/$program.assembly-reference-disassembly.txt"
        readelf -h -l -S -s "$temporary_directory/$program.encoded" \
            >"$output_directory/$program.elf.txt"
        objcopy -O binary --only-section=.text \
            "$temporary_directory/$program.encoded" \
            "$temporary_directory/$program.linked-text.bin"
        cmp "$output_directory/$program.bin" \
            "$temporary_directory/$program.linked-text.bin"
    done

    set +e
    "$temporary_directory/return-42.encoded" \
        >"$output_directory/return-42.native-output.txt"
    return_status=$?
    "$temporary_directory/sum-to.encoded" \
        >"$output_directory/sum-to.native-output.txt"
    sum_status=$?
    "$temporary_directory/operations.encoded" \
        >"$output_directory/operations.native-output.txt"
    operations_status=$?
    "$temporary_directory/return-42.via-asm" \
        >"$temporary_directory/return-42.via-asm-output.txt"
    reference_return_status=$?
    "$temporary_directory/sum-to.via-asm" \
        >"$temporary_directory/sum-to.via-asm-output.txt"
    reference_sum_status=$?
    "$temporary_directory/operations.via-asm" \
        >"$temporary_directory/operations.via-asm-output.txt"
    reference_operations_status=$?
    set -e

    if [ "$return_status" -ne 42 ] || [ "$sum_status" -ne 0 ] ||
        [ "$operations_status" -ne 37 ] ||
        [ "$reference_return_status" -ne 42 ] ||
        [ "$reference_sum_status" -ne 0 ] ||
        [ "$reference_operations_status" -ne 37 ]; then
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
    cmp "$output_directory/return-42.native-output.txt" \
        "$temporary_directory/return-42.via-asm-output.txt"
    cmp "$output_directory/sum-to.native-output.txt" \
        "$temporary_directory/sum-to.via-asm-output.txt"
    cmp "$output_directory/operations.native-output.txt" \
        "$temporary_directory/operations.via-asm-output.txt"

    {
        printf 'native-execution=passed\n'
        printf 'return-42-status=%s\n' "$return_status"
        printf 'sum-to-status=%s\n' "$sum_status"
        printf 'operations-status=%s\n' "$operations_status"
        printf 'interpreter-encoded-output=identical\n'
        printf 'assembly-encoded-output=identical\n'
        printf 'linked-text-flat-image=identical\n'
    } >"$output_directory/native-results.txt"
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

printf 'recorded x86-64 encoder evidence in %s\n' "$output_directory"
