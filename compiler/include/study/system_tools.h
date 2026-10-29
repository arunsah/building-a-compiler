#ifndef STUDY_SYSTEM_TOOLS_H
#define STUDY_SYSTEM_TOOLS_H

#include "study/diagnostic.h"

#include <stdbool.h>

bool study_system_assemble_x86_64(
    const char *assembly_path,
    const char *object_path,
    const char *source_path,
    StudyDiagnostic *diagnostic
);

bool study_system_wrap_x86_64_binary(
    const char *directory,
    const char *binary_name,
    const char *object_name,
    const char *source_path,
    StudyDiagnostic *diagnostic
);

bool study_system_link_x86_64(
    const char *object_path,
    const char *executable_path,
    const char *source_path,
    StudyDiagnostic *diagnostic
);

#endif
