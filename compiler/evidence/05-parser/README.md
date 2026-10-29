# Parser and syntax-tree evidence

These records were generated on 29 October 2026 by
`compiler/record-parser-evidence.sh` from source revision
`0325622bd33f26d4391587589a5e6baedc38b1a6`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The parser builds with strict warnings and passes its API, command, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. |

Each directory contains the recorded environment, normalized commands, pass
markers, the complete `return-42.study` syntax-tree dump, one missing-semicolon
diagnostic with its exit status, and checksums. The syntax tree and failure
record are identical across the two hosts.

These records establish token-to-syntax behaviour for the current grammar,
examples, and test suite. They do not establish that names are declared, types
match, every function returns, or a native executable can be generated. Those
rules belong to later stages.

The Linux record was copied back from the temporary Debian VM directory after
the checks passed. The temporary source and evidence directory was then
removed.
