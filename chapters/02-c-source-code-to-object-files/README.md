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

| File | What it contains |
|---|---|
| `commands.txt` | The compiler commands used for the snapshot |
| `hello.i` | The complete preprocessed translation unit |
| `hello-O0.s` | Assembly produced without optimisation |
| `hello-O2.s` | Assembly produced with `-O2` for comparison |
| `hello.o` | The relocatable object file |
| `file-type.txt` | The object format and target architecture |
| `file-header.txt` | The ELF or Mach-O file header |
| `sections.txt` | The sections stored in the object file |
| `symbols.txt` | Defined and undefined symbols |
| `relocations.txt` | References which still need the linker |
| `disassembly.txt` | Machine instructions decoded from `hello.o` |
| `checksums.sha256` | SHA-256 checksums for the recorded files |

The preprocessed files contain line markers beginning with `#`. They preserve
the source filename and line number for diagnostics after headers and source
have been combined. The article explains the part we need; following every
system-header marker is not required for this chapter.
