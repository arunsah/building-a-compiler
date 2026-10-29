# Building a Compiler

This repository began as the supporting repository for the
[Building a Compiler](https://arunsah.com/compiler) series. The initial
investigation of the existing C toolchain now belongs to the independent
[Understanding C](https://arunsah.com/c) series. The repository keeps the
source code, experiments, and recorded outputs used by those articles.

The `chapters/` directories contain the programs, verification scripts, and
recorded files used by the earlier toolchain articles. This keeps their
complete evidence available without placing large compiler output inside the
article.

The new [`compiler/`](compiler) directory contains the one evolving compiler
which we will build through the rest of the series. We mark reviewed chapter
checkpoints in Git history instead of copying the complete compiler into a new
directory for every chapter.

## Current articles

- Understanding C, Article 2: [From C source code to object files](chapters/02-c-source-code-to-object-files)
- Understanding C, Article 3: [From object files to an executable](chapters/03-object-files-to-executable)

## Current compiler checkpoint

The compiler now establishes the `studycc` driver and owned source-byte
boundary, turns the source into tokens with exact ranges, and parses the
complete first grammar into a syntax tree. It now also resolves function and
local names, checks expression types and calls, and verifies return paths. It
now lowers the checked program into typed basic blocks, verifies that IR, and
executes it with a small reference interpreter. It can now also emit GNU
x86-64 assembly and, on Debian Linux x86-64, produce a static executable
through explicitly named system assembler and linker adapters. Those adapters
are temporary and are not the final compiler path.

```sh
make -C compiler test
make -C compiler sanitize
./compiler/build/studycc dump source compiler/examples/return-42.study
./compiler/build/studycc dump tokens compiler/examples/return-42.study
./compiler/build/studycc dump ast compiler/examples/return-42.study
./compiler/build/studycc dump checked compiler/examples/return-42.study
./compiler/build/studycc dump ir compiler/examples/return-42.study
./compiler/build/studycc emit asm compiler/examples/return-42.study
./compiler/build/studycc check compiler/examples/return-42.study
./compiler/build/studycc interpret compiler/examples/sum-to.study
./compiler/build/studycc build compiler/examples/sum-to.study -o /tmp/sum-to
```

See the [compiler README](compiler/README.md) for the ownership contract,
directory layout, exit statuses, and current limits.

## Working on an article

Work for an article is prepared on its own branch. The implementation, article
examples, and recorded output are reviewed together. When the article is
ready, its branch can be squash-merged so that `main` has one clear checkpoint
for that article.

## Licence

The source code in this repository is available under the [MIT Licence](LICENSE).
