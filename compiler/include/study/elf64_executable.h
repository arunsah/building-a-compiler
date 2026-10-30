#ifndef STUDY_ELF64_EXECUTABLE_H
#define STUDY_ELF64_EXECUTABLE_H

#include "study/diagnostic.h"
#include "study/elf64.h"
#include "study/source.h"
#include "study/x86_64.h"
#include "study/x86_64_encode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct StudyElf64AppliedRelocation {
    size_t object_relocation_index;
    size_t text_offset;
    size_t symbol_index;
    uint64_t symbol_address;
    int64_t addend;
    int32_t value;
} StudyElf64AppliedRelocation;

typedef struct StudyElf64Executable {
    unsigned char *bytes;
    size_t length;
    uint64_t base_address;
    uint64_t entry_address;
    uint64_t text_address;
    size_t text_offset;
    size_t text_size;
    size_t load_size;
    size_t program_header_offset;
    size_t program_header_count;
    size_t symbol_offset;
    size_t symbol_count;
    size_t string_offset;
    size_t string_size;
    size_t section_string_offset;
    size_t section_string_size;
    size_t section_header_offset;
    size_t section_count;
    StudyElf64AppliedRelocation *applied_relocations;
    size_t applied_relocation_count;
} StudyElf64Executable;

void study_elf64_executable_init(StudyElf64Executable *executable);
void study_elf64_executable_free(StudyElf64Executable *executable);

/*
 * Link the exact single-.text ELF64 object produced by study_elf64_object_build
 * into one static Linux x86-64 ET_EXEC file. The bounded linker accepts only
 * defined .text symbols and R_X86_64_PC32 RELA records. The caller must provide
 * an initialised, empty output executable.
 */
bool study_elf64_executable_build(
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyElf64Executable *output,
    StudyDiagnostic *diagnostic
);

bool study_elf64_executable_verify(
    const StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

bool study_elf64_executable_dump(
    FILE *stream,
    const StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

bool study_elf64_executable_write(
    FILE *stream,
    const StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
