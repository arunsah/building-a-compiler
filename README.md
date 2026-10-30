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

The [`compiler/`](compiler) directory contains the one evolving compiler built
through the series. We mark reviewed chapter checkpoints in Git history
instead of copying the complete compiler into a new directory for every
chapter.

## Current articles

- Understanding C, Article 2: [From C source code to object files](chapters/02-c-source-code-to-object-files)
- Understanding C, Article 3: [From object files to an executable](chapters/03-object-files-to-executable)

## Current compiler checkpoint

The first bounded native path is complete. `studycc` owns source bytes, tokens,
syntax, semantic checks, typed IR, x86-64 instruction selection and encoding,
the ELF relocatable object, final addresses and relocations, and the static ELF
executable. The default build does not invoke a system assembler, object
wrapper, linker, C library, or dynamic loader. Explicit GNU-tool paths remain
available only as references for comparison.

```sh
make -C compiler test
make -C compiler sanitize
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
./compiler/build/studycc emit obj compiler/examples/return-42.study > /tmp/return-42.o
./compiler/build/studycc emit exe compiler/examples/return-42.study > /tmp/return-42
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
