# Building a Compiler

This repository contains the source code, experiments, and recorded outputs
used in the [Building a Compiler](https://arunsah.com/compiler) series.

Each chapter has its own directory. The directory contains the programs used
in the article, a script which verifies them, and any generated files which the
article discusses. This keeps the complete evidence available without placing
large compiler outputs inside the article.

## Chapters

- Chapter 2: [From C source code to object files](chapters/02-c-source-code-to-object-files)

## Working on a chapter

Work for a chapter is prepared on a branch named `chapter-NN-short-name`. The
source, article examples, and recorded outputs are reviewed together. When the
chapter is ready, its branch can be squash-merged so that `main` has one clear
checkpoint for that chapter.

## Licence

The source code in this repository is available under the [MIT Licence](LICENSE).
