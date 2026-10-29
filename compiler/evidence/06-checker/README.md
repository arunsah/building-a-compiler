# Semantic-checker evidence

These records were generated on 29 October 2026 by
`compiler/record-checker-evidence.sh` from source revision
`604bc56a5acfa2b47b3b81ad91026a7f2dd1901c`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The semantic checker builds with strict warnings and passes its API, command, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. |

Each directory contains the recorded environment, normalized commands, pass
markers, the complete checked dump for `return-42.study`, one return-type
diagnostic with its exit status, and checksums. The checked dump and failure
record are identical across the two hosts.

These records establish name resolution, type checking, call validation, and
return-path checking for the current language and test suite. They do not
establish a typed intermediate representation, executable language semantics,
or native code generation. Those belong to later compiler stages.

The Linux record was copied back from the temporary Debian VM directory after
the checks passed. The temporary VM source and evidence directory was then
removed.
