#ifndef STUDY_SOURCE_H
#define STUDY_SOURCE_H

#include "study/diagnostic.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct StudySource {
    char *path;
    unsigned char *bytes;
    size_t length;
} StudySource;

void study_source_init(StudySource *source);
void study_source_free(StudySource *source);

/*
 * Producers require an initialized empty output. On success, output owns a
 * path copy and length bytes followed by a convenience NUL. On failure, output
 * remains empty. length, not the NUL byte, is authoritative.
 */
bool study_source_copy(
    StudySource *output,
    const char *path,
    const unsigned char *bytes,
    size_t length,
    StudyDiagnostic *diagnostic
);

bool study_source_read(
    StudySource *output,
    const char *path,
    StudyDiagnostic *diagnostic
);

size_t study_source_line_count(const StudySource *source);
bool study_source_has_final_line_ending(const StudySource *source);

#endif
