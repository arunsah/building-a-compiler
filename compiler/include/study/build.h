#ifndef STUDY_BUILD_H
#define STUDY_BUILD_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/x86_64.h"

#include <stdbool.h>

/*
 * Temporary vertical slice: emit GNU assembly and invoke the explicitly named
 * system assembler and linker adapters. The final compiler path will replace
 * both adapters. A successful executable is published by rename; a failed
 * build leaves the requested output unchanged.
 */
bool study_build_with_system_tools(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
);

/*
 * Encode instructions ourselves, wrap the flat image in a temporary ELF
 * object with GNU objcopy, and invoke GNU ld. The remaining object and linker
 * adapters are explicit boundaries for the next checkpoints.
 */
bool study_build_with_encoder_and_system_linker(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
);

#endif
