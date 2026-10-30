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
 * Encode instructions and write the ELF64 relocatable object ourselves, then
 * invoke GNU ld as the one remaining default-path adapter.
 */
bool study_build_with_elf_and_system_linker(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
);

/*
 * Reference path for comparing our ELF writer with the earlier GNU objcopy
 * wrapper. This is never selected silently by the default build.
 */
bool study_build_with_objcopy_and_system_linker(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
);

#endif
