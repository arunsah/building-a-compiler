#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 OUTPUT_DIRECTORY" >&2
    exit 2
fi

chapter_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
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
common_flags="-std=c17 -Wall -Wextra -Wpedantic -Werror"
system_name=$(uname -s)
machine_name=$(uname -m)

case "$system_name:$machine_name" in
    Darwin:arm64)
        platform_name=macos-arm64
        ;;
    Linux:x86_64)
        platform_name=linux-x86-64
        ;;
    *)
        echo "Unsupported recording platform: $system_name $machine_name" >&2
        exit 1
        ;;
esac

cat >"$output_dir/commands.txt" <<EOF
$c_compiler $common_flags -E hello.c -o hello.i
$c_compiler $common_flags -O0 -S hello.c -o hello-O0.s
$c_compiler $common_flags -O2 -S hello.c -o hello-O2.s
$c_compiler $common_flags -O0 -c hello.c -o hello.o
EOF

cd "$chapter_dir"

"$c_compiler" $common_flags -E hello.c -o "$output_dir/hello.i"
"$c_compiler" $common_flags -O0 -S hello.c -o "$output_dir/hello-O0.s"
"$c_compiler" $common_flags -O2 -S hello.c -o "$output_dir/hello-O2.s"
"$c_compiler" $common_flags -O0 -c hello.c -o "$output_dir/hello.o"

(
    cd "$output_dir"
    file hello.o
) >"$output_dir/file-type.txt"
(
    cd "$output_dir"
    nm hello.o
) >"$output_dir/symbols.txt"

case "$platform_name" in
    macos-arm64)
        (
            cd "$output_dir"
            otool -hv hello.o
        ) >"$output_dir/file-header.txt"
        (
            cd "$output_dir"
            otool -l hello.o
        ) >"$output_dir/sections.txt"
        (
            cd "$output_dir"
            otool -rv hello.o
        ) >"$output_dir/relocations.txt"
        (
            cd "$output_dir"
            otool -tvV hello.o
        ) >"$output_dir/disassembly.txt"
        (
            cd "$output_dir"
            for artifact in *; do
                [ "$artifact" = checksums.sha256 ] && continue
                shasum -a 256 "$artifact"
            done
        ) >"$output_dir/checksums.sha256"
        ;;
    linux-x86-64)
        (
            cd "$output_dir"
            readelf --file-header hello.o
        ) >"$output_dir/file-header.txt"
        (
            cd "$output_dir"
            readelf --sections hello.o
        ) >"$output_dir/sections.txt"
        (
            cd "$output_dir"
            readelf --relocs hello.o
        ) >"$output_dir/relocations.txt"
        (
            cd "$output_dir"
            objdump --disassemble --reloc hello.o
        ) >"$output_dir/disassembly.txt"
        (
            cd "$output_dir"
            for artifact in *; do
                [ "$artifact" = checksums.sha256 ] && continue
                sha256sum "$artifact"
            done
        ) >"$output_dir/checksums.sha256"
        ;;
esac

printf 'Recorded %s artifacts in %s\n' "$platform_name" "$output_dir"
