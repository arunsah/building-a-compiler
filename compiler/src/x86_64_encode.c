#include "study/x86_64_encode.h"

#include "study/x86_64_verify.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { STUDY_X86_IMAGE_INITIAL_CAPACITY = 256 };

typedef enum StudyX86FixupKind {
    STUDY_X86_FIXUP_FUNCTION = 0,
    STUDY_X86_FIXUP_BLOCK
} StudyX86FixupKind;

typedef struct StudyX86Fixup {
    StudyX86FixupKind kind;
    size_t target;
    size_t displacement_offset;
    size_t next_offset;
    StudySourceRange range;
} StudyX86Fixup;

typedef struct StudyX86Encoder {
    const StudyX86Program *program;
    const StudySource *source;
    StudyX86Image image;
    StudyX86Fixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;
    StudyDiagnostic *diagnostic;
} StudyX86Encoder;

void study_x86_64_image_init(StudyX86Image *image) {
    if (image == NULL) {
        return;
    }

    image->bytes = NULL;
    image->length = 0U;
    image->capacity = 0U;
    image->function_offsets = NULL;
    image->function_count = 0U;
    image->block_offsets = NULL;
    image->block_count = 0U;
    image->instruction_offsets = NULL;
    image->instruction_sizes = NULL;
    image->instruction_count = 0U;
    image->entry_offset = SIZE_MAX;
}

void study_x86_64_image_free(StudyX86Image *image) {
    if (image == NULL) {
        return;
    }

    free(image->bytes);
    free(image->function_offsets);
    free(image->block_offsets);
    free(image->instruction_offsets);
    free(image->instruction_sizes);
    study_x86_64_image_init(image);
}

static bool study_x86_image_is_empty(const StudyX86Image *image) {
    return image != NULL && image->bytes == NULL && image->length == 0U &&
           image->capacity == 0U && image->function_offsets == NULL &&
           image->function_count == 0U && image->block_offsets == NULL &&
           image->block_count == 0U && image->instruction_offsets == NULL &&
           image->instruction_sizes == NULL &&
           image->instruction_count == 0U && image->entry_offset == SIZE_MAX;
}

static void study_x86_encode_error(
    StudyX86Encoder *encoder,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        encoder->diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        encoder->source != NULL ? encoder->source->path : NULL,
        "could not encode x86-64 instructions: %s",
        message
    );
    if (range != NULL && range->start.line > 0U) {
        study_diagnostic_set_location(
            encoder->diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static bool study_x86_reserve_bytes(
    StudyX86Encoder *encoder,
    size_t additional
) {
    size_t required;
    size_t capacity;
    unsigned char *grown;

    if (additional > SIZE_MAX - encoder->image.length) {
        study_x86_encode_error(encoder, NULL, "the code image is too large");
        return false;
    }
    required = encoder->image.length + additional;
    if (required <= encoder->image.capacity) {
        return true;
    }

    capacity = encoder->image.capacity == 0U
                   ? STUDY_X86_IMAGE_INITIAL_CAPACITY
                   : encoder->image.capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2U) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    grown = realloc(encoder->image.bytes, capacity);
    if (grown == NULL) {
        study_x86_encode_error(
            encoder,
            NULL,
            "not enough memory for the code image"
        );
        return false;
    }
    encoder->image.bytes = grown;
    encoder->image.capacity = capacity;
    return true;
}

static bool study_x86_emit_u8(StudyX86Encoder *encoder, uint8_t value) {
    if (!study_x86_reserve_bytes(encoder, 1U)) {
        return false;
    }
    encoder->image.bytes[encoder->image.length++] = value;
    return true;
}

static bool study_x86_emit_u32(StudyX86Encoder *encoder, uint32_t value) {
    size_t index;

    if (!study_x86_reserve_bytes(encoder, 4U)) {
        return false;
    }
    for (index = 0U; index < 4U; index++) {
        encoder->image.bytes[encoder->image.length++] =
            (unsigned char)((value >> (index * 8U)) & UINT32_C(0xff));
    }
    return true;
}

static bool study_x86_emit_u64(StudyX86Encoder *encoder, uint64_t value) {
    size_t index;

    if (!study_x86_reserve_bytes(encoder, 8U)) {
        return false;
    }
    for (index = 0U; index < 8U; index++) {
        encoder->image.bytes[encoder->image.length++] =
            (unsigned char)((value >> (index * 8U)) & UINT64_C(0xff));
    }
    return true;
}

static void study_x86_patch_i32(
    StudyX86Image *image,
    size_t offset,
    int32_t value
) {
    uint32_t bits = (uint32_t)value;
    size_t index;

    for (index = 0U; index < 4U; index++) {
        image->bytes[offset + index] =
            (unsigned char)((bits >> (index * 8U)) & UINT32_C(0xff));
    }
}

static bool study_x86_register_code(
    StudyX86Register register_id,
    uint8_t *code
) {
    switch (register_id) {
        case STUDY_X86_REGISTER_RAX:
            *code = 0U;
            return true;
        case STUDY_X86_REGISTER_RCX:
            *code = 1U;
            return true;
        case STUDY_X86_REGISTER_RDX:
            *code = 2U;
            return true;
        case STUDY_X86_REGISTER_RSP:
            *code = 4U;
            return true;
        case STUDY_X86_REGISTER_RBP:
            *code = 5U;
            return true;
        case STUDY_X86_REGISTER_RSI:
            *code = 6U;
            return true;
        case STUDY_X86_REGISTER_RDI:
            *code = 7U;
            return true;
        case STUDY_X86_REGISTER_R8:
            *code = 8U;
            return true;
        case STUDY_X86_REGISTER_R9:
            *code = 9U;
            return true;
    }

    return false;
}

static bool study_x86_operand_register_code(
    const StudyX86Operand *operand,
    uint8_t *code
) {
    return operand->kind == STUDY_X86_OPERAND_REGISTER &&
           study_x86_register_code(operand->as.register_id, code);
}

static bool study_x86_operand_rm_code(
    const StudyX86Operand *operand,
    uint8_t *code
) {
    if (operand->kind == STUDY_X86_OPERAND_REGISTER) {
        return study_x86_register_code(operand->as.register_id, code);
    }
    if (operand->kind == STUDY_X86_OPERAND_MEMORY) {
        return study_x86_register_code(operand->as.memory.base, code);
    }
    return false;
}

static bool study_x86_emit_rex(
    StudyX86Encoder *encoder,
    StudyX86Width width,
    uint8_t reg_code,
    uint8_t rm_code,
    bool force
) {
    uint8_t rex = UINT8_C(0x40);

    if (width == STUDY_X86_WIDTH_64) {
        rex |= UINT8_C(0x08);
    }
    if ((reg_code & UINT8_C(0x08)) != 0U) {
        rex |= UINT8_C(0x04);
    }
    if ((rm_code & UINT8_C(0x08)) != 0U) {
        rex |= UINT8_C(0x01);
    }
    if (rex != UINT8_C(0x40) || force) {
        return study_x86_emit_u8(encoder, rex);
    }
    return true;
}

static bool study_x86_emit_modrm(
    StudyX86Encoder *encoder,
    uint8_t reg_field,
    const StudyX86Operand *rm_operand
) {
    uint8_t rm_code;
    uint8_t mod;
    int32_t displacement;
    bool sib;

    if (!study_x86_operand_rm_code(rm_operand, &rm_code)) {
        return false;
    }
    if (rm_operand->kind == STUDY_X86_OPERAND_REGISTER) {
        return study_x86_emit_u8(
            encoder,
            (uint8_t)(UINT8_C(0xc0) |
                      (uint8_t)((reg_field & UINT8_C(0x07)) << 3U) |
                      (rm_code & UINT8_C(0x07)))
        );
    }

    displacement = rm_operand->as.memory.displacement;
    if (displacement == 0 && (rm_code & UINT8_C(0x07)) != UINT8_C(0x05)) {
        mod = 0U;
    } else if (displacement >= INT8_MIN && displacement <= INT8_MAX) {
        mod = 1U;
    } else {
        mod = 2U;
    }
    sib = (rm_code & UINT8_C(0x07)) == UINT8_C(0x04);
    if (!study_x86_emit_u8(
            encoder,
            (uint8_t)((uint8_t)(mod << 6U) |
                      (uint8_t)((reg_field & UINT8_C(0x07)) << 3U) |
                      (sib ? UINT8_C(0x04) :
                             (rm_code & UINT8_C(0x07))))
        )) {
        return false;
    }
    if (sib && !study_x86_emit_u8(
                   encoder,
                   (uint8_t)(UINT8_C(0x20) | (rm_code & UINT8_C(0x07)))
               )) {
        return false;
    }
    if (mod == 1U) {
        return study_x86_emit_u8(encoder, (uint8_t)displacement);
    }
    if (mod == 2U) {
        return study_x86_emit_u32(encoder, (uint32_t)displacement);
    }
    return true;
}

static bool study_x86_emit_rm_instruction(
    StudyX86Encoder *encoder,
    StudyX86Width width,
    uint8_t opcode,
    uint8_t reg_code,
    const StudyX86Operand *rm_operand
) {
    uint8_t rm_code;

    return study_x86_operand_rm_code(rm_operand, &rm_code) &&
           study_x86_emit_rex(
               encoder,
               width,
               reg_code,
               rm_code,
               width == STUDY_X86_WIDTH_8
           ) &&
           study_x86_emit_u8(encoder, opcode) &&
           study_x86_emit_modrm(encoder, reg_code, rm_operand);
}

static bool study_x86_emit_group_immediate(
    StudyX86Encoder *encoder,
    const StudyX86Operand *immediate,
    const StudyX86Operand *destination,
    uint8_t group
) {
    uint8_t rm_code;

    if (!study_x86_operand_rm_code(destination, &rm_code) ||
        !study_x86_emit_rex(
            encoder,
            destination->width,
            group,
            rm_code,
            destination->width == STUDY_X86_WIDTH_8
        ) ||
        !study_x86_emit_u8(
            encoder,
            destination->width == STUDY_X86_WIDTH_8
                ? UINT8_C(0x80)
                : UINT8_C(0x81)
        ) ||
        !study_x86_emit_modrm(encoder, group, destination)) {
        return false;
    }
    if (destination->width == STUDY_X86_WIDTH_8) {
        return study_x86_emit_u8(
            encoder,
            (uint8_t)immediate->as.immediate.value
        );
    }
    if (destination->width == STUDY_X86_WIDTH_64) {
        int64_t signed_value = (int64_t)immediate->as.immediate.value;

        if ((!immediate->as.immediate.is_signed &&
             immediate->as.immediate.value > (uint64_t)INT32_MAX) ||
            (immediate->as.immediate.is_signed &&
             (signed_value < INT32_MIN || signed_value > INT32_MAX))) {
            return false;
        }
    }
    return study_x86_emit_u32(
        encoder,
        (uint32_t)immediate->as.immediate.value
    );
}

static bool study_x86_emit_mov(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    const StudyX86Operand *source = &instruction->first;
    const StudyX86Operand *destination = &instruction->second;
    uint8_t source_code;
    uint8_t destination_code;

    if (source->kind == STUDY_X86_OPERAND_IMMEDIATE) {
        if (destination->kind == STUDY_X86_OPERAND_REGISTER) {
            if (!study_x86_operand_register_code(destination, &destination_code) ||
                !study_x86_emit_rex(
                    encoder,
                    destination->width,
                    0U,
                    destination_code,
                    destination->width == STUDY_X86_WIDTH_8
                ) ||
                !study_x86_emit_u8(
                    encoder,
                    (uint8_t)((destination->width == STUDY_X86_WIDTH_8
                                   ? UINT8_C(0xb0)
                                   : UINT8_C(0xb8)) |
                              (destination_code & UINT8_C(0x07)))
                )) {
                return false;
            }
            if (destination->width == STUDY_X86_WIDTH_8) {
                return study_x86_emit_u8(
                    encoder,
                    (uint8_t)source->as.immediate.value
                );
            }
            if (destination->width == STUDY_X86_WIDTH_32) {
                return study_x86_emit_u32(
                    encoder,
                    (uint32_t)source->as.immediate.value
                );
            }
            return study_x86_emit_u64(
                encoder,
                source->as.immediate.value
            );
        }
        if (!study_x86_operand_rm_code(destination, &destination_code) ||
            !study_x86_emit_rex(
                encoder,
                destination->width,
                0U,
                destination_code,
                destination->width == STUDY_X86_WIDTH_8
            ) ||
            !study_x86_emit_u8(
                encoder,
                destination->width == STUDY_X86_WIDTH_8
                    ? UINT8_C(0xc6)
                    : UINT8_C(0xc7)
            ) ||
            !study_x86_emit_modrm(encoder, 0U, destination)) {
            return false;
        }
        if (destination->width == STUDY_X86_WIDTH_8) {
            return study_x86_emit_u8(
                encoder,
                (uint8_t)source->as.immediate.value
            );
        }
        if (destination->width == STUDY_X86_WIDTH_64) {
            int64_t signed_value = (int64_t)source->as.immediate.value;

            if ((!source->as.immediate.is_signed &&
                 source->as.immediate.value > (uint64_t)INT32_MAX) ||
                (source->as.immediate.is_signed &&
                 (signed_value < INT32_MIN || signed_value > INT32_MAX))) {
                return false;
            }
        }
        return study_x86_emit_u32(
            encoder,
            (uint32_t)source->as.immediate.value
        );
    }

    if (source->kind == STUDY_X86_OPERAND_REGISTER) {
        return study_x86_operand_register_code(source, &source_code) &&
               study_x86_emit_rm_instruction(
                   encoder,
                   destination->width,
                   destination->width == STUDY_X86_WIDTH_8
                       ? UINT8_C(0x88)
                       : UINT8_C(0x89),
                   source_code,
                   destination
               );
    }
    return study_x86_operand_register_code(destination, &destination_code) &&
           study_x86_emit_rm_instruction(
               encoder,
               destination->width,
               destination->width == STUDY_X86_WIDTH_8
                   ? UINT8_C(0x8a)
                   : UINT8_C(0x8b),
               destination_code,
               source
           );
}

static bool study_x86_emit_binary_arithmetic(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction,
    uint8_t rm_opcode_8,
    uint8_t rm_opcode,
    uint8_t reg_opcode_8,
    uint8_t reg_opcode,
    uint8_t immediate_group
) {
    const StudyX86Operand *source = &instruction->first;
    const StudyX86Operand *destination = &instruction->second;
    uint8_t register_code;

    if (source->kind == STUDY_X86_OPERAND_IMMEDIATE) {
        return study_x86_emit_group_immediate(
            encoder,
            source,
            destination,
            immediate_group
        );
    }
    if (source->kind == STUDY_X86_OPERAND_REGISTER) {
        return study_x86_operand_register_code(source, &register_code) &&
               study_x86_emit_rm_instruction(
                   encoder,
                   destination->width,
                   destination->width == STUDY_X86_WIDTH_8
                       ? rm_opcode_8
                       : rm_opcode,
                   register_code,
                   destination
               );
    }
    return study_x86_operand_register_code(destination, &register_code) &&
           study_x86_emit_rm_instruction(
               encoder,
               destination->width,
               destination->width == STUDY_X86_WIDTH_8
                   ? reg_opcode_8
                   : reg_opcode,
               register_code,
               source
           );
}

static bool study_x86_emit_test(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    const StudyX86Operand *rm_operand = &instruction->second;
    const StudyX86Operand *register_operand = &instruction->first;
    uint8_t register_code;

    if (register_operand->kind != STUDY_X86_OPERAND_REGISTER) {
        rm_operand = &instruction->first;
        register_operand = &instruction->second;
    }
    return study_x86_operand_register_code(register_operand, &register_code) &&
           study_x86_emit_rm_instruction(
               encoder,
               register_operand->width,
               register_operand->width == STUDY_X86_WIDTH_8
                   ? UINT8_C(0x84)
                   : UINT8_C(0x85),
               register_code,
               rm_operand
           );
}

static bool study_x86_emit_movzx(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    uint8_t destination_code;
    uint8_t source_code;

    return study_x86_operand_register_code(
               &instruction->second,
               &destination_code
           ) &&
           study_x86_operand_rm_code(&instruction->first, &source_code) &&
           study_x86_emit_rex(
               encoder,
               STUDY_X86_WIDTH_32,
               destination_code,
               source_code,
               true
           ) &&
           study_x86_emit_u8(encoder, UINT8_C(0x0f)) &&
           study_x86_emit_u8(encoder, UINT8_C(0xb6)) &&
           study_x86_emit_modrm(
               encoder,
               destination_code,
               &instruction->first
           );
}

static bool study_x86_emit_lea(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    uint8_t destination_code;

    return study_x86_operand_register_code(
               &instruction->second,
               &destination_code
           ) &&
           study_x86_emit_rm_instruction(
               encoder,
               STUDY_X86_WIDTH_64,
               UINT8_C(0x8d),
               destination_code,
               &instruction->first
           );
}

static bool study_x86_emit_multiply(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    uint8_t destination_code;
    uint8_t source_code;

    return study_x86_operand_register_code(
               &instruction->second,
               &destination_code
           ) &&
           study_x86_operand_rm_code(&instruction->first, &source_code) &&
           study_x86_emit_rex(
               encoder,
               instruction->second.width,
               destination_code,
               source_code,
               false
           ) &&
           study_x86_emit_u8(encoder, UINT8_C(0x0f)) &&
           study_x86_emit_u8(encoder, UINT8_C(0xaf)) &&
           study_x86_emit_modrm(
               encoder,
               destination_code,
               &instruction->first
           );
}

static bool study_x86_emit_group_unary(
    StudyX86Encoder *encoder,
    const StudyX86Operand *operand,
    uint8_t group,
    bool byte_opcode
) {
    uint8_t rm_code;

    return study_x86_operand_rm_code(operand, &rm_code) &&
           study_x86_emit_rex(
               encoder,
               operand->width,
               group,
               rm_code,
               operand->width == STUDY_X86_WIDTH_8
           ) &&
           study_x86_emit_u8(
               encoder,
               byte_opcode && operand->width == STUDY_X86_WIDTH_8
                   ? UINT8_C(0xf6)
                   : byte_opcode
                         ? UINT8_C(0xf7)
                         : operand->width == STUDY_X86_WIDTH_8
                               ? UINT8_C(0xfe)
                               : UINT8_C(0xff)
           ) &&
           study_x86_emit_modrm(encoder, group, operand);
}

static bool study_x86_condition_code(
    StudyX86Condition condition,
    uint8_t *code
) {
    switch (condition) {
        case STUDY_X86_CONDITION_EQUAL:
            *code = UINT8_C(0x4);
            return true;
        case STUDY_X86_CONDITION_NOT_EQUAL:
            *code = UINT8_C(0x5);
            return true;
        case STUDY_X86_CONDITION_LESS:
            *code = UINT8_C(0xc);
            return true;
        case STUDY_X86_CONDITION_LESS_EQUAL:
            *code = UINT8_C(0xe);
            return true;
        case STUDY_X86_CONDITION_GREATER:
            *code = UINT8_C(0xf);
            return true;
        case STUDY_X86_CONDITION_GREATER_EQUAL:
            *code = UINT8_C(0xd);
            return true;
        case STUDY_X86_CONDITION_NOT_SIGN:
            *code = UINT8_C(0x9);
            return true;
    }

    return false;
}

static bool study_x86_emit_set_condition(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    uint8_t condition;
    uint8_t rm_code;

    return study_x86_condition_code(instruction->condition, &condition) &&
           study_x86_operand_rm_code(&instruction->first, &rm_code) &&
           study_x86_emit_rex(
               encoder,
               STUDY_X86_WIDTH_8,
               0U,
               rm_code,
               true
           ) &&
           study_x86_emit_u8(encoder, UINT8_C(0x0f)) &&
           study_x86_emit_u8(
               encoder,
               (uint8_t)(UINT8_C(0x90) | condition)
           ) &&
           study_x86_emit_modrm(encoder, 0U, &instruction->first);
}

static bool study_x86_reserve_fixup(StudyX86Encoder *encoder) {
    size_t capacity;
    StudyX86Fixup *grown;

    if (encoder->fixup_count < encoder->fixup_capacity) {
        return true;
    }
    capacity = encoder->fixup_capacity == 0U
                   ? 32U
                   : encoder->fixup_capacity * 2U;
    if (capacity < encoder->fixup_capacity ||
        capacity > SIZE_MAX / sizeof(*encoder->fixups)) {
        study_x86_encode_error(encoder, NULL, "too many relative fixups");
        return false;
    }
    grown = realloc(encoder->fixups, capacity * sizeof(*encoder->fixups));
    if (grown == NULL) {
        study_x86_encode_error(
            encoder,
            NULL,
            "not enough memory for relative fixups"
        );
        return false;
    }
    encoder->fixups = grown;
    encoder->fixup_capacity = capacity;
    return true;
}

static bool study_x86_emit_rel32(
    StudyX86Encoder *encoder,
    StudyX86FixupKind kind,
    size_t target,
    StudySourceRange range
) {
    StudyX86Fixup *fixup;

    if (!study_x86_reserve_fixup(encoder)) {
        return false;
    }
    fixup = &encoder->fixups[encoder->fixup_count++];
    fixup->kind = kind;
    fixup->target = target;
    fixup->displacement_offset = encoder->image.length;
    fixup->next_offset = encoder->image.length + 4U;
    fixup->range = range;
    return study_x86_emit_u32(encoder, 0U);
}

static bool study_x86_emit_call(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    return study_x86_emit_u8(encoder, UINT8_C(0xe8)) &&
           study_x86_emit_rel32(
               encoder,
               STUDY_X86_FIXUP_FUNCTION,
               instruction->target_function,
               instruction->range
           );
}

static bool study_x86_emit_jump(
    StudyX86Encoder *encoder,
    StudyX86BlockId target,
    StudySourceRange range
) {
    return study_x86_emit_u8(encoder, UINT8_C(0xe9)) &&
           study_x86_emit_rel32(
               encoder,
               STUDY_X86_FIXUP_BLOCK,
               target,
               range
           );
}

static bool study_x86_emit_branch(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    uint8_t condition;

    return study_x86_condition_code(instruction->condition, &condition) &&
           study_x86_emit_u8(encoder, UINT8_C(0x0f)) &&
           study_x86_emit_u8(
               encoder,
               (uint8_t)(UINT8_C(0x80) | condition)
           ) &&
           study_x86_emit_rel32(
               encoder,
               STUDY_X86_FIXUP_BLOCK,
               instruction->then_block,
               instruction->range
           ) &&
           study_x86_emit_jump(
               encoder,
               instruction->else_block,
               instruction->range
           );
}

static bool study_x86_emit_instruction(
    StudyX86Encoder *encoder,
    const StudyX86Instruction *instruction
) {
    uint8_t register_code;

    switch (instruction->opcode) {
        case STUDY_X86_MOV:
            return study_x86_emit_mov(encoder, instruction);
        case STUDY_X86_MOVZX:
            return study_x86_emit_movzx(encoder, instruction);
        case STUDY_X86_LEA:
            return study_x86_emit_lea(encoder, instruction);
        case STUDY_X86_ADD:
            return study_x86_emit_binary_arithmetic(
                encoder,
                instruction,
                UINT8_C(0x00),
                UINT8_C(0x01),
                UINT8_C(0x02),
                UINT8_C(0x03),
                0U
            );
        case STUDY_X86_SUBTRACT:
            return study_x86_emit_binary_arithmetic(
                encoder,
                instruction,
                UINT8_C(0x28),
                UINT8_C(0x29),
                UINT8_C(0x2a),
                UINT8_C(0x2b),
                5U
            );
        case STUDY_X86_MULTIPLY:
            return study_x86_emit_multiply(encoder, instruction);
        case STUDY_X86_NEGATE:
            return study_x86_emit_group_unary(
                encoder,
                &instruction->first,
                3U,
                true
            );
        case STUDY_X86_XOR:
            return study_x86_emit_binary_arithmetic(
                encoder,
                instruction,
                UINT8_C(0x30),
                UINT8_C(0x31),
                UINT8_C(0x32),
                UINT8_C(0x33),
                6U
            );
        case STUDY_X86_TEST:
            return study_x86_emit_test(encoder, instruction);
        case STUDY_X86_COMPARE:
            return study_x86_emit_binary_arithmetic(
                encoder,
                instruction,
                UINT8_C(0x38),
                UINT8_C(0x39),
                UINT8_C(0x3a),
                UINT8_C(0x3b),
                7U
            );
        case STUDY_X86_SET_CONDITION:
            return study_x86_emit_set_condition(encoder, instruction);
        case STUDY_X86_DIVIDE_UNSIGNED:
            return study_x86_emit_group_unary(
                encoder,
                &instruction->first,
                6U,
                true
            );
        case STUDY_X86_INCREMENT:
            return study_x86_emit_group_unary(
                encoder,
                &instruction->first,
                0U,
                false
            );
        case STUDY_X86_DECREMENT:
            return study_x86_emit_group_unary(
                encoder,
                &instruction->first,
                1U,
                false
            );
        case STUDY_X86_PUSH:
            return study_x86_operand_register_code(
                       &instruction->first,
                       &register_code
                   ) &&
                   study_x86_emit_rex(
                       encoder,
                       STUDY_X86_WIDTH_32,
                       0U,
                       register_code,
                       false
                   ) &&
                   study_x86_emit_u8(
                       encoder,
                       (uint8_t)(UINT8_C(0x50) |
                                 (register_code & UINT8_C(0x07)))
                   );
        case STUDY_X86_CALL:
            return study_x86_emit_call(encoder, instruction);
        case STUDY_X86_JUMP:
            return study_x86_emit_jump(
                encoder,
                instruction->then_block,
                instruction->range
            );
        case STUDY_X86_BRANCH_CONDITION:
            return study_x86_emit_branch(encoder, instruction);
        case STUDY_X86_LEAVE:
            return study_x86_emit_u8(encoder, UINT8_C(0xc9));
        case STUDY_X86_RETURN:
            return study_x86_emit_u8(encoder, UINT8_C(0xc3));
        case STUDY_X86_SYSCALL:
            return study_x86_emit_u8(encoder, UINT8_C(0x0f)) &&
                   study_x86_emit_u8(encoder, UINT8_C(0x05));
        case STUDY_X86_UNDEFINED:
            return study_x86_emit_u8(encoder, UINT8_C(0x0f)) &&
                   study_x86_emit_u8(encoder, UINT8_C(0x0b));
    }

    return false;
}

static bool study_x86_allocate_image_maps(StudyX86Encoder *encoder) {
    size_t index;

    encoder->image.function_offsets = malloc(
        encoder->program->function_count *
        sizeof(*encoder->image.function_offsets)
    );
    encoder->image.block_offsets = malloc(
        encoder->program->block_count * sizeof(*encoder->image.block_offsets)
    );
    encoder->image.instruction_offsets = malloc(
        encoder->program->instruction_count *
        sizeof(*encoder->image.instruction_offsets)
    );
    encoder->image.instruction_sizes = malloc(
        encoder->program->instruction_count *
        sizeof(*encoder->image.instruction_sizes)
    );
    if (encoder->image.function_offsets == NULL ||
        encoder->image.block_offsets == NULL ||
        encoder->image.instruction_offsets == NULL ||
        encoder->image.instruction_sizes == NULL) {
        study_x86_encode_error(
            encoder,
            NULL,
            "not enough memory for image offsets"
        );
        return false;
    }
    encoder->image.function_count = encoder->program->function_count;
    encoder->image.block_count = encoder->program->block_count;
    encoder->image.instruction_count = encoder->program->instruction_count;
    for (index = 0U; index < encoder->image.function_count; index++) {
        encoder->image.function_offsets[index] = SIZE_MAX;
    }
    for (index = 0U; index < encoder->image.block_count; index++) {
        encoder->image.block_offsets[index] = SIZE_MAX;
    }
    for (index = 0U; index < encoder->image.instruction_count; index++) {
        encoder->image.instruction_offsets[index] = SIZE_MAX;
        encoder->image.instruction_sizes[index] = 0U;
    }
    return true;
}

static bool study_x86_encode_functions(StudyX86Encoder *encoder) {
    StudyX86FunctionId function_id;

    for (function_id = 0U;
         function_id < encoder->program->function_count;
         function_id++) {
        const StudyX86Function *function =
            &encoder->program->functions[function_id];
        StudyX86BlockId block_id = function->first_block;

        if (function_id > 0U) {
            while (encoder->image.length % 16U != 0U) {
                if (!study_x86_emit_u8(encoder, UINT8_C(0x90))) {
                    return false;
                }
            }
        }
        encoder->image.function_offsets[function_id] = encoder->image.length;
        while (block_id != STUDY_X86_BLOCK_NONE) {
            StudyX86InstructionId instruction_id =
                encoder->program->blocks[block_id].first_instruction;

            encoder->image.block_offsets[block_id] = encoder->image.length;
            while (instruction_id != STUDY_X86_INSTRUCTION_NONE) {
                const StudyX86Instruction *instruction =
                    &encoder->program->instructions[instruction_id];
                size_t offset = encoder->image.length;

                encoder->image.instruction_offsets[instruction_id] = offset;
                if (!study_x86_emit_instruction(encoder, instruction)) {
                    if (encoder->diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
                        study_x86_encode_error(
                            encoder,
                            &instruction->range,
                            "an instruction has no supported encoding"
                        );
                    }
                    return false;
                }
                encoder->image.instruction_sizes[instruction_id] =
                    encoder->image.length - offset;
                instruction_id = instruction->next_instruction;
            }
            block_id = encoder->program->blocks[block_id].next_block;
        }
    }
    encoder->image.entry_offset =
        encoder->image.function_offsets[encoder->program->start_function];
    return true;
}

static bool study_x86_resolve_fixups(StudyX86Encoder *encoder) {
    size_t index;

    for (index = 0U; index < encoder->fixup_count; index++) {
        const StudyX86Fixup *fixup = &encoder->fixups[index];
        size_t target_offset;
        int64_t displacement;

        if (fixup->kind == STUDY_X86_FIXUP_FUNCTION) {
            if (fixup->target >= encoder->image.function_count) {
                study_x86_encode_error(
                    encoder,
                    &fixup->range,
                    "a call fixup names an unknown function"
                );
                return false;
            }
            target_offset = encoder->image.function_offsets[fixup->target];
        } else {
            if (fixup->target >= encoder->image.block_count) {
                study_x86_encode_error(
                    encoder,
                    &fixup->range,
                    "a branch fixup names an unknown block"
                );
                return false;
            }
            target_offset = encoder->image.block_offsets[fixup->target];
        }
        if (target_offset == SIZE_MAX ||
            target_offset > (size_t)INT64_MAX ||
            fixup->next_offset > (size_t)INT64_MAX) {
            study_x86_encode_error(
                encoder,
                &fixup->range,
                "a relative target has no code offset"
            );
            return false;
        }
        displacement = (int64_t)target_offset -
                       (int64_t)fixup->next_offset;
        if (displacement < INT32_MIN || displacement > INT32_MAX ||
            fixup->displacement_offset > encoder->image.length - 4U) {
            study_x86_encode_error(
                encoder,
                &fixup->range,
                "a relative target is outside the rel32 range"
            );
            return false;
        }
        study_x86_patch_i32(
            &encoder->image,
            fixup->displacement_offset,
            (int32_t)displacement
        );
    }
    return true;
}

bool study_x86_64_encode(
    const StudyX86Program *program,
    const StudySource *source,
    StudyX86Image *output,
    StudyDiagnostic *diagnostic
) {
    StudyX86Encoder encoder;
    bool success = false;

    study_diagnostic_clear(diagnostic);
    if (!study_x86_image_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "x86-64 encoding requires an initialised empty output image"
        );
        return false;
    }
    if (source == NULL || !study_x86_64_verify(program, source, diagnostic)) {
        return false;
    }

    encoder.program = program;
    encoder.source = source;
    study_x86_64_image_init(&encoder.image);
    encoder.fixups = NULL;
    encoder.fixup_count = 0U;
    encoder.fixup_capacity = 0U;
    encoder.diagnostic = diagnostic;

    if (!study_x86_allocate_image_maps(&encoder) ||
        !study_x86_encode_functions(&encoder) ||
        !study_x86_resolve_fixups(&encoder)) {
        goto cleanup;
    }

    *output = encoder.image;
    study_x86_64_image_init(&encoder.image);
    success = true;

cleanup:
    free(encoder.fixups);
    study_x86_64_image_free(&encoder.image);
    return success;
}

static bool study_x86_image_matches_program(
    const StudyX86Program *program,
    const StudyX86Image *image
) {
    size_t index;

    if (program == NULL || image == NULL || image->bytes == NULL ||
        image->length == 0U || image->function_offsets == NULL ||
        image->block_offsets == NULL || image->instruction_offsets == NULL ||
        image->instruction_sizes == NULL ||
        image->function_count != program->function_count ||
        image->block_count != program->block_count ||
        image->instruction_count != program->instruction_count ||
        image->entry_offset >= image->length ||
        image->entry_offset !=
            image->function_offsets[program->start_function]) {
        return false;
    }
    for (index = 0U; index < image->function_count; index++) {
        if (image->function_offsets[index] >= image->length) {
            return false;
        }
    }
    for (index = 0U; index < image->block_count; index++) {
        if (image->block_offsets[index] >= image->length) {
            return false;
        }
    }
    for (index = 0U; index < image->instruction_count; index++) {
        if (image->instruction_sizes[index] == 0U ||
            image->instruction_offsets[index] >= image->length ||
            image->instruction_sizes[index] >
                image->length - image->instruction_offsets[index]) {
            return false;
        }
    }
    return true;
}

bool study_x86_64_image_verify(
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    study_diagnostic_clear(diagnostic);
    if (source == NULL ||
        !study_x86_64_verify(program, source, diagnostic) ||
        !study_x86_image_matches_program(program, image)) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_diagnostic_set(
                diagnostic,
                STUDY_DIAGNOSTIC_INTERNAL,
                source != NULL ? source->path : NULL,
                "x86-64 image does not match its target program"
            );
        }
        return false;
    }
    return true;
}

bool study_x86_64_image_dump(
    FILE *stream,
    const StudyX86Program *program,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    StudyX86FunctionId function_id;

    study_diagnostic_clear(diagnostic);
    if (stream == NULL ||
        !study_x86_64_image_verify(program, image, source, diagnostic)) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_diagnostic_set(
                diagnostic,
                STUDY_DIAGNOSTIC_INTERNAL,
                source != NULL ? source->path : NULL,
                "encoding dump requires a matching target program and image"
            );
        }
        return false;
    }

    if (fprintf(
            stream,
            "image x86-64-linux-system-v [bytes=%zu, entry=0x%08zx] {\n",
            image->length,
            image->entry_offset
        ) < 0) {
        goto io_error;
    }
    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        const StudyX86Function *function = &program->functions[function_id];
        StudyX86BlockId block_id = function->first_block;

        if (fprintf(stream, "  function f%zu ", function_id) < 0) {
            goto io_error;
        }
        if ((function->kind == STUDY_X86_FUNCTION_START &&
             fputs("_start", stream) == EOF) ||
            (function->kind == STUDY_X86_FUNCTION_PRINT_I32 &&
             fputs("study_rt_print_i32", stream) == EOF) ||
            (function->kind == STUDY_X86_FUNCTION_STUDY &&
             fprintf(stream, "study_fn_%zu", function->ir_function) < 0) ||
            fprintf(
                stream,
                " @0x%08zx\n",
                image->function_offsets[function_id]
            ) < 0) {
            goto io_error;
        }

        while (block_id != STUDY_X86_BLOCK_NONE) {
            StudyX86InstructionId instruction_id =
                program->blocks[block_id].first_instruction;

            if (fprintf(
                    stream,
                    "    b%zu @0x%08zx\n",
                    block_id,
                    image->block_offsets[block_id]
                ) < 0) {
                goto io_error;
            }
            while (instruction_id != STUDY_X86_INSTRUCTION_NONE) {
                const StudyX86Instruction *instruction =
                    &program->instructions[instruction_id];
                size_t offset = image->instruction_offsets[instruction_id];
                size_t size = image->instruction_sizes[instruction_id];
                size_t index;

                if (fprintf(stream, "      %08zx: ", offset) < 0) {
                    goto io_error;
                }
                for (index = 0U; index < 12U; index++) {
                    if (index < size) {
                        if (fprintf(
                                stream,
                                "%02x ",
                                (unsigned int)image->bytes[offset + index]
                            ) < 0) {
                            goto io_error;
                        }
                    } else if (fputs("   ", stream) == EOF) {
                        goto io_error;
                    }
                }
                if (fprintf(
                        stream,
                        "i%zu %s",
                        instruction_id,
                        study_x86_64_opcode_name(instruction->opcode)
                    ) < 0) {
                    goto io_error;
                }
                if ((instruction->opcode == STUDY_X86_CALL &&
                     fprintf(
                         stream,
                         " f%zu",
                         instruction->target_function
                     ) < 0) ||
                    (instruction->opcode == STUDY_X86_JUMP &&
                     fprintf(stream, " b%zu", instruction->then_block) < 0) ||
                    (instruction->opcode == STUDY_X86_BRANCH_CONDITION &&
                     fprintf(
                         stream,
                         " b%zu, b%zu",
                         instruction->then_block,
                         instruction->else_block
                     ) < 0) ||
                    fputc('\n', stream) == EOF) {
                    goto io_error;
                }
                instruction_id = instruction->next_instruction;
            }
            block_id = program->blocks[block_id].next_block;
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
        "could not write x86-64 encoding dump"
    );
    return false;
}

bool study_x86_64_image_write(
    FILE *stream,
    const StudyX86Image *image,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    study_diagnostic_clear(diagnostic);
    if (stream == NULL || source == NULL || image == NULL ||
        image->bytes == NULL || image->length == 0U ||
        image->entry_offset >= image->length) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "binary output requires a complete x86-64 image"
        );
        return false;
    }
    if (fwrite(image->bytes, 1U, image->length, stream) != image->length) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            source->path,
            "could not write the x86-64 code image"
        );
        return false;
    }
    return true;
}
