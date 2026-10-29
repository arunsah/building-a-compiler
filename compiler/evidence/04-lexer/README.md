# Lexer and source-range evidence

These records were generated on 29 October 2026 by
`compiler/record-lexer-evidence.sh` from source revision
`394865032db0492dd8cf5e194c9c4d2313b68610`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The lexer builds with strict warnings and passes its API, command, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. |

Each directory contains the recorded environment, normalized commands, pass
markers, the complete `return-42.study` token dump, one rejected integer
literal with its diagnostic and exit status, and checksums. The token dump and
failure record are identical across the two hosts.

These records establish source-to-token behaviour for the current examples and
test suite. They do not establish that a token stream follows the grammar, has
valid names or types, or can be compiled. Those rules belong to later stages.

The Linux record was copied back from the temporary Debian VM directory after
the checks passed. The temporary source and evidence directory was then
removed.
