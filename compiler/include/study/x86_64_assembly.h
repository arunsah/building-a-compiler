#ifndef STUDY_X86_64_ASSEMBLY_H
#define STUDY_X86_64_ASSEMBLY_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/x86_64.h"

#include <stdbool.h>
#include <stdio.h>

/*
 * Render a verified x86-64 target program as GNU assembler source. This is a
 * temporary textual adapter: the later encoder will consume the same target
 * representation without parsing this text.
 */
bool study_x86_64_emit_assembly(
    FILE *stream,
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
