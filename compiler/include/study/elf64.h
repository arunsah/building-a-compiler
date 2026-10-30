#ifndef STUDY_ELF64_H
#define STUDY_ELF64_H

#include "study/diagnostic.h"
#include "study/source.h"
#include "study/x86_64.h"
#include "study/x86_64_encode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct StudyElf64Relocation {
    StudyX86InstructionId instruction;
    size_t offset;
    StudyX86FunctionId target_function;
    size_t symbol_index;
    int64_t addend;
} StudyElf64Relocation;

typedef struct StudyElf64Object {
    unsigned char *bytes;
    size_t length;
    size_t text_offset;
    size_t text_size;
    size_t relocation_offset;
    size_t relocation_count;
    size_t symbol_offset;
    size_t symbol_count;
    size_t string_offset;
    size_t string_size;
    size_t section_string_offset;
    size_t section_string_size;
    size_t section_header_offset;
    size_t section_count;
    size_t *function_symbol_indices;
    size_t function_count;
    StudyElf64Relocation *relocations;
} StudyElf64Object;

void study_elf64_object_init(StudyElf64Object *object);
void study_elf64_object_free(StudyElf64Object *object);

/*
 * Build one little-endian ELF64 x86-64 relocatable object around an encoded
 * code image. Calls become R_X86_64_PC32 relocations; block-local control flow
 * remains resolved inside .text. The caller must provide an initialised,
 * empty output object.
 */
bool study_elf64_object_build(
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyElf64Object *output,
    StudyDiagnostic *diagnostic
);

bool study_elf64_object_verify(
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

bool study_elf64_object_dump(
    FILE *stream,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

bool study_elf64_object_write(
    FILE *stream,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
