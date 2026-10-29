# Study compiler

This directory contains the evolving compiler built for the
[Building a Compiler](https://arunsah.com/compiler) series. It is separate from
the `chapters/` directories, which retain experiments and recorded output for
the earlier Understanding C articles.

The compiler is written in C17. **Study**, `.study`, and `studycc` are working
names for the learning language, source files, and compiler command. The first
milestone targets static Linux x86-64 ELF executables.

## Current checkpoint: first native path through system tools

The driver can show both the source metadata and the token stream:

```sh
make -C compiler
./compiler/build/studycc dump source compiler/examples/return-42.study
./compiler/build/studycc dump tokens compiler/examples/return-42.study
./compiler/build/studycc dump ast compiler/examples/return-42.study
./compiler/build/studycc dump checked compiler/examples/return-42.study
./compiler/build/studycc dump ir compiler/examples/return-42.study
./compiler/build/studycc emit asm compiler/examples/return-42.study
./compiler/build/studycc check compiler/examples/return-42.study
./compiler/build/studycc interpret compiler/examples/sum-to.study
```

On Debian Linux x86-64 with GNU `as` and `ld`:

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

`emit asm` lowers the verified IR directly to GNU assembler text for the first
Linux x86-64 System V path. Every mutable slot and temporary value receives a
simple stack location. Generated functions use 16-byte-aligned frames, the
first six integer argument registers, explicit block labels, and 32-bit
operations with the same wraparound and signed-comparison behaviour as the
interpreter.

The assembly includes `_start` and a small `print(i32)` runtime. `_start` calls
Study `main` and invokes the Linux `exit` system call with its result. The
runtime converts the complete signed value to decimal bytes and writes them
through the Linux `write` system call. It does not use libc or a dynamic
loader.

`build` writes that assembly to a temporary directory, invokes the system GNU
assembler and linker through separate adapter functions, and renames a
successful executable into place. It never constructs a shell command. A
failed compilation leaves an earlier output unchanged. This command is
expected to fail clearly on a host such as macOS where `as` and `ld` do not
provide the required Linux ELF target.

This is the first native vertical slice, not the final backend. The system
assembler and linker remain visible temporary dependencies. Later checkpoints
will add a target representation and replace them with our own x86-64 encoder,
ELF writer, and bounded linker.

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
the `main` result, assembly selection, and deliberately corrupted IR rejected
by the verifier. On Linux x86-64 with GNU binutils, the command tests also
build and run static executables, compare native output with the interpreter,
check non-zero Study results, and inspect the ELF header and program headers.

To retain a small environment and verification record in a new directory:

```sh
./compiler/record-ir-evidence.sh NEW_OUTPUT_DIRECTORY
```

The recorder uses a temporary build directory, runs the strict and sanitizer
tests, saves one IR dump and one interpreted result, and writes checksums. Review
`environment.txt` before committing it; environment records must not contain a
private hostname, account identifier, credential, or token.

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

## Temporary native-build contract

`study_x86_64_emit_assembly()` verifies the IR before writing target assembly.
The emitted functions use numeric labels rather than source identifiers, so
their symbols remain valid even as the source language later accepts richer
names. A stack layout is private to one emitted function. Slots and values from
another function cannot receive an address in that frame.

`study_build_with_system_tools()` owns only its temporary directory and paths.
The IR and source remain borrowed. The GNU assembler creates one temporary ELF
object, and GNU `ld` links that object as a static x86-64 executable. The
requested output path is replaced only after both adapters succeed. Temporary
assembly, object, and unfinished output files are removed on every return
path.

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
