#ifndef STUDY_X86_64_ASSEMBLY_H
#define STUDY_X86_64_ASSEMBLY_H

#include "study/diagnostic.h"
#include "study/ir.h"
#include "study/source.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * Emit GNU assembler source for the first Linux x86-64 System V target.
 * The input IR is borrowed and verified again before any assembly is written.
 */
bool study_x86_64_emit_assembly(
    FILE *stream,
    const StudyIrProgram *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
