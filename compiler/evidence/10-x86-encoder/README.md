# x86-64 instruction-encoder evidence

These records were generated on 29 October 2026 by
`compiler/record-x86-encoder-evidence.sh` from source revision
`231c7812af23760b23209f97d221c73f0d8e1097`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The verified target-program-to-byte encoder, relative fixups, binary dump, and the rest of the compiler pass strict warning, command, API, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. Native Linux execution is deliberately not attempted there. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. The encoded image is wrapped with GNU `objcopy` and linked with GNU `ld`, without invoking the assembler on the default path, and then runs as a native static executable. |

Each host record retains the complete x86-64 target dump, encoding map, flat
binary image, and rendered GNU assembly for `return-42.study`, `sum-to.study`,
and `operations.study`. All of these files are byte-for-byte identical across
the two hosts. The reference-interpreter output is also identical. This
establishes that instruction encoding and relative fixups are deterministic
target decisions rather than behaviour selected by the host which runs
`studycc`.

The encoding map connects each byte range to its target function, block, and
instruction. It makes visible:

- function and basic-block offsets in the flat image;
- opcode bytes, REX prefixes, ModR/M and SIB bytes, immediates, and stack
  displacements;
- the two machine instructions produced by one target conditional branch;
- forward and backward `rel32` call, jump, and conditional-branch fixups; and
- the padding used to align each function to a 16-byte boundary.

The three flat images contain 280, 495, and 1,619 bytes respectively. The
Linux record retains an `objdump` disassembly of every image beside a
disassembly of the executable produced through the older GNU assembly path.
These are comparison records, not inputs to the encoder.

On Linux, the encoded programs returned statuses 42, 0, and 37. The loop and
operations output matched both the reference interpreter and the older
assembly path. The `.text` bytes extracted from each linked executable are
identical to the corresponding flat image, proving that the temporary object
wrapper and linker did not rewrite our instructions. The retained ELF reports
also show an ELF64 x86-64 static executable whose entry address names `_start`
and which has no dynamic interpreter.

This evidence does not claim that the compiler already writes an ELF object or
links one. GNU `objcopy` still gives the flat image an ELF `.text` section and
`_start` symbol, while GNU `ld` still lays out the executable. Those are the
next boundaries to replace. GNU `as` is used only by the separately named
`build via-asm` reference path.

Both host directories contain a normalized command list, environment record,
pass markers, and checksums. The Linux files were copied back only after their
checksums passed. Temporary source trees, build products, and VM records were
removed after the retained copy was verified.
