# First native-path evidence

These records were generated on 29 October 2026 by
`compiler/record-system-tool-path-evidence.sh` from source revision
`32b47ae443526be35bead57398d7f2a8a10e32c1`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The compiler, typed IR, reference interpreter, and GNU x86-64 assembly emitter build with strict warnings and pass their command, API, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. Native Linux execution is deliberately not attempted on this host. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. The temporary GNU assembler and linker adapters also build and run the representative programs as static ELF64 x86-64 executables. |

The complete assembly for `return-42.study` and `sum-to.study` is retained in
both host directories. Those files are byte-for-byte identical across the two
hosts. The interpreter output for `operations.study` is also identical. This
shows that assembly emission is a compiler transformation, rather than a
different result chosen by the host on which `studycc` happens to run.

The Linux record additionally retains:

- native output and process status for the return-only, loop, and operations
  programs;
- a comparison between interpreter output and native output;
- the `file` description, ELF header, program headers, section headers, and
  symbol table for the `sum-to` executable; and
- a complete `objdump` disassembly of that executable.

The native programs returned statuses 42, 0, and 37 as expected. The loop and
operations output matched the reference interpreter. The inspected `sum-to`
file is a static ELF64 `ET_EXEC` file for x86-64 and has no `INTERP` program
header, so it does not require a dynamic loader. Each host directory contains
its environment, normalized command list, pass markers, and checksums.

This evidence establishes the first complete path from typed IR to a running
Linux x86-64 process. It does not establish that the compiler already owns the
whole binary pipeline. GNU `as` and `ld` remain explicit temporary adapters.
Instruction encoding, ELF object writing, and bounded linking still belong to
later checkpoints. The tests also cover only the current small language and
representative programs; they are not a claim about optimisation or other
processor and operating-system targets.

The Linux record was copied back from the temporary Debian VM after its
checksums passed. The temporary source, binaries, and evidence are not part of
the retained repository record.
