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

#endif
