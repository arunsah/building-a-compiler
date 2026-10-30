#ifndef STUDY_BUILD_H
#define STUDY_BUILD_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/x86_64.h"

#include <stdbool.h>

/*
 * Final bounded path for the first milestone: encode instructions, write our
 * ELF64 relocatable object, link it into a static ET_EXEC file, and publish the
 * executable without invoking a system binary tool.
 */
bool study_build_executable(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
);

/*
 * Reference path: emit GNU assembly and invoke the explicitly named system
 * assembler and linker adapters. A successful executable is published by
 * rename; a failed build leaves the requested output unchanged.
 */
bool study_build_with_system_tools(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
);

/*
 * Reference path: encode instructions and write the ELF64 relocatable object
 * ourselves, then invoke GNU ld. This is never selected silently by the
 * default build.
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
