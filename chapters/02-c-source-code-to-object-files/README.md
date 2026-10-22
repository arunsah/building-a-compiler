# From C source code to object files

This directory contains the program and recorded outputs used in Chapter 2 of
the [Building a Compiler](https://arunsah.com/compiler) series.

The chapter follows one small Hello World program through preprocessing,
compilation, and assembly. It stops at the relocatable object file, before the
linker produces an executable.

## Program

[`hello.c`](hello.c) uses one system header, one macro, and one C library
function. This keeps the source familiar while giving us something useful to
observe at every stage.

Run the portable verification:

```sh
./verify.sh
```

Set `CC` to use another system compiler:

```sh
CC=clang ./verify.sh
CC=gcc ./verify.sh
```

## Reproducing the artifacts

The recording script supports macOS ARM64 and Linux x86-64. Give it the path of
a new output directory; it refuses to mix a new run with an existing snapshot.

```sh
./record-artifacts.sh /path/to/new-output-directory
```

The published snapshot was recorded on 22 October 2026:

| | Debian guest | Mac |
|---|---|---|
| Operating system | Debian 13.7 | macOS 26.6.2 |
| Architecture | x86-64 | ARM64 |
| C compiler | GCC 14.2.0 | Apple Clang 21.0.0 |
| Inspection tools | GNU Binutils 2.44 | Command Line Tools 26.6 |

## Recorded files

The [Linux x86-64](artifacts/2026-10-22/linux-x86-64) and
[macOS ARM64](artifacts/2026-10-22/macos-arm64) directories contain the same
kinds of files:

| File | Command | What it contains |
|---|---|---|
| `commands.txt` | Written by `record-artifacts.sh` | The compiler commands used for the snapshot |
| `hello.i` | `cc -std=c17 -Wall -Wextra -Wpedantic -Werror -E hello.c -o hello.i` | The complete preprocessed translation unit |
| `hello-O0.s` | `cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 -S hello.c -o hello-O0.s` | Assembly produced without optimisation |
| `hello-O2.s` | `cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O2 -S hello.c -o hello-O2.s` | Assembly produced with `-O2` for comparison |
| `hello.o` | `cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 -c hello.c -o hello.o` | The relocatable object file |
| `file-type.txt` | `file hello.o > file-type.txt` | The object format and target architecture |
| `file-header.txt` | Linux: `readelf --file-header hello.o > file-header.txt`; macOS: `otool -hv hello.o > file-header.txt` | The ELF or Mach-O file header |
| `sections.txt` | Linux: `readelf --sections hello.o > sections.txt`; macOS: `otool -l hello.o > sections.txt` | The sections stored in the object file |
| `symbols.txt` | `nm hello.o > symbols.txt` | Defined and undefined symbols |
| `relocations.txt` | Linux: `readelf --relocs hello.o > relocations.txt`; macOS: `otool -rv hello.o > relocations.txt` | References which still need the linker |
| `disassembly.txt` | Linux: `objdump --disassemble --reloc hello.o > disassembly.txt`; macOS: `otool -tvV hello.o > disassembly.txt` | Machine instructions decoded from `hello.o` |
| `checksums.sha256` | `sha256sum` on Linux; `shasum -a 256` on macOS | SHA-256 checksums for the recorded files |

`record-artifacts.sh` runs the checksum tool once for every file in the output
directory, skips `checksums.sha256` itself, and redirects the combined output
to that file. The loop is kept in the script because the list of recorded files
may grow as the chapter changes.

The preprocessed files contain line markers beginning with `#`. They preserve
the source filename and line number for diagnostics after headers and source
have been combined. The article explains the part we need; following every
system-header marker is not required for this chapter.
