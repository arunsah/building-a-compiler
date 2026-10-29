#ifndef STUDY_CHECKER_H
#define STUDY_CHECKER_H

#include "study/checked.h"
#include "study/diagnostic.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <stdbool.h>

/*
 * The source, token list, and syntax tree are borrowed. On success, output
 * owns its annotation and declaration arrays; names remain token indices, so
 * every borrowed input must outlive the checked program. The output must be
 * initialized and empty. On failure it remains empty.
 */
bool study_check_program(
    const StudySource *source,
    const StudyTokenList *tokens,
    const StudySyntaxTree *tree,
    StudyCheckedProgram *output,
    StudyDiagnostic *diagnostic
);

#endif
