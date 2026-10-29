#ifndef STUDY_X86_64_LOWER_H
#define STUDY_X86_64_LOWER_H

#include "study/diagnostic.h"
#include "study/ir.h"
#include "study/source.h"
#include "study/x86_64.h"

#include <stdbool.h>

/*
 * Select x86-64 instructions and assign the deliberately simple stack
 * locations used by the first Linux System V backend.
 */
bool study_lower_to_x86_64(
    const StudyIrProgram *ir,
    const StudySource *source,
    StudyX86Program *output,
    StudyDiagnostic *diagnostic
);

#endif
