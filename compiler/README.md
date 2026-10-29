# Study compiler

This directory contains the evolving compiler built for the
[Building a Compiler](https://arunsah.com/compiler) series. It is separate from
the `chapters/` directories, which retain experiments and recorded output for
the earlier Understanding C articles.

The compiler is written in C17. **Study**, `.study`, and `studycc` are working
names for the learning language, source files, and compiler command. The first
milestone targets static Linux x86-64 ELF executables.

## Current checkpoint: driver and owned source bytes

The current command reads a source file and prints the metadata which the
driver observed:

```sh
make -C compiler
./compiler/build/studycc dump source compiler/examples/return-42.study
```

Expected shape:

```text
source: compiler/examples/return-42.study
bytes: 36
lines: 4
final line ending: yes
```

The exact byte count depends on the checked-in fixture and is verified by the
test suite. No tokenisation, parsing, checking, or executable generation is
implemented yet. The `dump source` command does not claim that the file is a
valid Study program.

## Build and verify

```sh
make -C compiler test
make -C compiler sanitize
```

The default build enables strict warnings and treats warnings as errors. The
tests cover command usage, missing files, empty input, paths containing spaces,
CRLF and bare-CR line endings, embedded NUL bytes, buffer growth, and the C
source-ownership API.

## Source ownership contract

`study_source_read()` reads bytes through a stream instead of assuming a
regular seekable file. On success, `StudySource` owns:

- a copy of the source identity used for diagnostics; and
- `length` source bytes followed by one convenience NUL byte.

The length remains authoritative because a source file may contain an embedded
NUL. A later lexer will reject bytes outside the language; the source reader's
job is to preserve them.

`study_source_copy()` provides the same ownership boundary for a caller which
already has source bytes in memory. The result remains valid after the caller
changes or releases its input buffers.

Both producers require an empty, initialized output. On failure they leave it
empty and provide a diagnostic. `study_source_free()` is safe for an empty or
already-freed value.

Diagnostics own their message text inside a fixed-size record and borrow their
path. A file-open diagnostic borrows the caller's path; later source
diagnostics will borrow `StudySource.path`. The driver prints a diagnostic
before releasing the corresponding owner.

## Exit status

| Status | Meaning |
| --- | --- |
| `0` | The requested compiler action succeeded |
| `1` | An input, source, or I/O error prevented the action |
| `2` | The command-line usage was incorrect |

A compiler status does not report the result of a generated program. Building
and running a program will remain separate actions.

## Directory layout

```text
include/study/   public C interfaces between implementation modules
src/             compiler implementation and command-line driver
tests/           C API and command-level checks
examples/        source programs which grow with the language
build/           ignored local binaries and objects
```

The compiler will remain one evolving implementation. Reviewed article
checkpoints are recorded by Git history rather than by copying the complete
source tree for every chapter.
