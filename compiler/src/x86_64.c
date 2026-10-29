#include "study/x86_64.h"

#include "study/x86_64_verify.h"

#include <inttypes.h>
#include <stdlib.h>

void study_x86_64_program_init(StudyX86Program *program) {
    if (program == NULL) {
        return;
    }

    program->functions = NULL;
    program->function_count = 0U;
    program->function_capacity = 0U;
    program->blocks = NULL;
    program->block_count = 0U;
    program->block_capacity = 0U;
    program->instructions = NULL;
    program->instruction_count = 0U;
    program->instruction_capacity = 0U;
    program->storage = NULL;
    program->storage_count = 0U;
    program->storage_capacity = 0U;
    program->start_function = STUDY_X86_FUNCTION_NONE;
    program->print_function = STUDY_X86_FUNCTION_NONE;
}

void study_x86_64_program_free(StudyX86Program *program) {
    if (program == NULL) {
        return;
    }

    free(program->functions);
    free(program->blocks);
    free(program->instructions);
    free(program->storage);
    study_x86_64_program_init(program);
}

const char *study_x86_64_opcode_name(StudyX86Opcode opcode) {
    switch (opcode) {
        case STUDY_X86_MOV:
            return "mov";
        case STUDY_X86_MOVZX:
            return "movzx";
        case STUDY_X86_LEA:
            return "lea";
        case STUDY_X86_ADD:
            return "add";
        case STUDY_X86_SUBTRACT:
            return "sub";
        case STUDY_X86_MULTIPLY:
            return "imul";
        case STUDY_X86_NEGATE:
            return "neg";
        case STUDY_X86_XOR:
            return "xor";
        case STUDY_X86_TEST:
            return "test";
        case STUDY_X86_COMPARE:
            return "cmp";
        case STUDY_X86_SET_CONDITION:
            return "set";
        case STUDY_X86_DIVIDE_UNSIGNED:
            return "div";
        case STUDY_X86_INCREMENT:
            return "inc";
        case STUDY_X86_DECREMENT:
            return "dec";
        case STUDY_X86_PUSH:
            return "push";
        case STUDY_X86_CALL:
            return "call";
        case STUDY_X86_JUMP:
            return "jump";
        case STUDY_X86_BRANCH_CONDITION:
            return "branch";
        case STUDY_X86_LEAVE:
            return "leave";
        case STUDY_X86_RETURN:
            return "return";
        case STUDY_X86_SYSCALL:
            return "syscall";
        case STUDY_X86_UNDEFINED:
            return "undefined";
    }

    return "unknown";
}

static const char *study_x86_64_width_name(StudyX86Width width) {
    switch (width) {
        case STUDY_X86_WIDTH_8:
            return "i8";
        case STUDY_X86_WIDTH_32:
            return "i32";
        case STUDY_X86_WIDTH_64:
            return "i64";
    }

    return "invalid";
}

static const char *study_x86_64_condition_name(StudyX86Condition condition) {
    switch (condition) {
        case STUDY_X86_CONDITION_EQUAL:
            return "equal";
        case STUDY_X86_CONDITION_NOT_EQUAL:
            return "not-equal";
        case STUDY_X86_CONDITION_LESS:
            return "less";
        case STUDY_X86_CONDITION_LESS_EQUAL:
            return "less-equal";
        case STUDY_X86_CONDITION_GREATER:
            return "greater";
        case STUDY_X86_CONDITION_GREATER_EQUAL:
            return "greater-equal";
        case STUDY_X86_CONDITION_NOT_SIGN:
            return "not-sign";
    }

    return "invalid";
}

static const char *study_x86_64_register_name(
    StudyX86Register register_id,
    StudyX86Width width
) {
    static const char *const names_8[] = {
        "al", "bpl", "spl", "dil", "sil", "dl", "cl", "r8b", "r9b"
    };
    static const char *const names_32[] = {
        "eax", "ebp", "esp", "edi", "esi", "edx", "ecx", "r8d", "r9d"
    };
    static const char *const names_64[] = {
        "rax", "rbp", "rsp", "rdi", "rsi", "rdx", "rcx", "r8", "r9"
    };

    if ((size_t)register_id >= sizeof(names_8) / sizeof(names_8[0])) {
        return "invalid-register";
    }
    if (width == STUDY_X86_WIDTH_8) {
        return names_8[register_id];
    }
    if (width == STUDY_X86_WIDTH_32) {
        return names_32[register_id];
    }
    if (width == STUDY_X86_WIDTH_64) {
        return names_64[register_id];
    }
    return "invalid-register-width";
}

static bool study_x86_64_dump_operand(
    FILE *stream,
    const StudyX86Operand *operand
) {
    switch (operand->kind) {
        case STUDY_X86_OPERAND_REGISTER:
            return fputs(
                       study_x86_64_register_name(
                           operand->as.register_id,
                           operand->width
                       ),
                       stream
                   ) != EOF;
        case STUDY_X86_OPERAND_IMMEDIATE:
            if (operand->as.immediate.is_signed) {
                return fprintf(
                           stream,
                           "#%" PRId64,
                           (int64_t)operand->as.immediate.value
                       ) >= 0;
            }
            if (operand->width == STUDY_X86_WIDTH_32) {
                return fprintf(
                           stream,
                           "#0x%08" PRIx32,
                           (uint32_t)operand->as.immediate.value
                       ) >= 0;
            }
            return fprintf(
                       stream,
                       "#0x%" PRIx64,
                       operand->as.immediate.value
                   ) >= 0;
        case STUDY_X86_OPERAND_MEMORY:
            if (operand->as.memory.displacement == 0) {
                return fprintf(
                           stream,
                           "[%s]",
                           study_x86_64_register_name(
                               operand->as.memory.base,
                               STUDY_X86_WIDTH_64
                           )
                       ) >= 0;
            }
            return fprintf(
                       stream,
                       "[%s%+" PRId32 "]",
                       study_x86_64_register_name(
                           operand->as.memory.base,
                           STUDY_X86_WIDTH_64
                       ),
                       operand->as.memory.displacement
                   ) >= 0;
        case STUDY_X86_OPERAND_NONE:
            break;
    }

    return false;
}

static bool study_x86_64_dump_function_name(
    FILE *stream,
    const StudyX86Function *function
) {
    switch (function->kind) {
        case STUDY_X86_FUNCTION_START:
            return fputs("_start", stream) != EOF;
        case STUDY_X86_FUNCTION_PRINT_I32:
            return fputs("study_rt_print_i32", stream) != EOF;
        case STUDY_X86_FUNCTION_STUDY:
            return fprintf(stream, "study_fn_%zu", function->ir_function) >= 0;
    }

    return false;
}

static bool study_x86_64_dump_instruction(
    FILE *stream,
    const StudyX86Instruction *instruction
) {
    if (fputs("      ", stream) == EOF) {
        return false;
    }

    switch (instruction->opcode) {
        case STUDY_X86_MOV:
        case STUDY_X86_LEA:
        case STUDY_X86_ADD:
        case STUDY_X86_SUBTRACT:
        case STUDY_X86_MULTIPLY:
        case STUDY_X86_XOR:
        case STUDY_X86_TEST:
        case STUDY_X86_COMPARE:
            return fprintf(
                       stream,
                       "%s.%s ",
                       study_x86_64_opcode_name(instruction->opcode),
                       study_x86_64_width_name(instruction->second.width)
                   ) >= 0 &&
                   study_x86_64_dump_operand(stream, &instruction->first) &&
                   fputs(", ", stream) != EOF &&
                   study_x86_64_dump_operand(stream, &instruction->second) &&
                   fputc('\n', stream) != EOF;
        case STUDY_X86_MOVZX:
            return fprintf(
                       stream,
                       "movzx.%s.%s ",
                       study_x86_64_width_name(instruction->first.width),
                       study_x86_64_width_name(instruction->second.width)
                   ) >= 0 &&
                   study_x86_64_dump_operand(stream, &instruction->first) &&
                   fputs(", ", stream) != EOF &&
                   study_x86_64_dump_operand(stream, &instruction->second) &&
                   fputc('\n', stream) != EOF;
        case STUDY_X86_NEGATE:
        case STUDY_X86_DIVIDE_UNSIGNED:
        case STUDY_X86_INCREMENT:
        case STUDY_X86_DECREMENT:
        case STUDY_X86_PUSH:
            return fprintf(
                       stream,
                       "%s.%s ",
                       study_x86_64_opcode_name(instruction->opcode),
                       study_x86_64_width_name(instruction->first.width)
                   ) >= 0 &&
                   study_x86_64_dump_operand(stream, &instruction->first) &&
                   fputc('\n', stream) != EOF;
        case STUDY_X86_SET_CONDITION:
            return fprintf(
                       stream,
                       "set.%s ",
                       study_x86_64_condition_name(instruction->condition)
                   ) >= 0 &&
                   study_x86_64_dump_operand(stream, &instruction->first) &&
                   fputc('\n', stream) != EOF;
        case STUDY_X86_CALL:
            return fprintf(stream, "call f%zu\n", instruction->target_function) >=
                   0;
        case STUDY_X86_JUMP:
            return fprintf(stream, "jump b%zu\n", instruction->then_block) >= 0;
        case STUDY_X86_BRANCH_CONDITION:
            return fprintf(
                       stream,
                       "branch.%s b%zu, b%zu\n",
                       study_x86_64_condition_name(instruction->condition),
                       instruction->then_block,
                       instruction->else_block
                   ) >= 0;
        case STUDY_X86_LEAVE:
        case STUDY_X86_RETURN:
        case STUDY_X86_SYSCALL:
        case STUDY_X86_UNDEFINED:
            return fprintf(
                       stream,
                       "%s\n",
                       study_x86_64_opcode_name(instruction->opcode)
                   ) >= 0;
    }

    return false;
}

static bool study_x86_64_dump_function(
    FILE *stream,
    const StudyX86Program *program,
    StudyX86FunctionId function_id
) {
    const StudyX86Function *function = &program->functions[function_id];
    StudyX86BlockId block_id = function->first_block;
    size_t index;

    if (fprintf(stream, "  function f%zu ", function_id) < 0 ||
        !study_x86_64_dump_function_name(stream, function) ||
        fprintf(
            stream,
            " [frame=%zu, entry=b%zu] {\n",
            function->frame_size,
            function->entry_block
        ) < 0) {
        return false;
    }

    if (function->storage_count > 0U) {
        if (fputs("    storage:\n", stream) == EOF) {
            return false;
        }
        for (index = function->first_storage;
             index < function->first_storage + function->storage_count;
             index++) {
            const StudyX86Storage *storage = &program->storage[index];
            const char prefix = storage->kind == STUDY_X86_STORAGE_SLOT
                                    ? 's'
                                    : 'v';

            if (fprintf(
                    stream,
                    "      ir.%c%zu -> i32 [rbp%+" PRId32 "]\n",
                    prefix,
                    storage->ir_id,
                    storage->displacement
                ) < 0) {
                return false;
            }
        }
    }

    while (block_id != STUDY_X86_BLOCK_NONE) {
        StudyX86InstructionId instruction_id =
            program->blocks[block_id].first_instruction;

        if (fprintf(stream, "    b%zu:\n", block_id) < 0) {
            return false;
        }
        while (instruction_id != STUDY_X86_INSTRUCTION_NONE) {
            const StudyX86Instruction *instruction =
                &program->instructions[instruction_id];

            if (!study_x86_64_dump_instruction(stream, instruction)) {
                return false;
            }
            instruction_id = instruction->next_instruction;
        }
        block_id = program->blocks[block_id].next_block;
    }

    return fputs("  }\n", stream) != EOF;
}

bool study_x86_64_program_dump(
    FILE *stream,
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    size_t function_id;

    study_diagnostic_clear(diagnostic);
    if (stream == NULL || source == NULL ||
        !study_x86_64_verify(program, source, diagnostic)) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_diagnostic_set(
                diagnostic,
                STUDY_DIAGNOSTIC_INTERNAL,
                source != NULL ? source->path : NULL,
                "x86-64 target dump requires an output stream and source"
            );
        }
        return false;
    }

    if (fputs("target x86-64-linux-system-v {\n", stream) == EOF) {
        goto write_error;
    }
    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        if (!study_x86_64_dump_function(stream, program, function_id)) {
            goto write_error;
        }
    }
    if (fputs("}\n", stream) == EOF) {
        goto write_error;
    }
    return true;

write_error:
    study_diagnostic_set(
        diagnostic,
        STUDY_DIAGNOSTIC_IO,
        source->path,
        "could not write x86-64 target dump"
    );
    return false;
}
