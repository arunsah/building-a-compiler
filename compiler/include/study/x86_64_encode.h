#ifndef STUDY_X86_64_ENCODE_H
#define STUDY_X86_64_ENCODE_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/x86_64.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct StudyX86Image {
    unsigned char *bytes;
    size_t length;
    size_t capacity;
    size_t *function_offsets;
    size_t function_count;
    size_t *block_offsets;
    size_t block_count;
    size_t *instruction_offsets;
    size_t *instruction_sizes;
    size_t instruction_count;
    size_t entry_offset;
} StudyX86Image;

void study_x86_64_image_init(StudyX86Image *image);
void study_x86_64_image_free(StudyX86Image *image);

/*
 * Encode one verified x86-64 target program into a position-independent flat
 * code image. Calls and control-flow edges are resolved as signed rel32
 * displacements inside the image. The caller must provide an initialised,
 * empty output image.
 */
bool study_x86_64_encode(
    const StudyX86Program *program,
    const StudySource *source,
    StudyX86Image *output,
    StudyDiagnostic *diagnostic
);

bool study_x86_64_image_dump(
    FILE *stream,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

bool study_x86_64_image_write(
    FILE *stream,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
