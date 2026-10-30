# Study compiler

This directory contains the evolving compiler built for the
[Building a Compiler](https://arunsah.com/compiler) series. It is separate from
the `chapters/` directories, which retain experiments and recorded output for
the earlier Understanding C articles.

The compiler is written in C17. **Study**, `.study`, and `studycc` are working
names for the learning language, source files, and compiler command. The first
milestone targets static Linux x86-64 ELF executables.

## Current checkpoint: a complete bounded native path

The current commands expose every representation and the default and reference
execution paths:

```sh
make -C compiler
./compiler/build/studycc dump source compiler/examples/return-42.study
./compiler/build/studycc dump tokens compiler/examples/return-42.study
./compiler/build/studycc dump ast compiler/examples/return-42.study
./compiler/build/studycc dump checked compiler/examples/return-42.study
./compiler/build/studycc dump ir compiler/examples/return-42.study
./compiler/build/studycc dump x86-64 compiler/examples/return-42.study
./compiler/build/studycc dump encoding compiler/examples/return-42.study
./compiler/build/studycc dump elf compiler/examples/return-42.study
./compiler/build/studycc dump executable compiler/examples/return-42.study
./compiler/build/studycc emit asm compiler/examples/return-42.study
./compiler/build/studycc emit bin compiler/examples/return-42.study > /tmp/return-42.bin
./compiler/build/studycc emit obj compiler/examples/return-42.study > /tmp/return-42.o
./compiler/build/studycc emit exe compiler/examples/return-42.study > /tmp/return-42
./compiler/build/studycc check compiler/examples/return-42.study
./compiler/build/studycc interpret compiler/examples/sum-to.study
```

The default build does not require an assembler, object wrapper, linker, C
library, or another system binary tool:

```sh
./compiler/build/studycc build compiler/examples/sum-to.study -o /tmp/sum-to
/tmp/sum-to
```

Expected shape:

```text
source: compiler/examples/return-42.study
bytes: 36
lines: 4
final line ending: yes
```

The token dump gives each token an exclusive byte range and a one-based line
and byte-column range. Integer tokens also carry their checked numeric value.
The lexer recognises the keywords and punctuation in the first Study grammar,
ASCII identifiers, decimal integers, spaces, tabs, common line endings, and
`//` line comments. It rejects unsupported bytes and decimal integers above
`2147483647` at their source location.

`dump ast` parses the complete first grammar: functions and parameters,
blocks, variable declarations and assignments, conditions, loops, returns,
printing, calls, grouping, unary expressions, and precedence-ordered binary
expressions. The tree keeps source ranges on every node.

`check` collects every function signature before checking any body, resolves
locals and parameters through lexical block scopes, annotates expressions with
`i32` or `bool`, validates operators, assignments, calls, conditions, printing
and return values, and conservatively checks that every function path returns.
`dump checked` makes those types and declaration links visible beside the AST.

`dump ir` lowers the checked program into typed values, mutable slots, and
basic blocks with explicit branch, jump, and return terminators. Every operand
value is defined earlier in the same block; values which must survive between
blocks are stored in typed slots. The independent verifier checks owners,
types, instruction chains, terminators, call signatures, control-flow targets,
reachability, and the `main` boundary without consulting the syntax tree.

`interpret` verifies the IR and executes `main`. It writes only the Study
program's `print` output. The command still returns the compiler's success or
failure status; the interpreter API returns the exact `i32` result separately.
Arithmetic uses unsigned C operations to implement Study's 32-bit wraparound
rules without relying on signed C overflow.

`dump x86-64` shows the next representation after typed IR. It contains the
process entry point, the small print runtime, Study functions, basic blocks,
selected x86-64 instructions, System V argument registers, and every stack
location. The first storage policy is deliberately simple: each mutable slot
and each temporary value receives its own eight-byte position in a
16-byte-aligned frame, although the stored Study value is still 32 bits wide.

The x86-64 lowerer builds this representation in memory and an independent
target verifier checks function and block ownership, explicit terminators,
branch and call targets, operand kinds and widths, stack-frame bounds, and the
runtime entry functions. `emit asm` then renders the verified target program
as GNU assembler text. It does not select instructions or decide storage while
printing text. This separation matters because our own encoder can consume the
same target program.

`dump encoding` shows where every target function, block, and instruction
begins in the encoded image. Each instruction line includes the bytes emitted
for that target instruction. Calls and branches are emitted with four-byte
placeholders and patched only after every target offset is known. `emit bin`
writes the same flat code image without a textual or object-file wrapper.

The encoder owns opcode bytes, REX prefixes, ModR/M and SIB bytes, immediates,
displacements, function alignment, and signed `rel32` branch fixups. The image
keeps function, block, and instruction offsets so byte-level failures can be
connected to the verified target program.

`dump elf` shows the next representation: a complete little-endian ELF64
x86-64 relocatable object. The writer owns the ELF header, `.text`,
`.rela.text`, `.symtab`, `.strtab`, `.shstrtab`, the non-executable-stack
marker, section headers, function symbols, and `R_X86_64_PC32` call
relocations. Internal block branches stay resolved inside `.text`. Calls use
zero placeholders in the object and name their target function through a RELA
record with addend `-4`.

`emit obj` writes that verified object without using an object-file tool. The
object verifier reads the serialized headers and tables back from the byte
array, checks their links and ranges, compares `.text` with the encoded image
apart from relocation fields, and checks every function symbol and call
relocation before the object can be published.

`dump executable` shows the final static ELF64 x86-64 file. The bounded linker
accepts only the exact single-`.text` object shape produced by this compiler.
It assigns `.text` the virtual address `0x401000`, gives every function symbol
its final address, applies every `R_X86_64_PC32` call relocation, chooses
`_start` as the entry, and writes an RX `PT_LOAD` segment together with a
non-executable `PT_GNU_STACK` contract. `emit exe` writes those verified bytes
without invoking GNU `ld`.

The executable keeps `.text`, `.symtab`, `.strtab`, and `.shstrtab` sections so
that the final code and function addresses remain inspectable. The Linux loader
uses the program headers rather than these section headers to create the
process image.

The assembly includes `_start` and a small `print(i32)` runtime. `_start` calls
Study `main` and invokes the Linux `exit` system call with its result. The
runtime converts the complete signed value to decimal bytes and writes them
through the Linux `write` system call. It does not use libc or a dynamic
loader.

`build` now writes and atomically publishes our complete executable. The GNU
linker path remains available as `build via-ld`; the earlier binary-wrapper and
assembly paths remain available as `build via-objcopy` and `build via-asm`.
No reference path is selected silently. The default path works with an empty
`PATH`. Reference paths invoke tools through argument arrays rather than shell
commands. Every path renames a successful executable into place only after all
of its steps succeed, so a failed compilation leaves an earlier output
unchanged.

This completes the first bounded native path. It is not a general linker. It
does not combine multiple objects, search archives, resolve undefined library
symbols, create dynamic-linking tables, or support another object format or
processor. Those are later extensions rather than hidden parts of this
milestone.

## Build and verify

```sh
make -C compiler test
make -C compiler sanitize
```

The default build enables strict warnings and treats warnings as errors. The
tests cover command usage, source ownership, empty input, paths containing
spaces, CRLF and bare-CR line endings, comments, every current keyword and
operator, longest operator matches, integer bounds, unsupported bytes, token
array ownership, source ranges, every current statement and expression shape,
operator precedence and associativity, malformed grammar, and bounded syntax
nesting. Semantic tests additionally cover forward calls, block shadowing,
same-scope duplicates, unknown names, every current type rule, function
signatures and argument lists, and conservative return-path analysis. IR and
interpreter tests cover lowering, typed values and slots, branches, loops,
calls, recursion, signed comparisons, wraparound arithmetic, program output,
the `main` result, target instruction selection and stack storage, assembly
rendering, instruction encoding, relative fixups, ELF sections, strings,
symbols, relocations, executable segments, relocation application, and
deliberately corrupted IR, target programs, ELF objects, and executables
rejected by their verifiers. On Linux x86-64 with GNU binutils, the command
tests independently inspect our object and executable, run the tool-free
default path and all three reference paths, compare native output with the
interpreter, and check non-zero Study results.

To retain the current environment and bounded-linker verification record in a
new directory:

```sh
./compiler/record-bounded-linker-evidence.sh NEW_OUTPUT_DIRECTORY
```

The recorder uses temporary build directories, runs the strict and sanitizer
tests, retains the executable dump and bytes for all three representative
programs, performs the native and reference checks when running on Linux
x86-64, and writes checksums. Review `environment.txt` before committing it;
environment records must not contain a private hostname, account identifier,
credential, or token.

The earlier driver records remain under
[`evidence/03-driver-source`](evidence/03-driver-source). The current macOS
Arm64 and Debian Linux x86-64 records are under
[`evidence/04-lexer`](evidence/04-lexer).

The current parser records are under
[`evidence/05-parser`](evidence/05-parser).

The current semantic-checker records are under
[`evidence/06-checker`](evidence/06-checker).

The current typed-IR and reference-interpreter records are under
[`evidence/07-typed-ir`](evidence/07-typed-ir).

The first native system-tool path records are under
[`evidence/08-system-tool-path`](evidence/08-system-tool-path).

The x86-64 target-lowering and stack-layout records are under
[`evidence/09-x86-target-ir`](evidence/09-x86-target-ir).

The first instruction-encoding and relative-fixup records are under
[`evidence/10-x86-encoder`](evidence/10-x86-encoder).

The ELF64 relocatable-object and call-relocation records are under
[`evidence/11-elf-object`](evidence/11-elf-object).

The bounded-linker and final static-executable records are under
[`evidence/12-bounded-linker`](evidence/12-bounded-linker).

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

## Token ownership contract

`study_lex()` borrows an immutable `StudySource` and writes into an initialized
empty `StudyTokenList`. On success, the list owns its dynamic token array. Each
token records a range into the source rather than copying its spelling, so the
source must outlive every token consumer.

On failure, the output list remains empty and the diagnostic borrows the
source path. The lexer may allocate a temporary list while scanning, but it
does not publish a partial token stream. `study_token_list_free()` releases an
owned list and restores it to the empty state.

Token positions use zero-based byte offsets and one-based lines and columns.
Columns count source bytes rather than terminal display cells. CRLF consumes
two bytes but advances the line only once. Every range has an exclusive end,
and the EOF token has an empty range at the final source position.

## Syntax-tree ownership contract

`study_parse()` borrows the source and complete token list and writes into an
initialized empty `StudySyntaxTree`. On success, the tree owns one dynamic node
array. Its name fields are token indices rather than copied spellings, so both
the token list and source must remain alive while the tree is used.

Nodes refer to children by stable numeric indices. Each parent keeps the first
and last child, and siblings form an ordered chain. The order carries roles:
for example, an `if` node contains its condition, then block, and optional else
block in that order. Every node also keeps the complete source range which
formed it.

The parser builds a temporary tree and publishes it only after reaching the
EOF token. On the first grammar error it reports the current token, frees every
temporary node, and leaves the caller's output empty. Recursive expression,
call, and block nesting is bounded to 256 levels so malformed input cannot ask
the C call stack to grow without limit.

## Checked-program ownership contract

`study_check_program()` borrows the source, token list, and syntax tree. On
success, `StudyCheckedProgram` owns one annotation for every syntax node and a
declaration array. An annotation may record an expression type, a resolved
declaration ID, and whether a statement or block definitely returns.

Names in declarations remain token indices, and declaration annotations refer
to syntax-node IDs. The checked program therefore does not replace its inputs;
it adds semantic facts while borrowing their spelling and structure. The
driver releases the checked program before the syntax tree, tokens, and
source.

The checker builds temporary declaration and scope tables. It publishes the
checked program only after every function body succeeds. A semantic error
frees those arrays and leaves the caller's output empty.

## Typed-IR ownership and verification contract

`study_lower_to_ir()` borrows the source, token list, syntax tree, and checked
program. On success, `StudyIrProgram` owns five growable arrays: functions,
basic blocks, instructions, slots, and values. Function and slot names remain
token indices so the source and token list must outlive an IR dump. Execution
uses resolved numeric IDs and does not need name spelling.

Instructions within a block form one linked sequence. Each block ends in
exactly one branch, jump, or return. Result values are immutable temporaries
and may be used only after their definition in the same block. Mutable source
variables and parameters use typed slots, which make values available across
control-flow edges without adding SSA phi nodes to this first IR.

The lowerer builds a temporary program, runs `study_ir_verify()`, and publishes
the result only after verification succeeds. The verifier does not consult the
syntax tree or checked annotations. This makes it a boundary check for every
later stage which consumes or transforms IR rather than only an assertion that
the lowerer probably emitted the right shape.

The reference interpreter verifies its input again. It limits nested calls to
256 and one interpretation to 1,000,000 executed IR instructions so recursive
or non-terminating input cannot consume the host without a stated boundary.
Those limits belong to the reference tool, not to the meaning of a future
native executable.

## x86-64 target ownership and verification contract

`study_lower_to_x86_64()` borrows verified typed IR and writes into an
initialised empty `StudyX86Program`. On success, the target program owns four
growable arrays: functions, basic blocks, instructions, and stack-location
records. Instructions use target function and block IDs rather than textual
labels. Study functions retain only the numeric IR function ID needed for a
stable symbol name.

Each basic block has one explicit terminator. Conditional branches carry both
successor blocks, so control flow does not depend on the order in which a text
printer happens to emit blocks. Calls point to target functions, including the
generated `_start` and `print(i32)` runtime boundary. Stack-location records
make the current storage decision inspectable instead of hiding offsets inside
the assembly renderer.

The lowerer builds a temporary target program, verifies it, and publishes it
only after the complete program succeeds. `study_x86_64_verify()` checks the
representation without consulting typed IR. The encoder and assembly renderer
can therefore trust the same boundary.

## x86-64 code-image ownership contract

`study_x86_64_encode()` borrows a verified target program and writes into an
initialised empty `StudyX86Image`. On success, the image owns the flat byte
array and offset arrays for every target function, block, and instruction.
Instruction sizes connect each byte range back to the target instruction which
produced it. The entry offset names the first byte of `_start`.

Calls, jumps, and the two machine instructions produced by one conditional
target branch use signed `rel32` displacements. The encoder records their
placeholder positions, emits the complete image, checks every target and
range, and then patches the little-endian displacements. It publishes no
partial image on failure. The image is position-independent within its own
byte array but does not yet contain ELF section, symbol, or relocation data.

## ELF64 object ownership and verification contract

`study_elf64_object_build()` borrows a verified target program and its matching
encoded image, and writes into an initialised empty `StudyElf64Object`. On
success the object owns one serialized byte array, function-to-symbol indices,
and an inspectable relocation record for every target call.

The writer emits one `ET_REL` file for little-endian x86-64. `.text` holds the
encoded image with zeroed call-displacement fields. Local symbols name the
runtime and Study functions; the global `_start` symbol is ordered after every
local symbol as required by the symbol-table boundary. Every call contributes
one `R_X86_64_PC32` RELA record whose offset points to its four-byte field and
whose addend is `-4`.

The writer builds and verifies a temporary object before publishing it. The
verifier checks the serialized ELF header, all seven section headers, table
ranges and links, string and section-name tables, symbol binding and function
ranges, `.text` bytes, and relocation records. A malformed object is rejected
without replacing the caller's earlier output.

## ELF64 executable ownership and verification contract

`study_elf64_executable_build()` borrows the verified object, target program,
and encoded image and writes into an initialised empty
`StudyElf64Executable`. The bounded linker reads the serialized object records,
requires every symbol to be defined in `.text`, and accepts only
`R_X86_64_PC32` RELA records with the explicit `-4` addend produced by our
object writer.

The linker copies `.text`, calculates `S + A - P` for every call, checks the
signed 32-bit result, and records each applied value. The resulting `ET_EXEC`
file has one RX load segment containing the ELF header, program headers, and
code. A zero-sized `PT_GNU_STACK` segment requests a readable and writable but
non-executable process stack. The executable also retains adjusted function
symbols and its section-name and symbol-name strings outside the loadable
segment.

The executable verifier reads the serialized header, program headers, section
headers, symbol table, strings, and relocated `.text` back from the byte array.
It independently recalculates every relocation and checks that `_start` names
the entry address. A failed link or verification does not publish a partial
file.

## Native-build contract

`study_x86_64_emit_assembly()` verifies the target program before writing GNU
assembly.
The emitted functions use numeric labels rather than source identifiers, so
their symbols remain valid even as the source language later accepts richer
names. A stack layout is private to one emitted function. Slots and values from
another function cannot receive an address in that frame.

`study_build_with_system_tools()` owns only its temporary directory and paths.
The target program and source remain borrowed. The GNU assembler creates one
temporary ELF object, and GNU `ld` links that object as a static x86-64
executable. The requested output path is replaced only after both adapters
succeed. Temporary assembly, object, and unfinished output files are removed
on every return path.

`study_build_with_elf_and_system_linker()` follows the same publication rule,
but writes our ELF object and invokes only GNU `ld`. It is now the explicitly
named `build via-ld` reference path. The objcopy and assembly routes remain
separate references as well.

`study_build_executable()` owns no temporary directory and invokes no external
tool. It builds the encoded image, object, and executable in memory; writes the
verified executable to a temporary file beside the requested output; marks it
executable; and publishes it by rename. An unfinished file is removed on every
failure path.

The generated runtime treats Linux `write` failure as a process failure with
status 74. The shell can observe only the platform's bounded process status,
so programs should use `print` when a complete signed `i32` must remain
visible.

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
