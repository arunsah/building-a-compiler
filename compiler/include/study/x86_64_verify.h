#ifndef STUDY_X86_64_VERIFY_H
#define STUDY_X86_64_VERIFY_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/x86_64.h"

#include <stdbool.h>

bool study_x86_64_verify(
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
