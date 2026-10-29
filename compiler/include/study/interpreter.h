#ifndef STUDY_INTERPRETER_H
#define STUDY_INTERPRETER_H

#include "study/diagnostic.h"
#include "study/ir.h"
#include "study/source.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Verify and execute main. Program output is written to stream. The main
 * result is returned as its exact i32 bit pattern; the compiler command keeps
 * its own success or failure exit status separate from this value.
 */
bool study_ir_interpret(
    const StudyIrProgram *program,
    const StudySource *source,
    FILE *stream,
    uint32_t *main_result,
    StudyDiagnostic *diagnostic
);

#endif
