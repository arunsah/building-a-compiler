#ifndef STUDY_LOWER_H
#define STUDY_LOWER_H

#include "study/checked.h"
#include "study/diagnostic.h"
#include "study/ir.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <stdbool.h>

/*
 * Every input is borrowed. On success, output owns all IR arrays but keeps
 * token indices for names, so source and tokens must outlive any IR dump. The
 * output must be initialized and empty. On failure it remains empty.
 */
bool study_lower_to_ir(
    const StudySource *source,
    const StudyTokenList *tokens,
    const StudySyntaxTree *tree,
    const StudyCheckedProgram *checked,
    StudyIrProgram *output,
    StudyDiagnostic *diagnostic
);

#endif
