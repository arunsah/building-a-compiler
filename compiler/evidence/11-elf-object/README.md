# ELF64 object-writer evidence

These records were generated on 30 October 2026 by
`compiler/record-elf-object-evidence.sh` from source revision
`330d379b90dd8a04532e52a25754b23026ff2a39`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The ELF64 writer, serialized-object verifier, symbol and relocation tests, object dump, and the rest of the compiler pass strict warning, command, API, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. Native Linux linking and execution are deliberately not attempted there. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. GNU `readelf` accepts the emitted object, GNU `ld` consumes its call relocations, and the resulting static executables run natively. |

Each host record retains the complete x86-64 target dump, instruction-encoding
map, flat code image, ELF object dump, serialized `.o` file, and rendered GNU
assembly for `return-42.study`, `sum-to.study`, and `operations.study`. All of
those files are byte-for-byte identical across the two hosts. The reference-
interpreter output is also identical. This establishes that the ELF layout,
function symbols, strings, and relocations are decisions made by our compiler,
not by the host which runs it.

The three ELF objects contain 1,016, 1,312, and 2,904 bytes. They contain seven
sections:

- executable `.text` bytes from our encoder;
- `.rela.text` call relocations;
- `.symtab` function symbols;
- `.strtab` function-name strings;
- `.shstrtab` section-name strings;
- an empty `.note.GNU-stack` marker; and
- the required null section.

The smallest object has one relocation: `_start` calls `study_fn_0` through an
`R_X86_64_PC32` RELA record at `.text+1` with addend `-4`. The loop program has
three call relocations. The operations program has twenty-one, including
recursive and runtime calls. Internal block jumps stay resolved inside the
encoded `.text` image.

The Linux record retains GNU `readelf` reports and `objdump -dr` output for
every object. These independently show an ELF64 little-endian x86-64 `ET_REL`
file, the expected section links and alignments, local runtime and Study
function symbols, a global `_start` symbol after the local symbols, and the
expected PC-relative relocations. The records are evidence about our object;
neither tool is an input to the writer.

On Linux, GNU `ld` linked each object without an assembler or `objcopy` in the
default path. The generated programs returned statuses 42, 0, and 37. Their
output matched the typed-IR interpreter, the earlier `objcopy` wrapper path,
and the GNU assembly path. The `.text` extracted from every linked executable
is byte-for-byte identical to the corresponding fully resolved flat image,
which also checks the relocation values applied by GNU `ld`.

The driver suite constructs a restricted `PATH` containing only GNU `ld`. The
default `build` command still produces and runs `return-42` in that environment.
The separately named `build via-objcopy` and `build via-asm` commands continue
to test the two older reference paths and report their missing adapters
separately.

This evidence does not claim that the compiler already links its object. GNU
`ld` still assigns the final virtual addresses, applies relocations, creates
program headers, and writes the executable. That is the next boundary to
replace.

Both host directories contain a normalized command list, environment record,
pass markers, and checksums. The Linux files were copied back only after their
checksums passed. Temporary source trees, build products, and VM records are
removed after the retained copy is verified.
