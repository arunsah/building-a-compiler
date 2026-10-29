# Study compiler

This directory contains the evolving compiler built for the
[Building a Compiler](https://arunsah.com/compiler) series. It is separate from
the `chapters/` directories, which retain experiments and recorded output for
the earlier Understanding C articles.

The compiler is written in C17. **Study**, `.study`, and `studycc` are working
names for the learning language, source files, and compiler command. The first
milestone targets static Linux x86-64 ELF executables.

## Current checkpoint: names, scopes, and types

The driver can show both the source metadata and the token stream:

```sh
make -C compiler
./compiler/build/studycc dump source compiler/examples/return-42.study
./compiler/build/studycc dump tokens compiler/examples/return-42.study
./compiler/build/studycc dump ast compiler/examples/return-42.study
./compiler/build/studycc dump checked compiler/examples/return-42.study
./compiler/build/studycc check compiler/examples/return-42.study
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

Typed IR, interpretation, and executable generation are not implemented yet.

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
signatures and argument lists, and conservative return-path analysis.

To retain a small environment and verification record in a new directory:

```sh
./compiler/record-checker-evidence.sh NEW_OUTPUT_DIRECTORY
```

The recorder uses a temporary build directory, runs the strict and sanitizer
tests, saves one sample observation, and writes checksums. Review
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
