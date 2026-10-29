# x86-64 target-program evidence

These records were generated on 29 October 2026 by
`compiler/record-x86-target-evidence.sh` from source revision
`2dfa383a06228899cf6e1b6637bf5372e076ad7f`.

| Record | What it establishes |
| --- | --- |
| `macos-arm64/` | The typed-IR-to-x86-64 lowerer, target verifier, target dump, assembly renderer, and the rest of the compiler pass strict warning, command, API, AddressSanitizer, and UndefinedBehaviorSanitizer checks using Apple Clang on macOS Arm64. Native Linux execution is deliberately not attempted there. |
| `linux-x86-64/` | The same committed source passes those checks using GCC on Debian Linux x86-64. GNU `as` and `ld`, which remain explicit temporary adapters, also turn the rendered assembly into native static executables. |

Each host record retains complete target dumps and rendered GNU assembly for
`return-42.study`, `sum-to.study`, and `operations.study`. The three target
dumps are byte-for-byte identical across the two hosts. The three assembly
files and the reference-interpreter output are also identical. This establishes
that instruction selection and stack layout are deterministic compiler
decisions rather than results chosen by the host running `studycc`.

The target dump makes the current backend decisions inspectable. It contains:

- `_start`, the no-libc `print(i32)` runtime, and every Study function;
- explicit basic blocks with two named successors for a conditional branch;
- System V argument and result registers;
- selected x86-64 operations and operand widths; and
- one visible stack location for every typed IR slot and value.

The Linux record additionally retains native output and process status for the
return-only, loop, and operations programs. Their statuses were 42, 0, and 37.
The loop and operations output matched the reference interpreter. The strict
Linux command suite also rechecked that the generated result is a static ELF64
x86-64 executable without a dynamic interpreter.

This evidence does not claim that the compiler already encodes instructions or
writes ELF files. The assembly renderer still writes text for GNU `as`, and GNU
`ld` still performs the final link. The next checkpoints will replace those
adapters while consuming this same verified target representation.

Both host directories contain the normalized command list, environment record,
pass markers, and checksums. The Linux files were copied back only after their
checksums passed. Temporary source trees, build products, and VM records were
removed after the retained copy was verified.
