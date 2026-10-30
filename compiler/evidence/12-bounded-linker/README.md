# Bounded-linker and final executable evidence

These records were generated on 30 October 2026 by
`compiler/record-bounded-linker-evidence.sh` from source revision
`6f7fc04410662a1fdcd9eeead4de3a93993914d7`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The bounded linker, serialized-executable verifier, executable dump, and the rest of the compiler pass strict warning, command, API, AddressSanitizer, UndefinedBehaviorSanitizer, and Clang static-analysis checks on macOS Arm64. The compiler writes Linux x86-64 executable bytes there, but macOS does not try to run them. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. The default path builds without a system binary tool in `PATH`; GNU `readelf` and `objdump` independently inspect the result; and all three programs run natively. |

Each host record retains the executable-layout dump, fully relocated `.text`
image, complete executable bytes, interpreter output, normalized command list,
environment record, test markers, and checksums. The three executable files are
byte-for-byte identical across the two hosts. This establishes that the ELF
layout, final symbol addresses, relocation values, and executable bytes are
decisions made by our compiler rather than by the host which runs it.

The compiler writes static, non-PIE ELF64 x86-64 `ET_EXEC` files. Their fixed
layout has:

- an ELF header and two program headers at the beginning of the file;
- one read-and-execute `PT_LOAD` segment based at `0x400000`;
- `.text` at file offset `0x1000` and virtual address `0x401000`;
- `_start` at `0x401000`, used as the ELF entry address;
- a read-and-write, non-executable `PT_GNU_STACK` contract; and
- retained `.text`, `.symtab`, `.strtab`, `.shstrtab`, and null section headers
  for inspection.

The Linux loader needs the program headers and entry address. It does not need
the symbol table or section headers. We keep those non-loadable records because
they make this learning executable easier to inspect without changing what is
mapped into the process.

The three executables contain 4,864, 5,120, and 6,280 bytes. The linker applied
one, three, and twenty-one `R_X86_64_PC32` call relocations. The executable dump
shows every calculation as `S`, `A`, `P`, and the signed value written at the
relocation site. The final `.text` extracted by GNU `objcopy` is byte-for-byte
identical to the fully resolved image produced by our encoder.

On Linux, the recorder deliberately sets `PATH=/nonexistent` while invoking the
default `build` command. All three files are still produced and are identical
to the retained `emit exe` bytes. The separately named `build via-ld`, `build
via-objcopy`, and `build via-asm` paths remain reference checks; none is selected
silently.

The generated programs returned statuses 42, 0, and 37. Their output matched
the typed-IR interpreter and every reference path. `readelf` independently
reports an ELF64 little-endian x86-64 executable with entry `0x401000`, one RX
load segment, no interpreter segment, and a non-executable stack contract.
`objdump` connects the final virtual addresses to `_start`, the print runtime,
and each Study function.

This completes the first bounded native path, not a general-purpose linker. The
linker accepts only the exact single-`.text` object produced by this compiler.
It does not combine objects, search archives, resolve library symbols, write
dynamic-linking tables, create position-independent executables, or target a
different processor or operating system.

Both host directories were copied into this record only after their checksums
passed. The Linux checksums were verified again after the files were copied out
of the VM, and the executable checksum lists were then compared across hosts.
Temporary source trees, VM builds, and transfer archives are not part of this
retained evidence.
