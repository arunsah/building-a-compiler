# Driver and source ownership evidence

These records were generated on 29 October 2026 by
`compiler/record-driver-evidence.sh` from source revision
`98551840bfb78f5c1c72a21eb3fd12314e12a1cd`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The compiler host code builds with strict warnings and passes its API, command, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. |
| `linux-x86-64/` | The same source passes those checks using GCC on Debian Linux x86-64. |

Each directory contains the recorded environment, normalized commands, pass
markers, one `dump source` result, and checksums. The records establish the
host-side driver and source ownership boundary only. They are not evidence of
tokenisation, parsing, code generation, or native execution of a generated
Study program.

The Linux record was copied back from the temporary Debian VM directory after
the checks passed. The temporary directory was then removed.
