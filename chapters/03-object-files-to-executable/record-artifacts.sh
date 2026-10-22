#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi

article_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir="$article_dir/calculator"
output_parent=$(dirname -- "$1")
output_name=$(basename -- "$1")
mkdir -p "$output_parent"
output_parent=$(CDPATH= cd -- "$output_parent" && pwd)
output_dir="$output_parent/$output_name"

if [ -e "$output_dir" ]; then
    echo "Output already exists: $output_dir" >&2
    exit 1
fi

mkdir "$output_dir"

c_compiler=${CC:-cc}
archiver=${AR:-ar}
common_flags="-std=c17 -Wall -Wextra -Wpedantic -Werror -O0"
system_name=$(uname -s)
machine_name=$(uname -m)

case "$system_name:$machine_name" in
    Darwin:arm64)
        platform_name=macos-arm64
        map_option=-Wl,-map,link-map.txt
        archive_map_option=-Wl,-map,archive-link-map.txt
        ;;
    Linux:x86_64)
        platform_name=linux-x86-64
        map_option=-Wl,-Map,link-map.txt
        archive_map_option=-Wl,-Map,archive-link-map.txt
        ;;
    *)
        echo "Unsupported recording platform: $system_name $machine_name" >&2
        exit 1
        ;;
esac

cat >"$output_dir/commands.txt" <<EOF
$c_compiler $common_flags -c calculator/main.c -o main.o
$c_compiler $common_flags -c calculator/add.c -o add.o
$c_compiler main.o -o calculator-missing-add  # expected to fail
$c_compiler -### main.o add.o -o calculator
$c_compiler main.o add.o $map_option -o calculator
$archiver rcs libcalculator.a add.o
$c_compiler main.o libcalculator.a $archive_map_option -o calculator-from-archive
./calculator 20 22 > program-output.txt
file main.o add.o libcalculator.a calculator > file-types.txt
wc -c main.o add.o libcalculator.a calculator > file-sizes.txt
nm main.o add.o > object-symbols.txt
nm calculator > executable-symbols.txt
$archiver t libcalculator.a > archive-contents.txt
nm libcalculator.a > archive-symbols.txt
EOF

cd "$output_dir"

"$c_compiler" $common_flags -c "$source_dir/main.c" -o main.o
"$c_compiler" $common_flags -c "$source_dir/add.c" -o add.o

set +e
"$c_compiler" main.o -o calculator-missing-add >missing-add-link.txt 2>&1
missing_add_status=$?
set -e

if [ "$missing_add_status" -eq 0 ]; then
    echo "Linking main.o without add.o unexpectedly succeeded." >&2
    exit 1
fi

"$c_compiler" -### main.o add.o -o calculator >driver-link-command.txt 2>&1
"$c_compiler" main.o add.o "$map_option" -o calculator
"$archiver" rcs libcalculator.a add.o
"$c_compiler" main.o libcalculator.a "$archive_map_option" \
    -o calculator-from-archive

archive_output=$(./calculator-from-archive 20 22)
if [ "$archive_output" != "20 + 22 = 42" ]; then
    echo "The archive-linked executable produced unexpected output." >&2
    exit 1
fi
rm calculator-from-archive

./calculator 20 22 >program-output.txt

file main.o add.o libcalculator.a calculator >file-types.txt
wc -c main.o add.o libcalculator.a calculator >file-sizes.txt
nm main.o add.o >object-symbols.txt
nm calculator >executable-symbols.txt
"$archiver" t libcalculator.a >archive-contents.txt
nm libcalculator.a >archive-symbols.txt

case "$platform_name" in
    macos-arm64)
        cat >>commands.txt <<'EOF'
otool -rv main.o > main-relocations.txt
otool -tvV main.o > main-disassembly.txt
otool -hv calculator > executable-header.txt
otool -l calculator > executable-load-commands.txt
otool -tvV calculator > executable-disassembly.txt
otool -L calculator > dynamic-dependencies.txt
dyld_info -imports calculator > dynamic-imports.txt
EOF
        otool -rv main.o >main-relocations.txt
        otool -tvV main.o >main-disassembly.txt
        otool -hv calculator >executable-header.txt
        otool -l calculator >executable-load-commands.txt
        otool -tvV calculator >executable-disassembly.txt
        otool -L calculator >dynamic-dependencies.txt
        dyld_info -imports calculator >dynamic-imports.txt

        for artifact in *; do
            [ "$artifact" = checksums.sha256 ] && continue
            shasum -a 256 "$artifact"
        done >checksums.sha256
        ;;
    linux-x86-64)
        cat >>commands.txt <<'EOF'
readelf --relocs --wide main.o > main-relocations.txt
objdump --disassemble --reloc main.o > main-disassembly.txt
readelf --file-header calculator > executable-header.txt
readelf --sections --wide calculator > executable-sections.txt
readelf --segments --wide calculator > executable-segments.txt
readelf --symbols --wide calculator > executable-symbol-table.txt
readelf --relocs --wide calculator > executable-relocations.txt
readelf --dynamic --wide calculator > dynamic-section.txt
readelf --dyn-syms --wide calculator > dynamic-symbols.txt
objdump --disassemble calculator > executable-disassembly.txt
size main.o add.o calculator > section-sizes.txt
EOF
        readelf --relocs --wide main.o >main-relocations.txt
        objdump --disassemble --reloc main.o >main-disassembly.txt
        readelf --file-header calculator >executable-header.txt
        readelf --sections --wide calculator >executable-sections.txt
        readelf --segments --wide calculator >executable-segments.txt
        readelf --symbols --wide calculator >executable-symbol-table.txt
        readelf --relocs --wide calculator >executable-relocations.txt
        readelf --dynamic --wide calculator >dynamic-section.txt
        readelf --dyn-syms --wide calculator >dynamic-symbols.txt
        objdump --disassemble calculator >executable-disassembly.txt
        size main.o add.o calculator >section-sizes.txt

        for artifact in *; do
            [ "$artifact" = checksums.sha256 ] && continue
            sha256sum "$artifact"
        done >checksums.sha256
        ;;
esac

printf 'Recorded %s artifacts in %s\n' "$platform_name" "$output_dir"
