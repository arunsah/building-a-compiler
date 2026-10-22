# Building a Compiler

This repository began as the supporting repository for the
[Building a Compiler](https://arunsah.com/compiler) series. The initial
investigation of the existing C toolchain now belongs to the independent
[Understanding C](https://arunsah.com/c) series. The repository keeps the
source code, experiments, and recorded outputs used by those articles.

Each chapter has its own directory. The directory contains the programs used
in the article, a script which verifies them, and any generated files which the
article discusses. This keeps the complete evidence available without placing
large compiler outputs inside the article.

## Current articles

- Understanding C, Article 2: [From C source code to object files](chapters/02-c-source-code-to-object-files)
- Understanding C, Article 3: [From object files to an executable](chapters/03-object-files-to-executable)

## Working on an article

Work for an article is prepared on its own branch. The source, article examples,
and recorded outputs are reviewed together. When the article is ready, its
branch can be squash-merged so that `main` has one clear checkpoint for that
article.

## Licence

The source code in this repository is available under the [MIT Licence](LICENSE).
