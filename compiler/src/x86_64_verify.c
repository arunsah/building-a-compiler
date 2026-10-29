#include "study/x86_64_verify.h"

#include <stdlib.h>

static void study_x86_64_verify_error(
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        source != NULL ? source->path : NULL,
        "invalid x86-64 target program: %s",
        message
    );
    if (range != NULL && range->start.line > 0U) {
        study_diagnostic_set_location(
            diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static bool study_x86_64_width_is_valid(StudyX86Width width) {
    return width == STUDY_X86_WIDTH_8 || width == STUDY_X86_WIDTH_32 ||
           width == STUDY_X86_WIDTH_64;
}

static bool study_x86_64_register_is_valid(StudyX86Register register_id) {
    return register_id >= STUDY_X86_REGISTER_RAX &&
           register_id <= STUDY_X86_REGISTER_R9;
}

static bool study_x86_64_condition_is_valid(StudyX86Condition condition) {
    return condition >= STUDY_X86_CONDITION_EQUAL &&
           condition <= STUDY_X86_CONDITION_NOT_SIGN;
}

static bool study_x86_64_operand_is_valid(const StudyX86Operand *operand) {
    if (operand->kind == STUDY_X86_OPERAND_NONE) {
        return true;
    }
    if (!study_x86_64_width_is_valid(operand->width)) {
        return false;
    }

    switch (operand->kind) {
        case STUDY_X86_OPERAND_REGISTER:
            return study_x86_64_register_is_valid(operand->as.register_id);
        case STUDY_X86_OPERAND_IMMEDIATE:
            if (operand->width == STUDY_X86_WIDTH_8) {
                return operand->as.immediate.is_signed ||
                       operand->as.immediate.value <= UINT8_MAX;
            }
            if (operand->width == STUDY_X86_WIDTH_32) {
                return operand->as.immediate.is_signed ||
                       operand->as.immediate.value <= UINT32_MAX;
            }
            return true;
        case STUDY_X86_OPERAND_MEMORY:
            return study_x86_64_register_is_valid(operand->as.memory.base);
        case STUDY_X86_OPERAND_NONE:
            return true;
    }

    return false;
}

static bool study_x86_64_operand_is_data(
    const StudyX86Operand *operand
) {
    return operand->kind == STUDY_X86_OPERAND_REGISTER ||
           operand->kind == STUDY_X86_OPERAND_MEMORY;
}

static bool study_x86_64_binary_operands_are_valid(
    const StudyX86Instruction *instruction,
    bool immediate_allowed
) {
    bool first_is_allowed =
        study_x86_64_operand_is_data(&instruction->first) ||
        (immediate_allowed &&
         instruction->first.kind == STUDY_X86_OPERAND_IMMEDIATE);

    return first_is_allowed &&
           study_x86_64_operand_is_data(&instruction->second) &&
           instruction->first.width == instruction->second.width &&
           !(instruction->first.kind == STUDY_X86_OPERAND_MEMORY &&
             instruction->second.kind == STUDY_X86_OPERAND_MEMORY);
}

static bool study_x86_64_target_is_in_function(
    const StudyX86Program *program,
    StudyX86FunctionId function_id,
    StudyX86BlockId block_id
) {
    return block_id < program->block_count &&
           program->blocks[block_id].function == function_id;
}

static bool study_x86_64_opcode_is_terminator(StudyX86Opcode opcode) {
    return opcode == STUDY_X86_JUMP ||
           opcode == STUDY_X86_BRANCH_CONDITION ||
           opcode == STUDY_X86_RETURN || opcode == STUDY_X86_UNDEFINED;
}

static bool study_x86_64_instruction_shape_is_valid(
    const StudyX86Program *program,
    const StudyX86Instruction *instruction
) {
    if (!study_x86_64_operand_is_valid(&instruction->first) ||
        !study_x86_64_operand_is_valid(&instruction->second)) {
        return false;
    }

    switch (instruction->opcode) {
        case STUDY_X86_MOV:
            return study_x86_64_binary_operands_are_valid(instruction, true);
        case STUDY_X86_MOVZX:
            return study_x86_64_operand_is_data(&instruction->first) &&
                   instruction->first.width == STUDY_X86_WIDTH_8 &&
                   instruction->second.kind == STUDY_X86_OPERAND_REGISTER &&
                   instruction->second.width == STUDY_X86_WIDTH_32;
        case STUDY_X86_LEA:
            return instruction->first.kind == STUDY_X86_OPERAND_MEMORY &&
                   instruction->first.width == STUDY_X86_WIDTH_64 &&
                   instruction->second.kind == STUDY_X86_OPERAND_REGISTER &&
                   instruction->second.width == STUDY_X86_WIDTH_64;
        case STUDY_X86_ADD:
        case STUDY_X86_SUBTRACT:
        case STUDY_X86_XOR:
        case STUDY_X86_COMPARE:
            return study_x86_64_binary_operands_are_valid(instruction, true);
        case STUDY_X86_MULTIPLY:
            return study_x86_64_operand_is_data(&instruction->first) &&
                   instruction->second.kind == STUDY_X86_OPERAND_REGISTER &&
                   instruction->first.width == instruction->second.width &&
                   instruction->second.width != STUDY_X86_WIDTH_8;
        case STUDY_X86_TEST:
            return study_x86_64_binary_operands_are_valid(instruction, false);
        case STUDY_X86_NEGATE:
        case STUDY_X86_INCREMENT:
        case STUDY_X86_DECREMENT:
            return study_x86_64_operand_is_data(&instruction->first);
        case STUDY_X86_SET_CONDITION:
            return study_x86_64_condition_is_valid(instruction->condition) &&
                   instruction->first.kind == STUDY_X86_OPERAND_REGISTER &&
                   instruction->first.width == STUDY_X86_WIDTH_8;
        case STUDY_X86_DIVIDE_UNSIGNED:
            return study_x86_64_operand_is_data(&instruction->first) &&
                   instruction->first.width == STUDY_X86_WIDTH_32;
        case STUDY_X86_PUSH:
            return instruction->first.kind == STUDY_X86_OPERAND_REGISTER &&
                   instruction->first.width == STUDY_X86_WIDTH_64;
        case STUDY_X86_CALL:
            return instruction->target_function < program->function_count;
        case STUDY_X86_JUMP:
            return study_x86_64_target_is_in_function(
                program,
                instruction->function,
                instruction->then_block
            );
        case STUDY_X86_BRANCH_CONDITION:
            return study_x86_64_condition_is_valid(instruction->condition) &&
                   study_x86_64_target_is_in_function(
                       program,
                       instruction->function,
                       instruction->then_block
                   ) &&
                   study_x86_64_target_is_in_function(
                       program,
                       instruction->function,
                       instruction->else_block
                   );
        case STUDY_X86_LEAVE:
        case STUDY_X86_RETURN:
        case STUDY_X86_SYSCALL:
        case STUDY_X86_UNDEFINED:
            return instruction->first.kind == STUDY_X86_OPERAND_NONE &&
                   instruction->second.kind == STUDY_X86_OPERAND_NONE;
    }

    return false;
}

static bool study_x86_64_verify_storage(
    const StudyX86Program *program,
    const StudyX86Function *function,
    StudyX86FunctionId function_id,
    bool *storage_seen,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    size_t index;

    if (function->first_storage > program->storage_count ||
        function->storage_count >
            program->storage_count - function->first_storage) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            NULL,
            "a function has an invalid storage range"
        );
        return false;
    }

    for (index = function->first_storage;
         index < function->first_storage + function->storage_count;
         index++) {
        const StudyX86Storage *storage = &program->storage[index];
        int64_t positive_offset = -(int64_t)storage->displacement;

        if (storage_seen[index] || storage->function != function_id ||
            (storage->kind != STUDY_X86_STORAGE_SLOT &&
             storage->kind != STUDY_X86_STORAGE_VALUE) ||
            storage->width != STUDY_X86_WIDTH_32 ||
            storage->displacement >= 0 ||
            positive_offset > (int64_t)function->frame_size ||
            positive_offset % 8 != 0) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                NULL,
                "a stack location is outside its owning frame"
            );
            return false;
        }
        storage_seen[index] = true;
    }
    return true;
}

static bool study_x86_64_verify_block(
    const StudyX86Program *program,
    StudyX86FunctionId function_id,
    StudyX86BlockId block_id,
    bool *instruction_seen,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyX86Block *block = &program->blocks[block_id];
    StudyX86InstructionId instruction_id = block->first_instruction;
    StudyX86InstructionId previous = STUDY_X86_INSTRUCTION_NONE;
    size_t traversed = 0U;

    if (block->function != function_id ||
        instruction_id == STUDY_X86_INSTRUCTION_NONE ||
        block->last_instruction == STUDY_X86_INSTRUCTION_NONE) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            NULL,
            "a basic block is empty or belongs to another function"
        );
        return false;
    }

    while (instruction_id != STUDY_X86_INSTRUCTION_NONE) {
        const StudyX86Instruction *instruction;

        if (instruction_id >= program->instruction_count ||
            instruction_seen[instruction_id] ||
            traversed++ >= program->instruction_count) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                NULL,
                "an instruction chain is invalid"
            );
            return false;
        }
        instruction = &program->instructions[instruction_id];
        if (instruction->function != function_id ||
            instruction->block != block_id ||
            !study_x86_64_instruction_shape_is_valid(program, instruction) ||
            (study_x86_64_opcode_is_terminator(instruction->opcode) &&
             instruction->next_instruction != STUDY_X86_INSTRUCTION_NONE)) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                &instruction->range,
                "an instruction has invalid operands, ownership, or placement"
            );
            return false;
        }
        instruction_seen[instruction_id] = true;
        previous = instruction_id;
        instruction_id = instruction->next_instruction;
    }

    if (previous != block->last_instruction ||
        !study_x86_64_opcode_is_terminator(
            program->instructions[previous].opcode
        )) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            &program->instructions[previous].range,
            "a basic block does not end in an explicit terminator"
        );
        return false;
    }
    return true;
}

static bool study_x86_64_verify_function(
    const StudyX86Program *program,
    StudyX86FunctionId function_id,
    bool *block_seen,
    bool *instruction_seen,
    bool *storage_seen,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyX86Function *function = &program->functions[function_id];
    StudyX86BlockId block_id = function->first_block;
    StudyX86BlockId previous = STUDY_X86_BLOCK_NONE;
    size_t traversed = 0U;

    if (function->kind < STUDY_X86_FUNCTION_START ||
        function->kind > STUDY_X86_FUNCTION_STUDY ||
        function->entry_block != function->first_block ||
        !study_x86_64_target_is_in_function(
            program,
            function_id,
            function->entry_block
        ) ||
        function->frame_size % 16U != 0U ||
        (function->kind == STUDY_X86_FUNCTION_STUDY &&
         function->ir_function == STUDY_IR_FUNCTION_NONE) ||
        (function->kind != STUDY_X86_FUNCTION_STUDY &&
         function->ir_function != STUDY_IR_FUNCTION_NONE) ||
        !study_x86_64_verify_storage(
            program,
            function,
            function_id,
            storage_seen,
            diagnostic,
            source
        )) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                NULL,
                "a function record is incomplete"
            );
        }
        return false;
    }

    while (block_id != STUDY_X86_BLOCK_NONE) {
        if (block_id >= program->block_count || block_seen[block_id] ||
            traversed++ >= program->block_count ||
            !study_x86_64_verify_block(
                program,
                function_id,
                block_id,
                instruction_seen,
                diagnostic,
                source
            )) {
            if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
                study_x86_64_verify_error(
                    diagnostic,
                    source,
                    NULL,
                    "a function has an invalid basic-block chain"
                );
            }
            return false;
        }
        block_seen[block_id] = true;
        previous = block_id;
        block_id = program->blocks[block_id].next_block;
    }

    if (previous != function->last_block) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            NULL,
            "a function does not name the last block in its chain"
        );
        return false;
    }
    return true;
}

bool study_x86_64_verify(
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    bool *block_seen = NULL;
    bool *instruction_seen = NULL;
    bool *storage_seen = NULL;
    size_t index;
    size_t start_count = 0U;
    size_t print_count = 0U;
    bool success = false;

    study_diagnostic_clear(diagnostic);
    if (program == NULL || program->functions == NULL ||
        program->function_count == 0U || program->blocks == NULL ||
        program->block_count == 0U || program->instructions == NULL ||
        program->instruction_count == 0U ||
        (program->storage_count > 0U && program->storage == NULL) ||
        program->start_function >= program->function_count ||
        program->print_function >= program->function_count) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            NULL,
            "the program arrays or runtime entry functions are incomplete"
        );
        return false;
    }

    block_seen = calloc(program->block_count, sizeof(*block_seen));
    instruction_seen = calloc(
        program->instruction_count,
        sizeof(*instruction_seen)
    );
    storage_seen = calloc(
        program->storage_count > 0U ? program->storage_count : 1U,
        sizeof(*storage_seen)
    );
    if (block_seen == NULL || instruction_seen == NULL ||
        storage_seen == NULL) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            NULL,
            "not enough memory to verify the target program"
        );
        goto cleanup;
    }

    for (index = 0U; index < program->function_count; index++) {
        if (program->functions[index].kind == STUDY_X86_FUNCTION_START) {
            start_count += 1U;
        } else if (program->functions[index].kind ==
                   STUDY_X86_FUNCTION_PRINT_I32) {
            print_count += 1U;
        }
        if (!study_x86_64_verify_function(
                program,
                index,
                block_seen,
                instruction_seen,
                storage_seen,
                diagnostic,
                source
            )) {
            goto cleanup;
        }
    }

    if (start_count != 1U || print_count != 1U ||
        program->functions[program->start_function].kind !=
            STUDY_X86_FUNCTION_START ||
        program->functions[program->print_function].kind !=
            STUDY_X86_FUNCTION_PRINT_I32) {
        study_x86_64_verify_error(
            diagnostic,
            source,
            NULL,
            "the program must contain one named start and print function"
        );
        goto cleanup;
    }

    for (index = 0U; index < program->block_count; index++) {
        if (!block_seen[index]) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                NULL,
                "a basic block is not owned by a function"
            );
            goto cleanup;
        }
    }
    for (index = 0U; index < program->instruction_count; index++) {
        if (!instruction_seen[index]) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                NULL,
                "an instruction is not owned by a basic block"
            );
            goto cleanup;
        }
    }
    for (index = 0U; index < program->storage_count; index++) {
        if (!storage_seen[index]) {
            study_x86_64_verify_error(
                diagnostic,
                source,
                NULL,
                "a stack location is not owned by a function"
            );
            goto cleanup;
        }
    }

    success = true;

cleanup:
    free(block_seen);
    free(instruction_seen);
    free(storage_seen);
    return success;
}
