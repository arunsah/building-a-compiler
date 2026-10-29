#ifndef STUDY_PARSER_H
#define STUDY_PARSER_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <stdbool.h>

/*
 * source and tokens are borrowed. The output owns one node array, while name
 * spellings remain token indices into the borrowed token list and source. The
 * output must be initialized and empty. On failure it remains empty.
 */
bool study_parse(
    const StudySource *source,
    const StudyTokenList *tokens,
    StudySyntaxTree *output,
    StudyDiagnostic *diagnostic
);

#endif
