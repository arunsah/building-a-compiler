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
grep -Fq 'studycc dump x86-64 INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump x86-64'
grep -Fq 'studycc dump encoding INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump encoding'
grep -Fq 'studycc dump elf INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe dump elf'
grep -Fq 'studycc emit asm INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe emit asm'
grep -Fq 'studycc emit bin INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe emit bin'
grep -Fq 'studycc emit obj INPUT.study' "$temporary_directory/stdout" || \
    fail '--help did not describe emit obj'
grep -Fq 'studycc build INPUT.study -o OUTPUT' "$temporary_directory/stdout" || \
    fail '--help did not describe build'
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

expect_status 0 "$compiler" dump x86-64 examples/return-42.study
grep -Fq 'target x86-64-linux-system-v {' \
    "$temporary_directory/stdout" || \
    fail 'x86-64 dump did not name the target'
grep -Fq 'function f0 _start [frame=0' "$temporary_directory/stdout" || \
    fail 'x86-64 dump did not include the process entry function'
grep -Fq 'ir.v0 -> width=32 [rbp-8]' "$temporary_directory/stdout" || \
    fail 'x86-64 dump did not show the value stack location'
grep -Fq 'mov.i32 #0x0000002a, eax' "$temporary_directory/stdout" || \
    fail 'x86-64 dump did not show selected instructions'

expect_status 0 "$compiler" emit asm examples/return-42.study
grep -Fq '.globl _start' "$temporary_directory/stdout" || \
    fail 'assembly did not export the process entry point'
grep -Fq 'call study_fn_0' "$temporary_directory/stdout" || \
    fail 'assembly entry point did not call Study main'
grep -Fq 'movl $0x0000002a, %eax' "$temporary_directory/stdout" || \
    fail 'assembly did not materialise the return value'

expect_status 0 "$compiler" dump encoding examples/return-42.study
grep -Fq 'image x86-64-linux-system-v [bytes=' \
    "$temporary_directory/stdout" || \
    fail 'encoding dump did not name the code image'
grep -Fq 'function f0 _start @0x00000000' \
    "$temporary_directory/stdout" || \
    fail 'encoding dump did not locate the entry function'
grep -Fq '00000000: e8' "$temporary_directory/stdout" || \
    fail 'encoding dump did not show the first call bytes'

expect_status 0 "$compiler" emit bin examples/return-42.study
test -s "$temporary_directory/stdout" || \
    fail 'binary emission produced an empty code image'
test "$(od -An -tx1 -N1 "$temporary_directory/stdout" | tr -d ' ')" = e8 || \
    fail 'binary emission did not begin with a near call'

expect_status 0 "$compiler" dump elf examples/return-42.study
grep -Fq 'elf64-relocatable x86-64 [bytes=' \
    "$temporary_directory/stdout" || \
    fail 'ELF dump did not identify the relocatable object'
grep -Fq 'section s2 .rela.text' "$temporary_directory/stdout" || \
    fail 'ELF dump did not include the relocation section'
grep -Fq 'sym3 _start global function' "$temporary_directory/stdout" || \
    fail 'ELF dump did not include the entry symbol'
grep -Fq 'R_X86_64_PC32 study_fn_0 addend=-4' \
    "$temporary_directory/stdout" || \
    fail 'ELF dump did not include the main call relocation'

expect_status 0 "$compiler" emit obj examples/return-42.study
test -s "$temporary_directory/stdout" || \
    fail 'ELF object emission produced an empty file'
test "$(od -An -tx1 -N4 "$temporary_directory/stdout" | tr -d ' \n')" = \
    7f454c46 || fail 'ELF object emission did not begin with ELF magic'

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

if test "$(uname -s)" = Linux && test "$(uname -m)" = x86_64 &&
    command -v as >/dev/null 2>&1 &&
    command -v objcopy >/dev/null 2>&1 &&
    command -v ld >/dev/null 2>&1 &&
    command -v readelf >/dev/null 2>&1 &&
    as --version 2>/dev/null | grep -Fq 'GNU assembler'; then
    return_executable="$temporary_directory/return-42"
    sum_executable="$temporary_directory/sum-to"
    operations_executable="$temporary_directory/operations"
    sum_via_assembly="$temporary_directory/sum-to-via-assembly"
    sum_via_objcopy="$temporary_directory/sum-to-via-objcopy"
    linker_only_executable="$temporary_directory/return-42-linker-only"
    adapter_directory="$temporary_directory/linker-only-tools"
    preserved_output="$temporary_directory/preserved-output"
    emitted_object="$temporary_directory/return-42.o"

    printf 'keep this output\n' >"$preserved_output"
    expect_status 1 "$compiler" build \
        examples/invalid/type-mismatch.study -o "$preserved_output"
    test "$(cat "$preserved_output")" = 'keep this output' || \
        fail 'failed build changed the earlier output'

    expect_status 1 env PATH=/nonexistent "$compiler" build \
        examples/return-42.study -o "$preserved_output"
    grep -Fq 'system ld adapter exited with status 127' \
        "$temporary_directory/stderr" || \
        fail 'missing linker adapter was not reported'
    test "$(cat "$preserved_output")" = 'keep this output' || \
        fail 'linker adapter failure changed the earlier output'

    mkdir "$adapter_directory"
    ln -s "$(command -v ld)" "$adapter_directory/ld"
    expect_status 0 env PATH="$adapter_directory" "$compiler" build \
        examples/return-42.study -o "$linker_only_executable"
    expect_status 42 "$linker_only_executable"

    expect_status 1 env PATH=/nonexistent "$compiler" build via-objcopy \
        examples/return-42.study -o "$preserved_output"
    grep -Fq 'system objcopy adapter exited with status 127' \
        "$temporary_directory/stderr" || \
        fail 'missing reference object wrapper was not reported'

    expect_status 1 env PATH=/nonexistent "$compiler" build via-asm \
        examples/return-42.study -o "$preserved_output"
    grep -Fq 'system as adapter exited with status 127' \
        "$temporary_directory/stderr" || \
        fail 'missing reference assembler was not reported'

    expect_status 0 "$compiler" build \
        examples/return-42.study -o "$return_executable"
    test -x "$return_executable" || \
        fail 'build did not produce an executable file'
    expect_status 42 "$return_executable"

    expect_status 0 "$compiler" build \
        examples/sum-to.study -o "$sum_executable"
    expect_status 0 "$sum_executable"
    test "$(cat "$temporary_directory/stdout")" = '55' || \
        fail 'native sum-to executable did not print 55'

    expect_status 0 "$compiler" build via-asm \
        examples/sum-to.study -o "$sum_via_assembly"
    expect_status 0 "$sum_via_assembly"
    test "$(cat "$temporary_directory/stdout")" = '55' || \
        fail 'reference assembly path did not print 55'

    expect_status 0 "$compiler" build via-objcopy \
        examples/sum-to.study -o "$sum_via_objcopy"
    expect_status 0 "$sum_via_objcopy"
    test "$(cat "$temporary_directory/stdout")" = '55' || \
        fail 'reference objcopy path did not print 55'

    "$compiler" emit obj examples/return-42.study >"$emitted_object"
    readelf -h -S -s -r "$emitted_object" >"$temporary_directory/object-elf"
    grep -Fq 'Type:                              REL (Relocatable file)' \
        "$temporary_directory/object-elf" || \
        fail 'our object is not ELF ET_REL'
    grep -Fq '.rela.text' "$temporary_directory/object-elf" || \
        fail 'our object has no .rela.text section'
    grep -Fq 'R_X86_64_PC32' "$temporary_directory/object-elf" || \
        fail 'our object has no PC-relative call relocation'
    grep -Fq '_start' "$temporary_directory/object-elf" || \
        fail 'our object has no _start symbol'

    readelf -h "$sum_executable" >"$temporary_directory/elf-header"
    grep -Fq 'Class:                             ELF64' \
        "$temporary_directory/elf-header" || \
        fail 'native output is not ELF64'
    grep -Fq 'Type:                              EXEC' \
        "$temporary_directory/elf-header" || \
        fail 'native output is not ET_EXEC'
    grep -Fq 'Machine:                           Advanced Micro Devices X86-64' \
        "$temporary_directory/elf-header" || \
        fail 'native output is not x86-64'
    if readelf -l "$sum_executable" | grep -Fq 'INTERP'; then
        fail 'native output unexpectedly requires a dynamic interpreter'
    fi

    expect_status 0 "$compiler" build \
        examples/operations.study -o "$operations_executable"
    expect_status 0 "$compiler" interpret examples/operations.study
    cp "$temporary_directory/stdout" \
        "$temporary_directory/operations-reference"
    expect_status 37 "$operations_executable"
    printf '21\n40\n-2147483648\n-7\n5\n1\n2\n3\n4\n5\n6\n' \
        >"$temporary_directory/operations-expected"
    cmp "$temporary_directory/operations-expected" \
        "$temporary_directory/stdout" || \
        fail 'native operations executable disagreed with expected output'
    cmp "$temporary_directory/operations-reference" \
        "$temporary_directory/stdout" || \
        fail 'native operations executable disagreed with the interpreter'
fi

printf 'driver tests passed\n'
