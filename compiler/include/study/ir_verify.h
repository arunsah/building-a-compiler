#ifndef STUDY_IR_VERIFY_H
#define STUDY_IR_VERIFY_H

#include "study/diagnostic.h"
#include "study/ir.h"
#include "study/source.h"

#include <stdbool.h>

/* Verify the IR without consulting the syntax tree or checked program. */
bool study_ir_verify(
    const StudyIrProgram *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
