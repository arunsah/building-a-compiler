#include "study/elf64_executable.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    STUDY_EXEC_ELF_HEADER_SIZE = 64,
    STUDY_EXEC_PROGRAM_HEADER_SIZE = 56,
    STUDY_EXEC_SECTION_HEADER_SIZE = 64,
    STUDY_EXEC_SYMBOL_SIZE = 24,
    STUDY_EXEC_RELOCATION_SIZE = 24,
    STUDY_EXEC_PROGRAM_HEADER_COUNT = 2,
    STUDY_EXEC_SECTION_COUNT = 5,
    STUDY_EXEC_SECTION_NULL = 0,
    STUDY_EXEC_SECTION_TEXT = 1,
    STUDY_EXEC_SECTION_SYMTAB = 2,
    STUDY_EXEC_SECTION_STRTAB = 3,
    STUDY_EXEC_SECTION_SHSTRTAB = 4,
    STUDY_EXEC_PT_LOAD = 1,
    STUDY_EXEC_PT_GNU_STACK = 0x6474e551,
    STUDY_EXEC_PF_X = 1,
    STUDY_EXEC_PF_W = 2,
    STUDY_EXEC_PF_R = 4,
    STUDY_EXEC_SHT_NULL = 0,
    STUDY_EXEC_SHT_PROGBITS = 1,
    STUDY_EXEC_SHT_SYMTAB = 2,
    STUDY_EXEC_SHT_STRTAB = 3,
    STUDY_EXEC_SHF_ALLOC = 2,
    STUDY_EXEC_SHF_EXECINSTR = 4,
    STUDY_EXEC_ET_EXEC = 2,
    STUDY_EXEC_EM_X86_64 = 62,
    STUDY_EXEC_R_X86_64_PC32 = 2
};

enum {
    STUDY_EXEC_NAME_TEXT = 1,
    STUDY_EXEC_NAME_SYMTAB = 7,
    STUDY_EXEC_NAME_STRTAB = 15,
    STUDY_EXEC_NAME_SHSTRTAB = 23
};

static const unsigned char study_exec_section_strings[] =
    "\0.text\0.symtab\0.strtab\0.shstrtab";

static const uint64_t study_exec_base_address = UINT64_C(0x400000);
static const size_t study_exec_page_size = 4096U;

typedef struct StudyElf64ExecutableBuild {
    StudyElf64Executable executable;
} StudyElf64ExecutableBuild;

static void study_exec_error(
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const char *message
) {
    study_diagnostic_set(
        diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        source != NULL ? source->path : NULL,
        "%s",
        message
    );
}

void study_elf64_executable_init(StudyElf64Executable *executable) {
    if (executable == NULL) {
        return;
    }
    memset(executable, 0, sizeof(*executable));
}

void study_elf64_executable_free(StudyElf64Executable *executable) {
    if (executable == NULL) {
        return;
    }
    free(executable->applied_relocations);
    free(executable->bytes);
    study_elf64_executable_init(executable);
}

static bool study_exec_is_empty(const StudyElf64Executable *executable) {
    return executable != NULL && executable->bytes == NULL &&
           executable->length == 0U &&
           executable->applied_relocations == NULL &&
           executable->applied_relocation_count == 0U;
}

static bool study_exec_add_size(
    size_t left,
    size_t right,
    size_t *result
) {
    if (left > SIZE_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool study_exec_multiply_size(
    size_t left,
    size_t right,
    size_t *result
) {
    if (left != 0U && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool study_exec_align_size(
    size_t value,
    size_t alignment,
    size_t *result
) {
    size_t remainder;
    size_t addition;

    if (alignment == 0U) {
        return false;
    }
    remainder = value % alignment;
    addition = remainder == 0U ? 0U : alignment - remainder;
    return study_exec_add_size(value, addition, result);
}

static bool study_exec_range(
    size_t total,
    size_t offset,
    size_t length
) {
    return offset <= total && length <= total - offset;
}

static void study_exec_write_u16(
    unsigned char *bytes,
    size_t offset,
    uint16_t value
) {
    bytes[offset] = (unsigned char)(value & UINT16_C(0xff));
    bytes[offset + 1U] =
        (unsigned char)((value >> 8U) & UINT16_C(0xff));
}

static void study_exec_write_u32(
    unsigned char *bytes,
    size_t offset,
    uint32_t value
) {
    size_t index;

    for (index = 0U; index < 4U; index++) {
        bytes[offset + index] =
            (unsigned char)((value >> (index * 8U)) & UINT32_C(0xff));
    }
}

static void study_exec_write_u64(
    unsigned char *bytes,
    size_t offset,
    uint64_t value
) {
    size_t index;

    for (index = 0U; index < 8U; index++) {
        bytes[offset + index] =
            (unsigned char)((value >> (index * 8U)) & UINT64_C(0xff));
    }
}

static uint16_t study_exec_read_u16(
    const unsigned char *bytes,
    size_t offset
) {
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1U] << 8U));
}

static uint32_t study_exec_read_u32(
    const unsigned char *bytes,
    size_t offset
) {
    uint32_t value = 0U;
    size_t index;

    for (index = 0U; index < 4U; index++) {
        value |= (uint32_t)bytes[offset + index] << (index * 8U);
    }
    return value;
}

static uint64_t study_exec_read_u64(
    const unsigned char *bytes,
    size_t offset
) {
    uint64_t value = 0U;
    size_t index;

    for (index = 0U; index < 8U; index++) {
        value |= (uint64_t)bytes[offset + index] << (index * 8U);
    }
    return value;
}

static int64_t study_exec_read_i64(
    const unsigned char *bytes,
    size_t offset
) {
    uint64_t bits = study_exec_read_u64(bytes, offset);
    uint64_t magnitude;

    if (bits <= (uint64_t)INT64_MAX) {
        return (int64_t)bits;
    }
    magnitude = (~bits) + UINT64_C(1);
    if (magnitude == (UINT64_C(1) << 63U)) {
        return INT64_MIN;
    }
    return -(int64_t)magnitude;
}

static bool study_exec_calculate_layout(
    StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t cursor;
    size_t byte_count;

    executable->base_address = study_exec_base_address;
    executable->program_header_offset = STUDY_EXEC_ELF_HEADER_SIZE;
    executable->program_header_count = STUDY_EXEC_PROGRAM_HEADER_COUNT;
    if (!study_exec_multiply_size(
            executable->program_header_count,
            STUDY_EXEC_PROGRAM_HEADER_SIZE,
            &byte_count
        ) ||
        !study_exec_add_size(
            executable->program_header_offset,
            byte_count,
            &cursor
        ) ||
        !study_exec_align_size(
            cursor,
            study_exec_page_size,
            &executable->text_offset
        ) ||
        !study_exec_add_size(
            executable->text_offset,
            object->text_size,
            &executable->load_size
        ) ||
        !study_exec_align_size(
            executable->load_size,
            8U,
            &executable->symbol_offset
        ) ||
        !study_exec_multiply_size(
            object->symbol_count,
            STUDY_EXEC_SYMBOL_SIZE,
            &byte_count
        ) ||
        !study_exec_add_size(
            executable->symbol_offset,
            byte_count,
            &cursor
        )) {
        study_exec_error(diagnostic, source, "ELF executable layout is too large");
        return false;
    }
    executable->string_offset = cursor;
    if (!study_exec_add_size(
            cursor,
            object->string_size,
            &cursor
        )) {
        study_exec_error(diagnostic, source, "ELF executable layout is too large");
        return false;
    }
    executable->section_string_offset = cursor;
    executable->section_string_size = sizeof(study_exec_section_strings);
    if (!study_exec_add_size(
            cursor,
            executable->section_string_size,
            &cursor
        ) ||
        !study_exec_align_size(
            cursor,
            8U,
            &executable->section_header_offset
        ) ||
        !study_exec_multiply_size(
            STUDY_EXEC_SECTION_COUNT,
            STUDY_EXEC_SECTION_HEADER_SIZE,
            &byte_count
        ) ||
        !study_exec_add_size(
            executable->section_header_offset,
            byte_count,
            &executable->length
        )) {
        study_exec_error(diagnostic, source, "ELF executable layout is too large");
        return false;
    }
    if ((uint64_t)executable->text_offset >
            UINT64_MAX - executable->base_address) {
        study_exec_error(diagnostic, source, "ELF executable address is too large");
        return false;
    }
    executable->text_address =
        executable->base_address + (uint64_t)executable->text_offset;
    executable->text_size = object->text_size;
    executable->symbol_count = object->symbol_count;
    executable->string_size = object->string_size;
    executable->section_count = STUDY_EXEC_SECTION_COUNT;
    executable->applied_relocation_count = object->relocation_count;
    return true;
}

static void study_exec_write_header(StudyElf64Executable *executable) {
    unsigned char *bytes = executable->bytes;

    bytes[0] = UINT8_C(0x7f);
    bytes[1] = (unsigned char)'E';
    bytes[2] = (unsigned char)'L';
    bytes[3] = (unsigned char)'F';
    bytes[4] = UINT8_C(2);
    bytes[5] = UINT8_C(1);
    bytes[6] = UINT8_C(1);
    bytes[7] = UINT8_C(0);
    study_exec_write_u16(bytes, 16U, STUDY_EXEC_ET_EXEC);
    study_exec_write_u16(bytes, 18U, STUDY_EXEC_EM_X86_64);
    study_exec_write_u32(bytes, 20U, UINT32_C(1));
    study_exec_write_u64(bytes, 24U, executable->entry_address);
    study_exec_write_u64(
        bytes,
        32U,
        (uint64_t)executable->program_header_offset
    );
    study_exec_write_u64(
        bytes,
        40U,
        (uint64_t)executable->section_header_offset
    );
    study_exec_write_u32(bytes, 48U, UINT32_C(0));
    study_exec_write_u16(bytes, 52U, STUDY_EXEC_ELF_HEADER_SIZE);
    study_exec_write_u16(bytes, 54U, STUDY_EXEC_PROGRAM_HEADER_SIZE);
    study_exec_write_u16(bytes, 56U, STUDY_EXEC_PROGRAM_HEADER_COUNT);
    study_exec_write_u16(bytes, 58U, STUDY_EXEC_SECTION_HEADER_SIZE);
    study_exec_write_u16(bytes, 60U, STUDY_EXEC_SECTION_COUNT);
    study_exec_write_u16(bytes, 62U, STUDY_EXEC_SECTION_SHSTRTAB);
}

static void study_exec_write_program_header(
    StudyElf64Executable *executable,
    size_t index,
    uint32_t type,
    uint32_t flags,
    uint64_t offset,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t file_size,
    uint64_t memory_size,
    uint64_t alignment
) {
    size_t base = executable->program_header_offset +
                  index * STUDY_EXEC_PROGRAM_HEADER_SIZE;

    study_exec_write_u32(executable->bytes, base, type);
    study_exec_write_u32(executable->bytes, base + 4U, flags);
    study_exec_write_u64(executable->bytes, base + 8U, offset);
    study_exec_write_u64(executable->bytes, base + 16U, virtual_address);
    study_exec_write_u64(executable->bytes, base + 24U, physical_address);
    study_exec_write_u64(executable->bytes, base + 32U, file_size);
    study_exec_write_u64(executable->bytes, base + 40U, memory_size);
    study_exec_write_u64(executable->bytes, base + 48U, alignment);
}

static void study_exec_write_program_headers(StudyElf64Executable *executable) {
    study_exec_write_program_header(
        executable,
        0U,
        STUDY_EXEC_PT_LOAD,
        STUDY_EXEC_PF_R | STUDY_EXEC_PF_X,
        UINT64_C(0),
        executable->base_address,
        executable->base_address,
        (uint64_t)executable->load_size,
        (uint64_t)executable->load_size,
        (uint64_t)study_exec_page_size
    );
    study_exec_write_program_header(
        executable,
        1U,
        STUDY_EXEC_PT_GNU_STACK,
        STUDY_EXEC_PF_R | STUDY_EXEC_PF_W,
        UINT64_C(0),
        UINT64_C(0),
        UINT64_C(0),
        UINT64_C(0),
        UINT64_C(0),
        UINT64_C(16)
    );
}

static void study_exec_write_section_header(
    StudyElf64Executable *executable,
    size_t index,
    uint32_t name,
    uint32_t type,
    uint64_t flags,
    uint64_t address,
    uint64_t offset,
    uint64_t size,
    uint32_t link,
    uint32_t info,
    uint64_t alignment,
    uint64_t entry_size
) {
    size_t base = executable->section_header_offset +
                  index * STUDY_EXEC_SECTION_HEADER_SIZE;

    study_exec_write_u32(executable->bytes, base, name);
    study_exec_write_u32(executable->bytes, base + 4U, type);
    study_exec_write_u64(executable->bytes, base + 8U, flags);
    study_exec_write_u64(executable->bytes, base + 16U, address);
    study_exec_write_u64(executable->bytes, base + 24U, offset);
    study_exec_write_u64(executable->bytes, base + 32U, size);
    study_exec_write_u32(executable->bytes, base + 40U, link);
    study_exec_write_u32(executable->bytes, base + 44U, info);
    study_exec_write_u64(executable->bytes, base + 48U, alignment);
    study_exec_write_u64(executable->bytes, base + 56U, entry_size);
}

static void study_exec_write_section_headers(
    StudyElf64Executable *executable,
    const StudyElf64Object *object
) {
    study_exec_write_section_header(
        executable,
        STUDY_EXEC_SECTION_NULL,
        0U,
        STUDY_EXEC_SHT_NULL,
        UINT64_C(0),
        UINT64_C(0),
        UINT64_C(0),
        UINT64_C(0),
        0U,
        0U,
        UINT64_C(0),
        UINT64_C(0)
    );
    study_exec_write_section_header(
        executable,
        STUDY_EXEC_SECTION_TEXT,
        STUDY_EXEC_NAME_TEXT,
        STUDY_EXEC_SHT_PROGBITS,
        STUDY_EXEC_SHF_ALLOC | STUDY_EXEC_SHF_EXECINSTR,
        executable->text_address,
        (uint64_t)executable->text_offset,
        (uint64_t)executable->text_size,
        0U,
        0U,
        UINT64_C(16),
        UINT64_C(0)
    );
    study_exec_write_section_header(
        executable,
        STUDY_EXEC_SECTION_SYMTAB,
        STUDY_EXEC_NAME_SYMTAB,
        STUDY_EXEC_SHT_SYMTAB,
        UINT64_C(0),
        UINT64_C(0),
        (uint64_t)executable->symbol_offset,
        (uint64_t)(executable->symbol_count * STUDY_EXEC_SYMBOL_SIZE),
        STUDY_EXEC_SECTION_STRTAB,
        (uint32_t)object->function_count,
        UINT64_C(8),
        STUDY_EXEC_SYMBOL_SIZE
    );
    study_exec_write_section_header(
        executable,
        STUDY_EXEC_SECTION_STRTAB,
        STUDY_EXEC_NAME_STRTAB,
        STUDY_EXEC_SHT_STRTAB,
        UINT64_C(0),
        UINT64_C(0),
        (uint64_t)executable->string_offset,
        (uint64_t)executable->string_size,
        0U,
        0U,
        UINT64_C(1),
        UINT64_C(0)
    );
    study_exec_write_section_header(
        executable,
        STUDY_EXEC_SECTION_SHSTRTAB,
        STUDY_EXEC_NAME_SHSTRTAB,
        STUDY_EXEC_SHT_STRTAB,
        UINT64_C(0),
        UINT64_C(0),
        (uint64_t)executable->section_string_offset,
        (uint64_t)executable->section_string_size,
        0U,
        0U,
        UINT64_C(1),
        UINT64_C(0)
    );
}

static bool study_exec_relocation_value(
    const StudyElf64Object *object,
    size_t relocation_index,
    size_t *text_offset,
    size_t *symbol_index,
    uint64_t *symbol_value,
    int64_t *addend,
    int32_t *value
) {
    size_t relocation_entry;
    size_t symbol_entry;
    uint64_t relocation_offset;
    uint64_t info;
    uint64_t raw_symbol;
    uint32_t type;
    int64_t result;

    if (!study_exec_multiply_size(
            relocation_index,
            STUDY_EXEC_RELOCATION_SIZE,
            &relocation_entry
        ) ||
        !study_exec_add_size(
            object->relocation_offset,
            relocation_entry,
            &relocation_entry
        ) ||
        !study_exec_range(
            object->length,
            relocation_entry,
            STUDY_EXEC_RELOCATION_SIZE
        )) {
        return false;
    }
    relocation_offset = study_exec_read_u64(object->bytes, relocation_entry);
    info = study_exec_read_u64(object->bytes, relocation_entry + 8U);
    *addend = study_exec_read_i64(object->bytes, relocation_entry + 16U);
    raw_symbol = info >> 32U;
    type = (uint32_t)(info & UINT32_MAX);
    if (relocation_offset > SIZE_MAX || raw_symbol > SIZE_MAX ||
        type != STUDY_EXEC_R_X86_64_PC32 || *addend != -4 ||
        (size_t)raw_symbol >= object->symbol_count ||
        (size_t)relocation_offset > object->text_size ||
        object->text_size - (size_t)relocation_offset < 4U) {
        return false;
    }
    *text_offset = (size_t)relocation_offset;
    *symbol_index = (size_t)raw_symbol;
    if (!study_exec_multiply_size(
            *symbol_index,
            STUDY_EXEC_SYMBOL_SIZE,
            &symbol_entry
        ) ||
        !study_exec_add_size(
            object->symbol_offset,
            symbol_entry,
            &symbol_entry
        ) ||
        !study_exec_range(object->length, symbol_entry, STUDY_EXEC_SYMBOL_SIZE) ||
        study_exec_read_u16(object->bytes, symbol_entry + 6U) != 1U) {
        return false;
    }
    *symbol_value = study_exec_read_u64(object->bytes, symbol_entry + 8U);
    if (*symbol_value > (uint64_t)INT64_MAX ||
        *text_offset > (size_t)INT64_MAX) {
        return false;
    }
    result = (int64_t)*symbol_value + *addend - (int64_t)*text_offset;
    if (result < INT32_MIN || result > INT32_MAX) {
        return false;
    }
    *value = (int32_t)result;
    return true;
}

static bool study_exec_apply_relocations(
    StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t index;

    for (index = 0U; index < object->relocation_count; index++) {
        StudyElf64AppliedRelocation *applied =
            &executable->applied_relocations[index];
        uint64_t symbol_value;
        size_t previous;

        if (!study_exec_relocation_value(
                object,
                index,
                &applied->text_offset,
                &applied->symbol_index,
                &symbol_value,
                &applied->addend,
                &applied->value
            )) {
            study_exec_error(
                diagnostic,
                source,
                "unsupported or invalid ELF object relocation"
            );
            return false;
        }
        for (previous = 0U; previous < index; previous++) {
            size_t earlier =
                executable->applied_relocations[previous].text_offset;
            if (applied->text_offset < earlier + 4U &&
                earlier < applied->text_offset + 4U) {
                study_exec_error(
                    diagnostic,
                    source,
                    "overlapping ELF object relocations"
                );
                return false;
            }
        }
        if (symbol_value > UINT64_MAX - executable->text_address) {
            study_exec_error(
                diagnostic,
                source,
                "ELF relocation symbol address is too large"
            );
            return false;
        }
        applied->object_relocation_index = index;
        applied->symbol_address = executable->text_address + symbol_value;
        study_exec_write_u32(
            executable->bytes,
            executable->text_offset + applied->text_offset,
            (uint32_t)applied->value
        );
    }
    return true;
}

static bool study_exec_copy_symbols(
    StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t byte_count;
    size_t index;

    if (!study_exec_multiply_size(
            object->symbol_count,
            STUDY_EXEC_SYMBOL_SIZE,
            &byte_count
        ) ||
        !study_exec_range(object->length, object->symbol_offset, byte_count)) {
        study_exec_error(diagnostic, source, "ELF object symbol table is invalid");
        return false;
    }
    memcpy(
        &executable->bytes[executable->symbol_offset],
        &object->bytes[object->symbol_offset],
        byte_count
    );
    for (index = 1U; index < object->symbol_count; index++) {
        size_t input = object->symbol_offset +
                       index * STUDY_EXEC_SYMBOL_SIZE;
        size_t output = executable->symbol_offset +
                        index * STUDY_EXEC_SYMBOL_SIZE;
        uint64_t value;

        if (study_exec_read_u16(object->bytes, input + 6U) != 1U) {
            study_exec_error(
                diagnostic,
                source,
                "bounded linker requires defined .text symbols"
            );
            return false;
        }
        value = study_exec_read_u64(object->bytes, input + 8U);
        if (value > UINT64_MAX - executable->text_address) {
            study_exec_error(
                diagnostic,
                source,
                "ELF symbol address is too large"
            );
            return false;
        }
        study_exec_write_u64(
            executable->bytes,
            output + 8U,
            executable->text_address + value
        );
    }
    return true;
}

static bool study_exec_start_address(
    StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t symbol_index;
    size_t symbol_entry;

    if (program->start_function >= object->function_count) {
        study_exec_error(diagnostic, source, "ELF start function is invalid");
        return false;
    }
    symbol_index = object->function_symbol_indices[program->start_function];
    if (symbol_index >= object->symbol_count ||
        !study_exec_multiply_size(
            symbol_index,
            STUDY_EXEC_SYMBOL_SIZE,
            &symbol_entry
        ) ||
        !study_exec_add_size(
            executable->symbol_offset,
            symbol_entry,
            &symbol_entry
        ) ||
        !study_exec_range(
            executable->length,
            symbol_entry,
            STUDY_EXEC_SYMBOL_SIZE
        )) {
        study_exec_error(diagnostic, source, "ELF start symbol is invalid");
        return false;
    }
    executable->entry_address =
        study_exec_read_u64(executable->bytes, symbol_entry + 8U);
    return true;
}

bool study_elf64_executable_build(
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyElf64Executable *output,
    StudyDiagnostic *diagnostic
) {
    StudyElf64ExecutableBuild build;

    study_diagnostic_clear(diagnostic);
    if (object == NULL || program == NULL || image == NULL || source == NULL ||
        output == NULL) {
        study_exec_error(diagnostic, source, "invalid ELF executable input");
        return false;
    }
    if (!study_exec_is_empty(output)) {
        study_exec_error(
            diagnostic,
            source,
            "ELF executable build requires an initialised empty output"
        );
        return false;
    }
    if (!study_elf64_object_verify(
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        return false;
    }

    study_elf64_executable_init(&build.executable);
    if (!study_exec_calculate_layout(
            &build.executable,
            object,
            source,
            diagnostic
        )) {
        goto fail;
    }
    build.executable.bytes = calloc(build.executable.length, 1U);
    if (build.executable.bytes == NULL) {
        study_exec_error(
            diagnostic,
            source,
            "not enough memory for the ELF executable"
        );
        goto fail;
    }
    if (object->relocation_count > 0U) {
        build.executable.applied_relocations = calloc(
            object->relocation_count,
            sizeof(*build.executable.applied_relocations)
        );
        if (build.executable.applied_relocations == NULL) {
            study_exec_error(
                diagnostic,
                source,
                "not enough memory for applied ELF relocations"
            );
            goto fail;
        }
    }
    memcpy(
        &build.executable.bytes[build.executable.text_offset],
        &object->bytes[object->text_offset],
        object->text_size
    );
    if (!study_exec_apply_relocations(
            &build.executable,
            object,
            source,
            diagnostic
        ) ||
        !study_exec_copy_symbols(
            &build.executable,
            object,
            source,
            diagnostic
        )) {
        goto fail;
    }
    memcpy(
        &build.executable.bytes[build.executable.string_offset],
        &object->bytes[object->string_offset],
        object->string_size
    );
    memcpy(
        &build.executable.bytes[build.executable.section_string_offset],
        study_exec_section_strings,
        sizeof(study_exec_section_strings)
    );
    if (!study_exec_start_address(
            &build.executable,
            object,
            program,
            source,
            diagnostic
        )) {
        goto fail;
    }
    study_exec_write_header(&build.executable);
    study_exec_write_program_headers(&build.executable);
    study_exec_write_section_headers(&build.executable, object);

    if (!study_elf64_executable_verify(
            &build.executable,
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        goto fail;
    }
    *output = build.executable;
    study_elf64_executable_init(&build.executable);
    return true;

fail:
    study_elf64_executable_free(&build.executable);
    return false;
}

static bool study_exec_section_header_matches(
    const StudyElf64Executable *executable,
    size_t index,
    uint32_t name,
    uint32_t type,
    uint64_t flags,
    uint64_t address,
    uint64_t offset,
    uint64_t size,
    uint32_t link,
    uint32_t info,
    uint64_t alignment,
    uint64_t entry_size
) {
    size_t base;

    if (!study_exec_multiply_size(
            index,
            STUDY_EXEC_SECTION_HEADER_SIZE,
            &base
        ) ||
        !study_exec_add_size(executable->section_header_offset, base, &base) ||
        !study_exec_range(
            executable->length,
            base,
            STUDY_EXEC_SECTION_HEADER_SIZE
        )) {
        return false;
    }
    return study_exec_read_u32(executable->bytes, base) == name &&
           study_exec_read_u32(executable->bytes, base + 4U) == type &&
           study_exec_read_u64(executable->bytes, base + 8U) == flags &&
           study_exec_read_u64(executable->bytes, base + 16U) == address &&
           study_exec_read_u64(executable->bytes, base + 24U) == offset &&
           study_exec_read_u64(executable->bytes, base + 32U) == size &&
           study_exec_read_u32(executable->bytes, base + 40U) == link &&
           study_exec_read_u32(executable->bytes, base + 44U) == info &&
           study_exec_read_u64(executable->bytes, base + 48U) == alignment &&
           study_exec_read_u64(executable->bytes, base + 56U) == entry_size;
}

static bool study_exec_verify_program_headers(
    const StudyElf64Executable *executable
) {
    const unsigned char *bytes = executable->bytes;
    size_t load = executable->program_header_offset;
    size_t stack = load + STUDY_EXEC_PROGRAM_HEADER_SIZE;

    return study_exec_range(
               executable->length,
               load,
               STUDY_EXEC_PROGRAM_HEADER_SIZE *
                   STUDY_EXEC_PROGRAM_HEADER_COUNT
           ) &&
           study_exec_read_u32(bytes, load) == STUDY_EXEC_PT_LOAD &&
           study_exec_read_u32(bytes, load + 4U) ==
               (STUDY_EXEC_PF_R | STUDY_EXEC_PF_X) &&
           study_exec_read_u64(bytes, load + 8U) == 0U &&
           study_exec_read_u64(bytes, load + 16U) ==
               executable->base_address &&
           study_exec_read_u64(bytes, load + 24U) ==
               executable->base_address &&
           study_exec_read_u64(bytes, load + 32U) == executable->load_size &&
           study_exec_read_u64(bytes, load + 40U) == executable->load_size &&
           study_exec_read_u64(bytes, load + 48U) == study_exec_page_size &&
           study_exec_read_u32(bytes, stack) == STUDY_EXEC_PT_GNU_STACK &&
           study_exec_read_u32(bytes, stack + 4U) ==
               (STUDY_EXEC_PF_R | STUDY_EXEC_PF_W) &&
           study_exec_read_u64(bytes, stack + 8U) == 0U &&
           study_exec_read_u64(bytes, stack + 16U) == 0U &&
           study_exec_read_u64(bytes, stack + 24U) == 0U &&
           study_exec_read_u64(bytes, stack + 32U) == 0U &&
           study_exec_read_u64(bytes, stack + 40U) == 0U &&
           study_exec_read_u64(bytes, stack + 48U) == 16U;
}

static bool study_exec_is_relocation_byte(
    const StudyElf64Executable *executable,
    size_t text_offset
) {
    size_t index;

    for (index = 0U;
         index < executable->applied_relocation_count;
         index++) {
        size_t offset = executable->applied_relocations[index].text_offset;
        if (text_offset >= offset && text_offset - offset < 4U) {
            return true;
        }
    }
    return false;
}

static bool study_exec_verify_text_and_relocations(
    const StudyElf64Executable *executable,
    const StudyElf64Object *object
) {
    size_t index;

    for (index = 0U; index < object->text_size; index++) {
        if (!study_exec_is_relocation_byte(executable, index) &&
            executable->bytes[executable->text_offset + index] !=
                object->bytes[object->text_offset + index]) {
            return false;
        }
    }
    for (index = 0U; index < object->relocation_count; index++) {
        const StudyElf64AppliedRelocation *applied =
            &executable->applied_relocations[index];
        size_t offset;
        size_t symbol;
        uint64_t symbol_value;
        int64_t addend;
        int32_t value;

        if (!study_exec_relocation_value(
                object,
                index,
                &offset,
                &symbol,
                &symbol_value,
                &addend,
                &value
            ) ||
            applied->object_relocation_index != index ||
            applied->text_offset != offset ||
            applied->symbol_index != symbol ||
            applied->symbol_address !=
                executable->text_address + symbol_value ||
            applied->addend != addend || applied->value != value ||
            study_exec_read_u32(
                executable->bytes,
                executable->text_offset + offset
            ) != (uint32_t)value) {
            return false;
        }
    }
    return true;
}

static bool study_exec_verify_symbols(
    const StudyElf64Executable *executable,
    const StudyElf64Object *object
) {
    size_t index;

    for (index = 0U; index < object->symbol_count; index++) {
        size_t input = object->symbol_offset +
                       index * STUDY_EXEC_SYMBOL_SIZE;
        size_t output = executable->symbol_offset +
                        index * STUDY_EXEC_SYMBOL_SIZE;
        uint64_t expected_value = study_exec_read_u64(
            object->bytes,
            input + 8U
        );
        size_t byte;

        if (index > 0U) {
            if (expected_value > UINT64_MAX - executable->text_address) {
                return false;
            }
            expected_value += executable->text_address;
        }
        for (byte = 0U; byte < STUDY_EXEC_SYMBOL_SIZE; byte++) {
            if (byte >= 8U && byte < 16U) {
                continue;
            }
            if (executable->bytes[output + byte] !=
                object->bytes[input + byte]) {
                return false;
            }
        }
        if (study_exec_read_u64(executable->bytes, output + 8U) !=
            expected_value) {
            return false;
        }
    }
    return true;
}

bool study_elf64_executable_verify(
    const StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t symbol_bytes;
    size_t section_bytes;
    size_t start_symbol;
    size_t start_entry;

    study_diagnostic_clear(diagnostic);
    if (executable == NULL || object == NULL || program == NULL ||
        image == NULL || source == NULL || executable->bytes == NULL ||
        executable->length == 0U ||
        !study_elf64_object_verify(
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_exec_error(diagnostic, source, "invalid ELF executable input");
        }
        return false;
    }
    if (executable->base_address != study_exec_base_address ||
        executable->program_header_offset != STUDY_EXEC_ELF_HEADER_SIZE ||
        executable->program_header_count !=
            STUDY_EXEC_PROGRAM_HEADER_COUNT ||
        executable->text_offset != study_exec_page_size ||
        executable->text_address !=
            executable->base_address + executable->text_offset ||
        executable->text_size != object->text_size ||
        executable->load_size != executable->text_offset +
            executable->text_size ||
        executable->symbol_count != object->symbol_count ||
        executable->string_size != object->string_size ||
        executable->section_string_size !=
            sizeof(study_exec_section_strings) ||
        executable->section_count != STUDY_EXEC_SECTION_COUNT ||
        executable->applied_relocation_count != object->relocation_count ||
        (executable->applied_relocation_count > 0U &&
         executable->applied_relocations == NULL) ||
        !study_exec_multiply_size(
            executable->symbol_count,
            STUDY_EXEC_SYMBOL_SIZE,
            &symbol_bytes
        ) ||
        !study_exec_multiply_size(
            executable->section_count,
            STUDY_EXEC_SECTION_HEADER_SIZE,
            &section_bytes
        ) ||
        !study_exec_range(
            executable->length,
            executable->text_offset,
            executable->text_size
        ) ||
        !study_exec_range(
            executable->length,
            executable->symbol_offset,
            symbol_bytes
        ) ||
        !study_exec_range(
            executable->length,
            executable->string_offset,
            executable->string_size
        ) ||
        !study_exec_range(
            executable->length,
            executable->section_string_offset,
            executable->section_string_size
        ) ||
        !study_exec_range(
            executable->length,
            executable->section_header_offset,
            section_bytes
        )) {
        study_exec_error(
            diagnostic,
            source,
            "ELF executable metadata does not describe a complete file"
        );
        return false;
    }
    if (executable->bytes[0] != UINT8_C(0x7f) ||
        executable->bytes[1] != (unsigned char)'E' ||
        executable->bytes[2] != (unsigned char)'L' ||
        executable->bytes[3] != (unsigned char)'F' ||
        executable->bytes[4] != UINT8_C(2) ||
        executable->bytes[5] != UINT8_C(1) ||
        executable->bytes[6] != UINT8_C(1) ||
        study_exec_read_u16(executable->bytes, 16U) != STUDY_EXEC_ET_EXEC ||
        study_exec_read_u16(executable->bytes, 18U) !=
            STUDY_EXEC_EM_X86_64 ||
        study_exec_read_u32(executable->bytes, 20U) != 1U ||
        study_exec_read_u64(executable->bytes, 24U) !=
            executable->entry_address ||
        study_exec_read_u64(executable->bytes, 32U) !=
            executable->program_header_offset ||
        study_exec_read_u64(executable->bytes, 40U) !=
            executable->section_header_offset ||
        study_exec_read_u16(executable->bytes, 52U) !=
            STUDY_EXEC_ELF_HEADER_SIZE ||
        study_exec_read_u16(executable->bytes, 54U) !=
            STUDY_EXEC_PROGRAM_HEADER_SIZE ||
        study_exec_read_u16(executable->bytes, 56U) !=
            STUDY_EXEC_PROGRAM_HEADER_COUNT ||
        study_exec_read_u16(executable->bytes, 58U) !=
            STUDY_EXEC_SECTION_HEADER_SIZE ||
        study_exec_read_u16(executable->bytes, 60U) !=
            STUDY_EXEC_SECTION_COUNT ||
        study_exec_read_u16(executable->bytes, 62U) !=
            STUDY_EXEC_SECTION_SHSTRTAB ||
        !study_exec_verify_program_headers(executable)) {
        study_exec_error(
            diagnostic,
            source,
            "ELF executable header or program headers are invalid"
        );
        return false;
    }
    if (!study_exec_section_header_matches(
            executable,
            STUDY_EXEC_SECTION_NULL,
            0U,
            STUDY_EXEC_SHT_NULL,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U,
            0U
        ) ||
        !study_exec_section_header_matches(
            executable,
            STUDY_EXEC_SECTION_TEXT,
            STUDY_EXEC_NAME_TEXT,
            STUDY_EXEC_SHT_PROGBITS,
            STUDY_EXEC_SHF_ALLOC | STUDY_EXEC_SHF_EXECINSTR,
            executable->text_address,
            executable->text_offset,
            executable->text_size,
            0U,
            0U,
            16U,
            0U
        ) ||
        !study_exec_section_header_matches(
            executable,
            STUDY_EXEC_SECTION_SYMTAB,
            STUDY_EXEC_NAME_SYMTAB,
            STUDY_EXEC_SHT_SYMTAB,
            0U,
            0U,
            executable->symbol_offset,
            symbol_bytes,
            STUDY_EXEC_SECTION_STRTAB,
            (uint32_t)object->function_count,
            8U,
            STUDY_EXEC_SYMBOL_SIZE
        ) ||
        !study_exec_section_header_matches(
            executable,
            STUDY_EXEC_SECTION_STRTAB,
            STUDY_EXEC_NAME_STRTAB,
            STUDY_EXEC_SHT_STRTAB,
            0U,
            0U,
            executable->string_offset,
            executable->string_size,
            0U,
            0U,
            1U,
            0U
        ) ||
        !study_exec_section_header_matches(
            executable,
            STUDY_EXEC_SECTION_SHSTRTAB,
            STUDY_EXEC_NAME_SHSTRTAB,
            STUDY_EXEC_SHT_STRTAB,
            0U,
            0U,
            executable->section_string_offset,
            executable->section_string_size,
            0U,
            0U,
            1U,
            0U
        )) {
        study_exec_error(
            diagnostic,
            source,
            "ELF executable section headers are invalid"
        );
        return false;
    }
    if (memcmp(
            &executable->bytes[executable->string_offset],
            &object->bytes[object->string_offset],
            object->string_size
        ) != 0 ||
        memcmp(
            &executable->bytes[executable->section_string_offset],
            study_exec_section_strings,
            sizeof(study_exec_section_strings)
        ) != 0 ||
        !study_exec_verify_text_and_relocations(executable, object) ||
        !study_exec_verify_symbols(executable, object)) {
        study_exec_error(
            diagnostic,
            source,
            "ELF executable contents are invalid"
        );
        return false;
    }
    if (program->start_function >= object->function_count) {
        study_exec_error(diagnostic, source, "ELF executable entry is invalid");
        return false;
    }
    start_symbol = object->function_symbol_indices[program->start_function];
    if (start_symbol >= executable->symbol_count ||
        !study_exec_multiply_size(
            start_symbol,
            STUDY_EXEC_SYMBOL_SIZE,
            &start_entry
        ) ||
        !study_exec_add_size(
            executable->symbol_offset,
            start_entry,
            &start_entry
        ) ||
        study_exec_read_u64(executable->bytes, start_entry + 8U) !=
            executable->entry_address ||
        executable->entry_address < executable->text_address ||
        executable->entry_address - executable->text_address >=
            executable->text_size) {
        study_exec_error(diagnostic, source, "ELF executable entry is invalid");
        return false;
    }
    return true;
}

static const char *study_exec_symbol_name(
    const StudyElf64Object *object,
    size_t symbol_index
) {
    size_t entry = object->symbol_offset +
                   symbol_index * STUDY_EXEC_SYMBOL_SIZE;
    uint32_t name = study_exec_read_u32(object->bytes, entry);

    if ((size_t)name >= object->string_size) {
        return "<invalid>";
    }
    return (const char *)&object->bytes[object->string_offset + name];
}

bool study_elf64_executable_dump(
    FILE *stream,
    const StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t index;

    study_diagnostic_clear(diagnostic);
    if (stream == NULL || !study_elf64_executable_verify(
            executable,
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        if (stream == NULL && diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_exec_error(diagnostic, source, "invalid executable dump stream");
        }
        return false;
    }
    if (fprintf(
            stream,
            "elf64-executable x86-64 [bytes=%zu, entry=0x%016" PRIx64
            ", segments=%zu, sections=%zu] {\n"
            "  header @0x%08x program-headers=0x%08zx "
            "section-headers=0x%08zx\n"
            "  segment p0 LOAD @0x%08x vaddr=0x%016" PRIx64
            " filesz=%zu flags=RX align=4096\n"
            "  segment p1 GNU_STACK size=0 flags=RW align=16\n"
            "  section s1 .text @0x%08zx vaddr=0x%016" PRIx64
            " size=%zu align=16 flags=AX\n"
            "  section s2 .symtab @0x%08zx entries=%zu link=s3\n"
            "  section s3 .strtab @0x%08zx size=%zu\n"
            "  section s4 .shstrtab @0x%08zx size=%zu\n"
            "  symbols:\n",
            executable->length,
            executable->entry_address,
            executable->program_header_count,
            executable->section_count,
            0,
            executable->program_header_offset,
            executable->section_header_offset,
            0,
            executable->base_address,
            executable->load_size,
            executable->text_offset,
            executable->text_address,
            executable->text_size,
            executable->symbol_offset,
            executable->symbol_count,
            executable->string_offset,
            executable->string_size,
            executable->section_string_offset,
            executable->section_string_size
        ) < 0) {
        study_exec_error(diagnostic, source, "could not write executable dump");
        return false;
    }
    for (index = 1U; index < executable->symbol_count; index++) {
        size_t entry = executable->symbol_offset +
                       index * STUDY_EXEC_SYMBOL_SIZE;
        unsigned char info = executable->bytes[entry + 4U];
        const char *binding = (info >> 4U) == 1U ? "global" : "local";
        if (fprintf(
                stream,
                "    sym%zu %s %s function 0x%016" PRIx64
                " size=%" PRIu64 "\n",
                index,
                study_exec_symbol_name(object, index),
                binding,
                study_exec_read_u64(executable->bytes, entry + 8U),
                study_exec_read_u64(executable->bytes, entry + 16U)
            ) < 0) {
            study_exec_error(diagnostic, source, "could not write executable dump");
            return false;
        }
    }
    if (fputs("  applied relocations:\n", stream) == EOF) {
        study_exec_error(diagnostic, source, "could not write executable dump");
        return false;
    }
    for (index = 0U;
         index < executable->applied_relocation_count;
         index++) {
        const StudyElf64AppliedRelocation *applied =
            &executable->applied_relocations[index];
        if (fprintf(
                stream,
                "    rela%zu .text+0x%08zx R_X86_64_PC32 %s "
                "S=0x%016" PRIx64 " A=%" PRId64 " P=0x%016" PRIx64
                " value=%" PRId32 "\n",
                applied->object_relocation_index,
                applied->text_offset,
                study_exec_symbol_name(object, applied->symbol_index),
                applied->symbol_address,
                applied->addend,
                executable->text_address + applied->text_offset,
                applied->value
            ) < 0) {
            study_exec_error(diagnostic, source, "could not write executable dump");
            return false;
        }
    }
    if (fputs("}\n", stream) == EOF || ferror(stream) != 0) {
        study_exec_error(diagnostic, source, "could not write executable dump");
        return false;
    }
    return true;
}

bool study_elf64_executable_write(
    FILE *stream,
    const StudyElf64Executable *executable,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    study_diagnostic_clear(diagnostic);
    if (stream == NULL || !study_elf64_executable_verify(
            executable,
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        if (stream == NULL && diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_exec_error(diagnostic, source, "invalid executable output stream");
        }
        return false;
    }
    if (fwrite(executable->bytes, 1U, executable->length, stream) !=
            executable->length ||
        ferror(stream) != 0) {
        study_exec_error(diagnostic, source, "could not write ELF executable");
        return false;
    }
    return true;
}
