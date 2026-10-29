# Typed-IR evidence

These records were generated on 29 October 2026 by
`compiler/record-ir-evidence.sh` from source revision
`63c8896c6ee5d325241bc2d2677c47cc41531ac7`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The typed IR, verifier, and reference interpreter build with strict warnings and pass their API, command, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. |

Each directory contains the recorded environment, normalized commands, pass
markers, complete IR dumps for `return-42.study` and `sum-to.study`, the output
from interpreting `sum-to.study`, and checksums. The IR dumps and program
output are identical across the two hosts.

These records establish the current lowering rules, typed values and slots,
explicit control flow, verifier boundary, and reference execution semantics
for the covered programs and tests. They do not establish native instruction
selection, object-file generation, linking, or executable behaviour. Those
belong to later compiler stages.

The Linux record was copied back from the temporary Debian VM directory after
the checks passed. The temporary VM source and evidence directory were then
removed.
