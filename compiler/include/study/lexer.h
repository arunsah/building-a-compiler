#ifndef STUDY_LEXER_H
#define STUDY_LEXER_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/token.h"

#include <stdbool.h>

/*
 * The output owns its token array. Individual tokens borrow their spelling
 * from source through byte ranges, so source must outlive every consumer of
 * the list. The output must be initialized and empty. On failure it remains
 * empty and diagnostic borrows source->path.
 */
bool study_lex(
    const StudySource *source,
    StudyTokenList *output,
    StudyDiagnostic *diagnostic
);

#endif
