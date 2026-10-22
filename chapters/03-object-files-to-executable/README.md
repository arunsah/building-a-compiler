# From object files to an executable

This directory contains the program and recorded outputs used in Chapter 3 of
the [Building a Compiler](https://arunsah.com/compiler) series.

Chapter 2 stopped at a relocatable object file. This chapter begins with two
object files, follows their symbols and relocations through the linker, and
examines the executable that the linker produces.

## Program

The example is a small checked calculator split across three source files:

- [`main.c`](calculator/main.c) reads two integers from the command line, calls `add`, and
  prints the result.
- [`add.h`](calculator/add.h) declares the interface between the two translation units.
- [`add.c`](calculator/add.c) implements the addition and detects signed integer overflow.

Compile the two translation units separately and then link their object files:

```sh
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 \
    -c calculator/main.c -o main.o
cc -std=c17 -Wall -Wextra -Wpedantic -Werror -O0 \
    -c calculator/add.c -o add.o
cc main.o add.o -o calculator
./calculator 20 22
```

The expected output is:

```text
20 + 22 = 42
```

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

The published snapshot was recorded on 23 October 2026:

| | Debian guest | Mac |
|---|---|---|
| Operating system | Debian 13.7 | macOS 26.6.2 |
| Architecture | x86-64 | ARM64 |
| C compiler | GCC 14.2.0 | Apple Clang 21.0.0 |
| Linker | GNU ld 2.44 | Apple ld 1267 |
| Inspection tools | GNU Binutils 2.44 | Command Line Tools 26.6 |

## Recorded files

The complete snapshots are in the
[Linux x86-64](artifacts/2026-10-23/linux-x86-64) and
[macOS ARM64](artifacts/2026-10-23/macos-arm64) directories. Every text output
has its producing command in `commands.txt`.

Both snapshots contain:

| File | What it lets us inspect |
|---|---|
| `main.o`, `add.o` | The two relocatable object files before linking |
| `calculator` | The executable produced by linking the object files directly |
| `libcalculator.a` | A static library containing `add.o` |
| `object-symbols.txt` | What each object file defines and still needs |
| `missing-add-link.txt` | The expected linker error when `add.o` is omitted |
| `driver-link-command.txt` | The linker command and system inputs selected by the compiler driver |
| `main-relocations.txt`, `main-disassembly.txt` | The unresolved call to `add` before linking |
| `link-map.txt` | The final addresses and output layout of the direct-object link |
| `archive-contents.txt`, `archive-symbols.txt` | The object stored in `libcalculator.a` and its symbols |
| `archive-link-map.txt` | Why the linker selected `add.o` from the static library |
| `executable-header.txt`, `executable-symbols.txt` | The completed executable's header and concise symbol list |
| `executable-disassembly.txt` | The final call instruction and linked `add` implementation |
| `file-types.txt`, `file-sizes.txt` | The formats, architectures, and on-disk sizes of the main files |
| `program-output.txt` | One successful run used as a recording check |
| `checksums.sha256` | SHA-256 checksums for the snapshot |

The Linux snapshot also contains the ELF section table, program headers,
complete symbol table, final relocations, dynamic section, dynamic symbols,
and section sizes. The macOS snapshot contains the Mach-O load commands,
linked libraries, and symbols imported from those libraries.

The recording script builds and runs a second executable through
`libcalculator.a`, but does not keep that executable. Its link map contains the
useful difference: `add.o` was selected from the archive. Keeping another copy
of the executable would not add anything to the investigation.
