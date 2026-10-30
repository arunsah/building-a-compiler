#include "study/elf64.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    STUDY_ELF64_HEADER_SIZE = 64,
    STUDY_ELF64_SECTION_HEADER_SIZE = 64,
    STUDY_ELF64_SYMBOL_SIZE = 24,
    STUDY_ELF64_RELOCATION_SIZE = 24,
    STUDY_ELF64_SECTION_COUNT = 7,
    STUDY_ELF64_SECTION_NULL = 0,
    STUDY_ELF64_SECTION_TEXT = 1,
    STUDY_ELF64_SECTION_RELA_TEXT = 2,
    STUDY_ELF64_SECTION_SYMTAB = 3,
    STUDY_ELF64_SECTION_STRTAB = 4,
    STUDY_ELF64_SECTION_SHSTRTAB = 5,
    STUDY_ELF64_SECTION_GNU_STACK = 6,
    STUDY_ELF64_SHT_NULL = 0,
    STUDY_ELF64_SHT_PROGBITS = 1,
    STUDY_ELF64_SHT_SYMTAB = 2,
    STUDY_ELF64_SHT_STRTAB = 3,
    STUDY_ELF64_SHT_RELA = 4,
    STUDY_ELF64_SHF_ALLOC = 2,
    STUDY_ELF64_SHF_EXECINSTR = 4,
    STUDY_ELF64_ET_REL = 1,
    STUDY_ELF64_EM_X86_64 = 62,
    STUDY_ELF64_R_X86_64_PC32 = 2,
    STUDY_ELF64_STB_LOCAL = 0,
    STUDY_ELF64_STB_GLOBAL = 1,
    STUDY_ELF64_STT_FUNC = 2
};

static const unsigned char study_elf64_section_strings[] =
    "\0.text\0.rela.text\0.symtab\0.strtab\0.shstrtab\0.note.GNU-stack";

enum {
    STUDY_ELF64_NAME_TEXT = 1,
    STUDY_ELF64_NAME_RELA_TEXT = 7,
    STUDY_ELF64_NAME_SYMTAB = 18,
    STUDY_ELF64_NAME_STRTAB = 26,
    STUDY_ELF64_NAME_SHSTRTAB = 34,
    STUDY_ELF64_NAME_GNU_STACK = 44
};

typedef struct StudyElf64Build {
    StudyElf64Object object;
    size_t *function_name_offsets;
    unsigned char *string_table;
} StudyElf64Build;

void study_elf64_object_init(StudyElf64Object *object) {
    if (object == NULL) {
        return;
    }
    object->bytes = NULL;
    object->length = 0U;
    object->text_offset = 0U;
    object->text_size = 0U;
    object->relocation_offset = 0U;
    object->relocation_count = 0U;
    object->symbol_offset = 0U;
    object->symbol_count = 0U;
    object->string_offset = 0U;
    object->string_size = 0U;
    object->section_string_offset = 0U;
    object->section_string_size = 0U;
    object->section_header_offset = 0U;
    object->section_count = 0U;
    object->function_symbol_indices = NULL;
    object->function_count = 0U;
    object->relocations = NULL;
}

void study_elf64_object_free(StudyElf64Object *object) {
    if (object == NULL) {
        return;
    }
    free(object->bytes);
    free(object->function_symbol_indices);
    free(object->relocations);
    study_elf64_object_init(object);
}

static bool study_elf64_object_is_empty(const StudyElf64Object *object) {
    return object != NULL && object->bytes == NULL && object->length == 0U &&
           object->function_symbol_indices == NULL &&
           object->relocations == NULL;
}

static void study_elf64_error(
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

static bool study_elf64_add_size(size_t left, size_t right, size_t *result) {
    if (left > SIZE_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool study_elf64_multiply_size(
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

static bool study_elf64_align_size(
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
    return study_elf64_add_size(value, addition, result);
}

static void study_elf64_write_u16(
    unsigned char *bytes,
    size_t offset,
    uint16_t value
) {
    bytes[offset] = (unsigned char)(value & UINT16_C(0xff));
    bytes[offset + 1U] = (unsigned char)((value >> 8U) & UINT16_C(0xff));
}

static void study_elf64_write_u32(
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

static void study_elf64_write_u64(
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

static uint16_t study_elf64_read_u16(
    const unsigned char *bytes,
    size_t offset
) {
    return (uint16_t)((uint16_t)bytes[offset] |
                      ((uint16_t)bytes[offset + 1U] << 8U));
}

static uint32_t study_elf64_read_u32(
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

static uint64_t study_elf64_read_u64(
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

static bool study_elf64_function_name(
    const StudyX86Function *function,
    char *buffer,
    size_t capacity
) {
    int length;

    switch (function->kind) {
        case STUDY_X86_FUNCTION_START:
            length = snprintf(buffer, capacity, "_start");
            break;
        case STUDY_X86_FUNCTION_PRINT_I32:
            length = snprintf(buffer, capacity, "study_rt_print_i32");
            break;
        case STUDY_X86_FUNCTION_STUDY:
            length = snprintf(
                buffer,
                capacity,
                "study_fn_%zu",
                function->ir_function
            );
            break;
        default:
            return false;
    }
    return length >= 0 && (size_t)length < capacity;
}

static bool study_elf64_count_calls(
    const StudyX86Program *program,
    size_t *count
) {
    size_t index;

    *count = 0U;
    for (index = 0U; index < program->instruction_count; index++) {
        if (program->instructions[index].opcode == STUDY_X86_CALL) {
            if (*count == SIZE_MAX) {
                return false;
            }
            *count += 1U;
        }
    }
    return true;
}

static bool study_elf64_function_range(
    const StudyX86Program *program,
    const StudyX86Image *image,
    StudyX86FunctionId function_id,
    size_t *start,
    size_t *size
) {
    const StudyX86Function *function;
    const StudyX86Block *block;
    StudyX86InstructionId last_instruction;
    size_t end;

    if (function_id >= program->function_count ||
        function_id >= image->function_count) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->last_block >= program->block_count) {
        return false;
    }
    block = &program->blocks[function->last_block];
    last_instruction = block->last_instruction;
    if (last_instruction >= program->instruction_count ||
        last_instruction >= image->instruction_count ||
        image->instruction_sizes[last_instruction] == 0U ||
        image->instruction_offsets[last_instruction] >
            image->length - image->instruction_sizes[last_instruction]) {
        return false;
    }
    *start = image->function_offsets[function_id];
    end = image->instruction_offsets[last_instruction] +
          image->instruction_sizes[last_instruction];
    if (end < *start) {
        return false;
    }
    *size = end - *start;
    return true;
}

static bool study_elf64_build_string_table(
    StudyElf64Build *build,
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t size = 1U;
    size_t cursor = 1U;
    size_t index;
    char name[64];

    build->function_name_offsets = calloc(
        program->function_count,
        sizeof(*build->function_name_offsets)
    );
    if (build->function_name_offsets == NULL) {
        study_elf64_error(
            diagnostic,
            source,
            "not enough memory for ELF function name offsets"
        );
        return false;
    }
    for (index = 0U; index < program->function_count; index++) {
        size_t length;

        if (!study_elf64_function_name(
                &program->functions[index],
                name,
                sizeof(name)
            )) {
            study_elf64_error(diagnostic, source, "invalid ELF function name");
            return false;
        }
        length = strlen(name) + 1U;
        if (!study_elf64_add_size(size, length, &size)) {
            study_elf64_error(diagnostic, source, "ELF string table is too large");
            return false;
        }
    }
    build->string_table = calloc(size, 1U);
    if (build->string_table == NULL) {
        study_elf64_error(
            diagnostic,
            source,
            "not enough memory for the ELF string table"
        );
        return false;
    }
    for (index = 0U; index < program->function_count; index++) {
        size_t length;

        if (!study_elf64_function_name(
                &program->functions[index],
                name,
                sizeof(name)
            )) {
            study_elf64_error(diagnostic, source, "invalid ELF function name");
            return false;
        }
        length = strlen(name) + 1U;
        build->function_name_offsets[index] = cursor;
        memcpy(&build->string_table[cursor], name, length);
        cursor += length;
    }
    build->object.string_size = size;
    return true;
}

static bool study_elf64_allocate_metadata(
    StudyElf64Build *build,
    const StudyX86Program *program,
    size_t relocation_count,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    StudyElf64Object *object = &build->object;

    object->function_symbol_indices = calloc(
        program->function_count,
        sizeof(*object->function_symbol_indices)
    );
    if (object->function_symbol_indices == NULL) {
        study_elf64_error(
            diagnostic,
            source,
            "not enough memory for ELF function symbols"
        );
        return false;
    }
    if (relocation_count > 0U) {
        object->relocations = calloc(
            relocation_count,
            sizeof(*object->relocations)
        );
        if (object->relocations == NULL) {
            study_elf64_error(
                diagnostic,
                source,
                "not enough memory for ELF relocations"
            );
            return false;
        }
    }
    object->function_count = program->function_count;
    object->relocation_count = relocation_count;
    return true;
}

static bool study_elf64_calculate_layout(
    StudyElf64Object *object,
    size_t text_size,
    size_t function_count,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t cursor = STUDY_ELF64_HEADER_SIZE;
    size_t byte_count;

    if (!study_elf64_align_size(cursor, 16U, &object->text_offset) ||
        !study_elf64_add_size(object->text_offset, text_size, &cursor) ||
        !study_elf64_align_size(cursor, 8U, &object->relocation_offset) ||
        !study_elf64_multiply_size(
            object->relocation_count,
            STUDY_ELF64_RELOCATION_SIZE,
            &byte_count
        ) ||
        !study_elf64_add_size(object->relocation_offset, byte_count, &cursor) ||
        !study_elf64_align_size(cursor, 8U, &object->symbol_offset) ||
        !study_elf64_add_size(function_count, 1U, &object->symbol_count) ||
        !study_elf64_multiply_size(
            object->symbol_count,
            STUDY_ELF64_SYMBOL_SIZE,
            &byte_count
        ) ||
        !study_elf64_add_size(object->symbol_offset, byte_count, &cursor)) {
        study_elf64_error(diagnostic, source, "ELF object layout is too large");
        return false;
    }
    object->string_offset = cursor;
    if (!study_elf64_add_size(cursor, object->string_size, &cursor)) {
        study_elf64_error(diagnostic, source, "ELF object layout is too large");
        return false;
    }
    object->section_string_offset = cursor;
    object->section_string_size = sizeof(study_elf64_section_strings);
    if (!study_elf64_add_size(
            cursor,
            object->section_string_size,
            &cursor
        ) ||
        !study_elf64_align_size(cursor, 8U, &object->section_header_offset) ||
        !study_elf64_multiply_size(
            STUDY_ELF64_SECTION_COUNT,
            STUDY_ELF64_SECTION_HEADER_SIZE,
            &byte_count
        ) ||
        !study_elf64_add_size(
            object->section_header_offset,
            byte_count,
            &object->length
        )) {
        study_elf64_error(diagnostic, source, "ELF object layout is too large");
        return false;
    }
    object->text_size = text_size;
    object->section_count = STUDY_ELF64_SECTION_COUNT;
    return true;
}

static void study_elf64_write_header(StudyElf64Object *object) {
    unsigned char *bytes = object->bytes;

    bytes[0] = UINT8_C(0x7f);
    bytes[1] = (unsigned char)'E';
    bytes[2] = (unsigned char)'L';
    bytes[3] = (unsigned char)'F';
    bytes[4] = UINT8_C(2);
    bytes[5] = UINT8_C(1);
    bytes[6] = UINT8_C(1);
    bytes[7] = UINT8_C(0);
    study_elf64_write_u16(bytes, 16U, STUDY_ELF64_ET_REL);
    study_elf64_write_u16(bytes, 18U, STUDY_ELF64_EM_X86_64);
    study_elf64_write_u32(bytes, 20U, UINT32_C(1));
    study_elf64_write_u64(bytes, 24U, UINT64_C(0));
    study_elf64_write_u64(bytes, 32U, UINT64_C(0));
    study_elf64_write_u64(
        bytes,
        40U,
        (uint64_t)object->section_header_offset
    );
    study_elf64_write_u32(bytes, 48U, UINT32_C(0));
    study_elf64_write_u16(bytes, 52U, STUDY_ELF64_HEADER_SIZE);
    study_elf64_write_u16(bytes, 54U, UINT16_C(0));
    study_elf64_write_u16(bytes, 56U, UINT16_C(0));
    study_elf64_write_u16(bytes, 58U, STUDY_ELF64_SECTION_HEADER_SIZE);
    study_elf64_write_u16(bytes, 60U, STUDY_ELF64_SECTION_COUNT);
    study_elf64_write_u16(bytes, 62U, STUDY_ELF64_SECTION_SHSTRTAB);
}

static void study_elf64_write_symbol(
    StudyElf64Object *object,
    size_t symbol_index,
    uint32_t name,
    unsigned char info,
    uint16_t section,
    uint64_t value,
    uint64_t size
) {
    size_t offset = object->symbol_offset +
                    symbol_index * STUDY_ELF64_SYMBOL_SIZE;

    study_elf64_write_u32(object->bytes, offset, name);
    object->bytes[offset + 4U] = info;
    object->bytes[offset + 5U] = UINT8_C(0);
    study_elf64_write_u16(object->bytes, offset + 6U, section);
    study_elf64_write_u64(object->bytes, offset + 8U, value);
    study_elf64_write_u64(object->bytes, offset + 16U, size);
}

static bool study_elf64_write_symbols(
    StudyElf64Build *build,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    StudyElf64Object *object = &build->object;
    size_t next_local = 1U;
    size_t start_symbol = program->function_count;
    size_t function_id;

    for (function_id = 0U; function_id < program->function_count; function_id++) {
        const StudyX86Function *function = &program->functions[function_id];
        size_t symbol_index;
        size_t value;
        size_t size;
        unsigned char binding;

        if (!study_elf64_function_range(
                program,
                image,
                function_id,
                &value,
                &size
            )) {
            study_elf64_error(diagnostic, source, "invalid ELF function range");
            return false;
        }
        if (function->kind == STUDY_X86_FUNCTION_START) {
            symbol_index = start_symbol;
            binding = STUDY_ELF64_STB_GLOBAL;
        } else {
            symbol_index = next_local;
            next_local += 1U;
            binding = STUDY_ELF64_STB_LOCAL;
        }
        object->function_symbol_indices[function_id] = symbol_index;
        study_elf64_write_symbol(
            object,
            symbol_index,
            (uint32_t)build->function_name_offsets[function_id],
            (unsigned char)((binding << 4U) | STUDY_ELF64_STT_FUNC),
            STUDY_ELF64_SECTION_TEXT,
            (uint64_t)value,
            (uint64_t)size
        );
    }
    if (next_local != start_symbol ||
        object->function_symbol_indices[program->start_function] !=
            start_symbol) {
        study_elf64_error(diagnostic, source, "invalid ELF symbol ordering");
        return false;
    }
    return true;
}

static bool study_elf64_write_relocations(
    StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t instruction_id;
    size_t relocation_index = 0U;

    for (instruction_id = 0U;
         instruction_id < program->instruction_count;
         instruction_id++) {
        const StudyX86Instruction *instruction =
            &program->instructions[instruction_id];
        size_t instruction_offset;
        size_t relocation_offset;
        size_t target;
        size_t symbol_index;
        size_t entry_offset;
        uint64_t info;

        if (instruction->opcode != STUDY_X86_CALL) {
            continue;
        }
        instruction_offset = image->instruction_offsets[instruction_id];
        if (instruction->target_function >= program->function_count ||
            instruction_offset > image->length - 5U ||
            image->instruction_sizes[instruction_id] != 5U ||
            image->bytes[instruction_offset] != UINT8_C(0xe8)) {
            study_elf64_error(
                diagnostic,
                source,
                "a call cannot be represented as an ELF relocation"
            );
            return false;
        }
        target = instruction->target_function;
        symbol_index = object->function_symbol_indices[target];
        relocation_offset = instruction_offset + 1U;
        object->relocations[relocation_index].instruction = instruction_id;
        object->relocations[relocation_index].offset = relocation_offset;
        object->relocations[relocation_index].target_function = target;
        object->relocations[relocation_index].symbol_index = symbol_index;
        object->relocations[relocation_index].addend = -4;

        memset(
            &object->bytes[object->text_offset + relocation_offset],
            0,
            4U
        );
        entry_offset = object->relocation_offset +
                       relocation_index * STUDY_ELF64_RELOCATION_SIZE;
        info = ((uint64_t)symbol_index << 32U) |
               STUDY_ELF64_R_X86_64_PC32;
        study_elf64_write_u64(
            object->bytes,
            entry_offset,
            (uint64_t)relocation_offset
        );
        study_elf64_write_u64(object->bytes, entry_offset + 8U, info);
        study_elf64_write_u64(
            object->bytes,
            entry_offset + 16U,
            UINT64_MAX - UINT64_C(3)
        );
        relocation_index += 1U;
    }
    if (relocation_index != object->relocation_count) {
        study_elf64_error(diagnostic, source, "ELF relocation count changed");
        return false;
    }
    return true;
}

static void study_elf64_write_section_header(
    StudyElf64Object *object,
    size_t section_index,
    uint32_t name,
    uint32_t type,
    uint64_t flags,
    uint64_t offset,
    uint64_t size,
    uint32_t link,
    uint32_t info,
    uint64_t alignment,
    uint64_t entry_size
) {
    size_t base = object->section_header_offset +
                  section_index * STUDY_ELF64_SECTION_HEADER_SIZE;

    study_elf64_write_u32(object->bytes, base, name);
    study_elf64_write_u32(object->bytes, base + 4U, type);
    study_elf64_write_u64(object->bytes, base + 8U, flags);
    study_elf64_write_u64(object->bytes, base + 16U, UINT64_C(0));
    study_elf64_write_u64(object->bytes, base + 24U, offset);
    study_elf64_write_u64(object->bytes, base + 32U, size);
    study_elf64_write_u32(object->bytes, base + 40U, link);
    study_elf64_write_u32(object->bytes, base + 44U, info);
    study_elf64_write_u64(object->bytes, base + 48U, alignment);
    study_elf64_write_u64(object->bytes, base + 56U, entry_size);
}

static void study_elf64_write_section_headers(StudyElf64Object *object) {
    size_t relocation_size =
        object->relocation_count * STUDY_ELF64_RELOCATION_SIZE;
    size_t symbol_size = object->symbol_count * STUDY_ELF64_SYMBOL_SIZE;
    size_t note_offset = object->section_string_offset +
                         object->section_string_size;

    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_NULL,
        0U,
        STUDY_ELF64_SHT_NULL,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U,
        0U
    );
    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_TEXT,
        STUDY_ELF64_NAME_TEXT,
        STUDY_ELF64_SHT_PROGBITS,
        STUDY_ELF64_SHF_ALLOC | STUDY_ELF64_SHF_EXECINSTR,
        (uint64_t)object->text_offset,
        (uint64_t)object->text_size,
        0U,
        0U,
        16U,
        0U
    );
    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_RELA_TEXT,
        STUDY_ELF64_NAME_RELA_TEXT,
        STUDY_ELF64_SHT_RELA,
        0U,
        (uint64_t)object->relocation_offset,
        (uint64_t)relocation_size,
        STUDY_ELF64_SECTION_SYMTAB,
        STUDY_ELF64_SECTION_TEXT,
        8U,
        STUDY_ELF64_RELOCATION_SIZE
    );
    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_SYMTAB,
        STUDY_ELF64_NAME_SYMTAB,
        STUDY_ELF64_SHT_SYMTAB,
        0U,
        (uint64_t)object->symbol_offset,
        (uint64_t)symbol_size,
        STUDY_ELF64_SECTION_STRTAB,
        (uint32_t)object->function_count,
        8U,
        STUDY_ELF64_SYMBOL_SIZE
    );
    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_STRTAB,
        STUDY_ELF64_NAME_STRTAB,
        STUDY_ELF64_SHT_STRTAB,
        0U,
        (uint64_t)object->string_offset,
        (uint64_t)object->string_size,
        0U,
        0U,
        1U,
        0U
    );
    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_SHSTRTAB,
        STUDY_ELF64_NAME_SHSTRTAB,
        STUDY_ELF64_SHT_STRTAB,
        0U,
        (uint64_t)object->section_string_offset,
        (uint64_t)object->section_string_size,
        0U,
        0U,
        1U,
        0U
    );
    study_elf64_write_section_header(
        object,
        STUDY_ELF64_SECTION_GNU_STACK,
        STUDY_ELF64_NAME_GNU_STACK,
        STUDY_ELF64_SHT_PROGBITS,
        0U,
        (uint64_t)note_offset,
        0U,
        0U,
        0U,
        1U,
        0U
    );
}

static bool study_elf64_range_is_valid(
    size_t offset,
    size_t size,
    size_t length
) {
    return offset <= length && size <= length - offset;
}

static bool study_elf64_is_relocation_byte(
    const StudyElf64Object *object,
    size_t text_offset
) {
    size_t index;

    for (index = 0U; index < object->relocation_count; index++) {
        size_t offset = object->relocations[index].offset;

        if (text_offset >= offset && text_offset - offset < 4U) {
            return true;
        }
    }
    return false;
}

static bool study_elf64_verify_section_header(
    const StudyElf64Object *object,
    size_t section_index,
    uint32_t name,
    uint32_t type,
    uint64_t flags,
    uint64_t offset,
    uint64_t size,
    uint32_t link,
    uint32_t info,
    uint64_t alignment,
    uint64_t entry_size
) {
    size_t base = object->section_header_offset +
                  section_index * STUDY_ELF64_SECTION_HEADER_SIZE;
    const unsigned char *bytes = object->bytes;

    return study_elf64_read_u32(bytes, base) == name &&
           study_elf64_read_u32(bytes, base + 4U) == type &&
           study_elf64_read_u64(bytes, base + 8U) == flags &&
           study_elf64_read_u64(bytes, base + 16U) == 0U &&
           study_elf64_read_u64(bytes, base + 24U) == offset &&
           study_elf64_read_u64(bytes, base + 32U) == size &&
           study_elf64_read_u32(bytes, base + 40U) == link &&
           study_elf64_read_u32(bytes, base + 44U) == info &&
           study_elf64_read_u64(bytes, base + 48U) == alignment &&
           study_elf64_read_u64(bytes, base + 56U) == entry_size;
}

static bool study_elf64_verify_sections(const StudyElf64Object *object) {
    uint64_t relocation_size =
        (uint64_t)object->relocation_count * STUDY_ELF64_RELOCATION_SIZE;
    uint64_t symbol_size =
        (uint64_t)object->symbol_count * STUDY_ELF64_SYMBOL_SIZE;
    uint64_t note_offset = (uint64_t)object->section_string_offset +
                           (uint64_t)object->section_string_size;

    return study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_NULL,
               0U,
               STUDY_ELF64_SHT_NULL,
               0U,
               0U,
               0U,
               0U,
               0U,
               0U,
               0U
           ) &&
           study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_TEXT,
               STUDY_ELF64_NAME_TEXT,
               STUDY_ELF64_SHT_PROGBITS,
               STUDY_ELF64_SHF_ALLOC | STUDY_ELF64_SHF_EXECINSTR,
               (uint64_t)object->text_offset,
               (uint64_t)object->text_size,
               0U,
               0U,
               16U,
               0U
           ) &&
           study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_RELA_TEXT,
               STUDY_ELF64_NAME_RELA_TEXT,
               STUDY_ELF64_SHT_RELA,
               0U,
               (uint64_t)object->relocation_offset,
               relocation_size,
               STUDY_ELF64_SECTION_SYMTAB,
               STUDY_ELF64_SECTION_TEXT,
               8U,
               STUDY_ELF64_RELOCATION_SIZE
           ) &&
           study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_SYMTAB,
               STUDY_ELF64_NAME_SYMTAB,
               STUDY_ELF64_SHT_SYMTAB,
               0U,
               (uint64_t)object->symbol_offset,
               symbol_size,
               STUDY_ELF64_SECTION_STRTAB,
               (uint32_t)object->function_count,
               8U,
               STUDY_ELF64_SYMBOL_SIZE
           ) &&
           study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_STRTAB,
               STUDY_ELF64_NAME_STRTAB,
               STUDY_ELF64_SHT_STRTAB,
               0U,
               (uint64_t)object->string_offset,
               (uint64_t)object->string_size,
               0U,
               0U,
               1U,
               0U
           ) &&
           study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_SHSTRTAB,
               STUDY_ELF64_NAME_SHSTRTAB,
               STUDY_ELF64_SHT_STRTAB,
               0U,
               (uint64_t)object->section_string_offset,
               (uint64_t)object->section_string_size,
               0U,
               0U,
               1U,
               0U
           ) &&
           study_elf64_verify_section_header(
               object,
               STUDY_ELF64_SECTION_GNU_STACK,
               STUDY_ELF64_NAME_GNU_STACK,
               STUDY_ELF64_SHT_PROGBITS,
               0U,
               note_offset,
               0U,
               0U,
               0U,
               1U,
               0U
           );
}

bool study_elf64_object_verify(
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t expected_relocations;
    size_t relocation_bytes;
    size_t symbol_bytes;
    size_t section_bytes;
    size_t index;

    study_diagnostic_clear(diagnostic);
    if (source == NULL ||
        !study_x86_64_image_verify(program, image, source, diagnostic)) {
        return false;
    }
    if (object == NULL || object->bytes == NULL || object->length == 0U ||
        object->function_count != program->function_count ||
        object->function_symbol_indices == NULL ||
        object->text_size != image->length ||
        object->section_count != STUDY_ELF64_SECTION_COUNT ||
        object->symbol_count != program->function_count + 1U ||
        object->section_string_size !=
            sizeof(study_elf64_section_strings) ||
        !study_elf64_count_calls(program, &expected_relocations) ||
        object->relocation_count != expected_relocations ||
        (object->relocation_count > 0U && object->relocations == NULL) ||
        !study_elf64_multiply_size(
            object->relocation_count,
            STUDY_ELF64_RELOCATION_SIZE,
            &relocation_bytes
        ) ||
        !study_elf64_multiply_size(
            object->symbol_count,
            STUDY_ELF64_SYMBOL_SIZE,
            &symbol_bytes
        ) ||
        !study_elf64_multiply_size(
            object->section_count,
            STUDY_ELF64_SECTION_HEADER_SIZE,
            &section_bytes
        ) ||
        !study_elf64_range_is_valid(
            object->text_offset,
            object->text_size,
            object->length
        ) ||
        !study_elf64_range_is_valid(
            object->relocation_offset,
            relocation_bytes,
            object->length
        ) ||
        !study_elf64_range_is_valid(
            object->symbol_offset,
            symbol_bytes,
            object->length
        ) ||
        !study_elf64_range_is_valid(
            object->string_offset,
            object->string_size,
            object->length
        ) ||
        !study_elf64_range_is_valid(
            object->section_string_offset,
            object->section_string_size,
            object->length
        ) ||
        !study_elf64_range_is_valid(
            object->section_header_offset,
            section_bytes,
            object->length
        ) ||
        object->section_header_offset + section_bytes != object->length) {
        study_elf64_error(
            diagnostic,
            source,
            "ELF object metadata does not describe a complete object"
        );
        return false;
    }

    if (object->bytes[0] != UINT8_C(0x7f) ||
        object->bytes[1] != (unsigned char)'E' ||
        object->bytes[2] != (unsigned char)'L' ||
        object->bytes[3] != (unsigned char)'F' ||
        object->bytes[4] != UINT8_C(2) ||
        object->bytes[5] != UINT8_C(1) ||
        object->bytes[6] != UINT8_C(1) ||
        study_elf64_read_u16(object->bytes, 16U) != STUDY_ELF64_ET_REL ||
        study_elf64_read_u16(object->bytes, 18U) != STUDY_ELF64_EM_X86_64 ||
        study_elf64_read_u32(object->bytes, 20U) != UINT32_C(1) ||
        study_elf64_read_u64(object->bytes, 24U) != UINT64_C(0) ||
        study_elf64_read_u64(object->bytes, 32U) != UINT64_C(0) ||
        study_elf64_read_u64(object->bytes, 40U) !=
            (uint64_t)object->section_header_offset ||
        study_elf64_read_u16(object->bytes, 52U) !=
            STUDY_ELF64_HEADER_SIZE ||
        study_elf64_read_u16(object->bytes, 58U) !=
            STUDY_ELF64_SECTION_HEADER_SIZE ||
        study_elf64_read_u16(object->bytes, 60U) !=
            STUDY_ELF64_SECTION_COUNT ||
        study_elf64_read_u16(object->bytes, 62U) !=
            STUDY_ELF64_SECTION_SHSTRTAB ||
        memcmp(
            &object->bytes[object->section_string_offset],
            study_elf64_section_strings,
            sizeof(study_elf64_section_strings)
        ) != 0 ||
        !study_elf64_verify_sections(object)) {
        study_elf64_error(diagnostic, source, "ELF header or sections are invalid");
        return false;
    }

    for (index = 0U; index < image->length; index++) {
        unsigned char expected = study_elf64_is_relocation_byte(object, index)
                                     ? UINT8_C(0)
                                     : image->bytes[index];

        if (object->bytes[object->text_offset + index] != expected) {
            study_elf64_error(
                diagnostic,
                source,
                "ELF .text does not match the encoded image"
            );
            return false;
        }
    }

    for (index = 0U; index < object->function_count; index++) {
        const StudyX86Function *function = &program->functions[index];
        size_t symbol_index = object->function_symbol_indices[index];
        size_t symbol_offset;
        uint32_t name_offset;
        size_t value;
        size_t size;
        unsigned char expected_binding =
            function->kind == STUDY_X86_FUNCTION_START
                ? STUDY_ELF64_STB_GLOBAL
                : STUDY_ELF64_STB_LOCAL;
        char expected_name[64];
        const unsigned char *name;
        size_t remaining;

        if (!study_elf64_function_range(
                program,
                image,
                index,
                &value,
                &size
            ) ||
            symbol_index == 0U || symbol_index >= object->symbol_count) {
            study_elf64_error(diagnostic, source, "ELF function symbol is invalid");
            return false;
        }
        symbol_offset = object->symbol_offset +
                        symbol_index * STUDY_ELF64_SYMBOL_SIZE;
        name_offset = study_elf64_read_u32(object->bytes, symbol_offset);
        if ((size_t)name_offset >= object->string_size ||
            !study_elf64_function_name(
                function,
                expected_name,
                sizeof(expected_name)
            )) {
            study_elf64_error(diagnostic, source, "ELF function name is invalid");
            return false;
        }
        name = &object->bytes[object->string_offset + (size_t)name_offset];
        remaining = object->string_size - (size_t)name_offset;
        if (memchr(name, '\0', remaining) == NULL ||
            strcmp((const char *)name, expected_name) != 0 ||
            object->bytes[symbol_offset + 4U] !=
                (unsigned char)((expected_binding << 4U) |
                                STUDY_ELF64_STT_FUNC) ||
            study_elf64_read_u16(object->bytes, symbol_offset + 6U) !=
                STUDY_ELF64_SECTION_TEXT ||
            study_elf64_read_u64(object->bytes, symbol_offset + 8U) !=
                (uint64_t)value ||
            study_elf64_read_u64(object->bytes, symbol_offset + 16U) !=
                (uint64_t)size) {
            study_elf64_error(diagnostic, source, "ELF function symbol is invalid");
            return false;
        }
    }

    for (index = 0U; index < object->relocation_count; index++) {
        const StudyElf64Relocation *relocation = &object->relocations[index];
        size_t entry = object->relocation_offset +
                       index * STUDY_ELF64_RELOCATION_SIZE;
        uint64_t expected_info =
            ((uint64_t)relocation->symbol_index << 32U) |
            STUDY_ELF64_R_X86_64_PC32;

        if (relocation->instruction >= program->instruction_count ||
            program->instructions[relocation->instruction].opcode !=
                STUDY_X86_CALL ||
            relocation->target_function >= program->function_count ||
            relocation->symbol_index !=
                object->function_symbol_indices[relocation->target_function] ||
            relocation->offset !=
                image->instruction_offsets[relocation->instruction] + 1U ||
            relocation->addend != -4 ||
            study_elf64_read_u64(object->bytes, entry) !=
                (uint64_t)relocation->offset ||
            study_elf64_read_u64(object->bytes, entry + 8U) != expected_info ||
            study_elf64_read_u64(object->bytes, entry + 16U) !=
                UINT64_MAX - UINT64_C(3)) {
            study_elf64_error(diagnostic, source, "ELF relocation is invalid");
            return false;
        }
    }
    return true;
}

bool study_elf64_object_build(
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyElf64Object *output,
    StudyDiagnostic *diagnostic
) {
    StudyElf64Build build;
    size_t relocation_count;
    bool success = false;

    study_elf64_object_init(&build.object);
    build.function_name_offsets = NULL;
    build.string_table = NULL;
    study_diagnostic_clear(diagnostic);

    if (!study_elf64_object_is_empty(output)) {
        study_elf64_error(
            diagnostic,
            source,
            "ELF writing requires an initialised empty output object"
        );
        return false;
    }
    if (source == NULL ||
        !study_x86_64_image_verify(program, image, source, diagnostic)) {
        return false;
    }
    if (program->function_count == 0U ||
        program->function_count > UINT32_MAX ||
        !study_elf64_count_calls(program, &relocation_count) ||
        !study_elf64_build_string_table(
            &build,
            program,
            source,
            diagnostic
        ) ||
        build.object.string_size > UINT32_MAX ||
        !study_elf64_allocate_metadata(
            &build,
            program,
            relocation_count,
            source,
            diagnostic
        ) ||
        !study_elf64_calculate_layout(
            &build.object,
            image->length,
            program->function_count,
            source,
            diagnostic
        )) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_elf64_error(diagnostic, source, "ELF object is too large");
        }
        goto cleanup;
    }
    build.object.bytes = calloc(build.object.length, 1U);
    if (build.object.bytes == NULL) {
        study_elf64_error(
            diagnostic,
            source,
            "not enough memory for the ELF object"
        );
        goto cleanup;
    }

    study_elf64_write_header(&build.object);
    memcpy(
        &build.object.bytes[build.object.text_offset],
        image->bytes,
        image->length
    );
    memcpy(
        &build.object.bytes[build.object.string_offset],
        build.string_table,
        build.object.string_size
    );
    memcpy(
        &build.object.bytes[build.object.section_string_offset],
        study_elf64_section_strings,
        sizeof(study_elf64_section_strings)
    );
    if (!study_elf64_write_symbols(
            &build,
            program,
            image,
            source,
            diagnostic
        ) ||
        !study_elf64_write_relocations(
            &build.object,
            program,
            image,
            source,
            diagnostic
        )) {
        goto cleanup;
    }
    study_elf64_write_section_headers(&build.object);
    if (!study_elf64_object_verify(
            &build.object,
            program,
            image,
            source,
            diagnostic
        )) {
        goto cleanup;
    }

    *output = build.object;
    study_elf64_object_init(&build.object);
    success = true;

cleanup:
    free(build.function_name_offsets);
    free(build.string_table);
    study_elf64_object_free(&build.object);
    return success;
}

bool study_elf64_object_dump(
    FILE *stream,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t function_id;
    size_t index;

    if (stream == NULL ||
        !study_elf64_object_verify(
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_elf64_error(diagnostic, source, "ELF dump requires an object");
        }
        return false;
    }
    if (fprintf(
            stream,
            "elf64-relocatable x86-64 [bytes=%zu, sections=%zu] {\n",
            object->length,
            object->section_count
        ) < 0 ||
        fprintf(
            stream,
            "  header @0x00000000 section-headers=0x%08zx\n",
            object->section_header_offset
        ) < 0 ||
        fprintf(
            stream,
            "  section s1 .text @0x%08zx size=%zu align=16 flags=AX\n",
            object->text_offset,
            object->text_size
        ) < 0 ||
        fprintf(
            stream,
            "  section s2 .rela.text @0x%08zx entries=%zu link=s3 info=s1\n",
            object->relocation_offset,
            object->relocation_count
        ) < 0 ||
        fprintf(
            stream,
            "  section s3 .symtab @0x%08zx entries=%zu link=s4\n",
            object->symbol_offset,
            object->symbol_count
        ) < 0 ||
        fprintf(
            stream,
            "  section s4 .strtab @0x%08zx size=%zu\n",
            object->string_offset,
            object->string_size
        ) < 0 ||
        fprintf(
            stream,
            "  section s5 .shstrtab @0x%08zx size=%zu\n",
            object->section_string_offset,
            object->section_string_size
        ) < 0 ||
        fputs("  section s6 .note.GNU-stack size=0 flags=-\n", stream) == EOF ||
        fputs("  symbols:\n", stream) == EOF) {
        goto io_error;
    }
    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        const StudyX86Function *function = &program->functions[function_id];
        size_t start;
        size_t size;
        char name[64];

        if (!study_elf64_function_range(
                program,
                image,
                function_id,
                &start,
                &size
            ) ||
            !study_elf64_function_name(function, name, sizeof(name)) ||
            fprintf(
                stream,
                "    sym%zu %s %s function .text+0x%08zx size=%zu\n",
                object->function_symbol_indices[function_id],
                name,
                function->kind == STUDY_X86_FUNCTION_START
                    ? "global"
                    : "local",
                start,
                size
            ) < 0) {
            goto io_error;
        }
    }
    if (fputs("  relocations:\n", stream) == EOF) {
        goto io_error;
    }
    for (index = 0U; index < object->relocation_count; index++) {
        const StudyElf64Relocation *relocation = &object->relocations[index];
        char name[64];

        if (!study_elf64_function_name(
                &program->functions[relocation->target_function],
                name,
                sizeof(name)
            ) ||
            fprintf(
                stream,
                "    rela%zu .text+0x%08zx R_X86_64_PC32 %s addend=%" PRId64
                " from=i%zu\n",
                index,
                relocation->offset,
                name,
                relocation->addend,
                relocation->instruction
            ) < 0) {
            goto io_error;
        }
    }
    if (fputs("}\n", stream) == EOF) {
        goto io_error;
    }
    return true;

io_error:
    study_diagnostic_set(
        diagnostic,
        STUDY_DIAGNOSTIC_IO,
        source->path,
        "could not write ELF object dump"
    );
    return false;
}

bool study_elf64_object_write(
    FILE *stream,
    const StudyElf64Object *object,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    if (stream == NULL ||
        !study_elf64_object_verify(
            object,
            program,
            image,
            source,
            diagnostic
        )) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_elf64_error(diagnostic, source, "ELF output requires an object");
        }
        return false;
    }
    if (fwrite(object->bytes, 1U, object->length, stream) != object->length) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            source->path,
            "could not write the ELF object"
        );
        return false;
    }
    return true;
}
