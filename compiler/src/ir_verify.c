#include "study/ir_verify.h"

#include <stdlib.h>
#include <string.h>

static bool study_ir_type_is_valid(StudySyntaxType type) {
    return type == STUDY_SYNTAX_TYPE_I32 || type == STUDY_SYNTAX_TYPE_BOOL;
}

static void study_ir_verify_error(
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        source != NULL ? source->path : NULL,
        "invalid typed IR: %s",
        message
    );
    if (range != NULL) {
        study_diagnostic_set_location(
            diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static bool study_ir_program_shape_is_valid(
    const StudyIrProgram *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    if (program == NULL || program->functions == NULL ||
        program->function_count == 0U || program->blocks == NULL ||
        program->block_count == 0U || program->instructions == NULL ||
        program->instruction_count == 0U || program->main_function >=
            program->function_count ||
        (program->slot_count > 0U && program->slots == NULL) ||
        (program->value_count > 0U && program->values == NULL)) {
        study_ir_verify_error(
            diagnostic,
            source,
            NULL,
            "the program arrays or main function are incomplete"
        );
        return false;
    }

    return true;
}

static bool study_ir_value_is_available(
    const StudyIrProgram *program,
    StudyIrFunctionId function_id,
    StudyIrValueId value_id,
    const bool *available,
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const StudySourceRange *range
) {
    if (value_id >= program->value_count || !available[value_id] ||
        program->values[value_id].function != function_id ||
        !study_ir_type_is_valid(program->values[value_id].type)) {
        study_ir_verify_error(
            diagnostic,
            source,
            range,
            "an operand is not defined earlier in the same block"
        );
        return false;
    }

    return true;
}

static bool study_ir_value_has_type(
    const StudyIrProgram *program,
    StudyIrValueId value_id,
    StudySyntaxType type,
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const StudySourceRange *range,
    const char *message
) {
    if (program->values[value_id].type != type) {
        study_ir_verify_error(diagnostic, source, range, message);
        return false;
    }

    return true;
}

static bool study_ir_slot_is_valid_for_function(
    const StudyIrProgram *program,
    StudyIrFunctionId function_id,
    StudyIrSlotId slot_id,
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const StudySourceRange *range
) {
    if (slot_id >= program->slot_count ||
        program->slots[slot_id].function != function_id ||
        !study_ir_type_is_valid(program->slots[slot_id].type)) {
        study_ir_verify_error(
            diagnostic,
            source,
            range,
            "an instruction refers to a slot outside its function"
        );
        return false;
    }

    return true;
}

static bool study_ir_block_is_valid_for_function(
    const StudyIrProgram *program,
    StudyIrFunctionId function_id,
    StudyIrBlockId block_id,
    StudyDiagnostic *diagnostic,
    const StudySource *source,
    const StudySourceRange *range
) {
    if (block_id >= program->block_count ||
        program->blocks[block_id].function != function_id) {
        study_ir_verify_error(
            diagnostic,
            source,
            range,
            "a control-flow target is outside its function"
        );
        return false;
    }

    return true;
}

static bool study_ir_opcode_has_result(StudyIrOpcode opcode) {
    switch (opcode) {
        case STUDY_IR_CONST_I32:
        case STUDY_IR_CONST_BOOL:
        case STUDY_IR_LOAD:
        case STUDY_IR_ADD:
        case STUDY_IR_SUBTRACT:
        case STUDY_IR_MULTIPLY:
        case STUDY_IR_NEGATE:
        case STUDY_IR_NOT:
        case STUDY_IR_EQUAL:
        case STUDY_IR_NOT_EQUAL:
        case STUDY_IR_LESS:
        case STUDY_IR_LESS_EQUAL:
        case STUDY_IR_GREATER:
        case STUDY_IR_GREATER_EQUAL:
        case STUDY_IR_CALL:
            return true;
        case STUDY_IR_STORE:
        case STUDY_IR_PRINT:
        case STUDY_IR_BRANCH:
        case STUDY_IR_JUMP:
        case STUDY_IR_RETURN:
            return false;
    }

    return false;
}

static bool study_ir_opcode_is_terminator(StudyIrOpcode opcode) {
    return opcode == STUDY_IR_BRANCH || opcode == STUDY_IR_JUMP ||
           opcode == STUDY_IR_RETURN;
}

static bool study_ir_verify_result(
    const StudyIrProgram *program,
    StudyIrInstructionId instruction_id,
    bool *value_seen,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyIrInstruction *instruction =
        &program->instructions[instruction_id];
    bool has_result = study_ir_opcode_has_result(instruction->opcode);

    if (!has_result) {
        if (instruction->result != STUDY_IR_VALUE_NONE) {
            study_ir_verify_error(
                diagnostic,
                source,
                &instruction->range,
                "a non-value instruction has a result"
            );
            return false;
        }
        return true;
    }

    if (instruction->result >= program->value_count ||
        value_seen[instruction->result] ||
        program->values[instruction->result].function != instruction->function ||
        program->values[instruction->result].definition != instruction_id ||
        !study_ir_type_is_valid(program->values[instruction->result].type)) {
        study_ir_verify_error(
            diagnostic,
            source,
            &instruction->range,
            "an instruction result does not have one matching value record"
        );
        return false;
    }

    value_seen[instruction->result] = true;
    return true;
}

static bool study_ir_verify_binary_i32(
    const StudyIrProgram *program,
    const StudyIrInstruction *instruction,
    const bool *available,
    StudySyntaxType result_type,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    if (!study_ir_value_is_available(
            program,
            instruction->function,
            instruction->as.binary.left,
            available,
            diagnostic,
            source,
            &instruction->range
        ) ||
        !study_ir_value_is_available(
            program,
            instruction->function,
            instruction->as.binary.right,
            available,
            diagnostic,
            source,
            &instruction->range
        ) ||
        !study_ir_value_has_type(
            program,
            instruction->as.binary.left,
            STUDY_SYNTAX_TYPE_I32,
            diagnostic,
            source,
            &instruction->range,
            "an integer operation has a non-i32 left operand"
        ) ||
        !study_ir_value_has_type(
            program,
            instruction->as.binary.right,
            STUDY_SYNTAX_TYPE_I32,
            diagnostic,
            source,
            &instruction->range,
            "an integer operation has a non-i32 right operand"
        ) ||
        !study_ir_value_has_type(
            program,
            instruction->result,
            result_type,
            diagnostic,
            source,
            &instruction->range,
            "an integer operation has the wrong result type"
        )) {
        return false;
    }

    return true;
}

static bool study_ir_verify_equal(
    const StudyIrProgram *program,
    const StudyIrInstruction *instruction,
    const bool *available,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    StudyIrValueId left = instruction->as.binary.left;
    StudyIrValueId right = instruction->as.binary.right;

    if (!study_ir_value_is_available(
            program,
            instruction->function,
            left,
            available,
            diagnostic,
            source,
            &instruction->range
        ) ||
        !study_ir_value_is_available(
            program,
            instruction->function,
            right,
            available,
            diagnostic,
            source,
            &instruction->range
        ) ||
        program->values[left].type != program->values[right].type ||
        !study_ir_value_has_type(
            program,
            instruction->result,
            STUDY_SYNTAX_TYPE_BOOL,
            diagnostic,
            source,
            &instruction->range,
            "an equality instruction has the wrong result type"
        )) {
        if (left < program->value_count && right < program->value_count &&
            program->values[left].type != program->values[right].type) {
            study_ir_verify_error(
                diagnostic,
                source,
                &instruction->range,
                "an equality instruction compares different types"
            );
        }
        return false;
    }

    return true;
}

static bool study_ir_verify_call(
    const StudyIrProgram *program,
    const StudyIrInstruction *instruction,
    const bool *available,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyIrFunction *callee;
    size_t index;

    if (instruction->as.call.function >= program->function_count ||
        instruction->as.call.argument_count >
            (size_t)STUDY_MAX_FUNCTION_PARAMETERS) {
        study_ir_verify_error(
            diagnostic,
            source,
            &instruction->range,
            "a call has an invalid callee or argument count"
        );
        return false;
    }

    callee = &program->functions[instruction->as.call.function];
    if (instruction->as.call.argument_count != callee->parameter_count ||
        !study_ir_value_has_type(
            program,
            instruction->result,
            callee->result_type,
            diagnostic,
            source,
            &instruction->range,
            "a call result does not match the callee result type"
        )) {
        study_ir_verify_error(
            diagnostic,
            source,
            &instruction->range,
            "a call does not match its function signature"
        );
        return false;
    }

    for (index = 0U; index < callee->parameter_count; index++) {
        StudyIrSlotId parameter_slot = callee->parameter_slots[index];
        StudyIrValueId argument = instruction->as.call.arguments[index];

        if (parameter_slot >= program->slot_count ||
            !study_ir_value_is_available(
                program,
                instruction->function,
                argument,
                available,
                diagnostic,
                source,
                &instruction->range
            ) ||
            program->values[argument].type !=
                program->slots[parameter_slot].type) {
            study_ir_verify_error(
                diagnostic,
                source,
                &instruction->range,
                "a call argument does not match its parameter type"
            );
            return false;
        }
    }

    return true;
}

static bool study_ir_verify_instruction(
    const StudyIrProgram *program,
    StudyIrInstructionId instruction_id,
    bool *available,
    bool *value_seen,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyIrInstruction *instruction =
        &program->instructions[instruction_id];
    StudyIrValueId operand;

    if (instruction->opcode < STUDY_IR_CONST_I32 ||
        instruction->opcode > STUDY_IR_RETURN) {
        study_ir_verify_error(
            diagnostic,
            source,
            &instruction->range,
            "an instruction has an unknown opcode"
        );
        return false;
    }

    if (!study_ir_verify_result(
            program,
            instruction_id,
            value_seen,
            diagnostic,
            source
        )) {
        return false;
    }

    switch (instruction->opcode) {
        case STUDY_IR_CONST_I32:
            if (!study_ir_value_has_type(
                    program,
                    instruction->result,
                    STUDY_SYNTAX_TYPE_I32,
                    diagnostic,
                    source,
                    &instruction->range,
                    "const.i32 has the wrong result type"
                )) {
                return false;
            }
            break;
        case STUDY_IR_CONST_BOOL:
            if (!study_ir_value_has_type(
                    program,
                    instruction->result,
                    STUDY_SYNTAX_TYPE_BOOL,
                    diagnostic,
                    source,
                    &instruction->range,
                    "const.bool has the wrong result type"
                )) {
                return false;
            }
            break;
        case STUDY_IR_LOAD:
            if (!study_ir_slot_is_valid_for_function(
                    program,
                    instruction->function,
                    instruction->as.load.slot,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                program->values[instruction->result].type !=
                    program->slots[instruction->as.load.slot].type) {
                study_ir_verify_error(
                    diagnostic,
                    source,
                    &instruction->range,
                    "load result does not match its slot type"
                );
                return false;
            }
            break;
        case STUDY_IR_STORE:
            if (!study_ir_slot_is_valid_for_function(
                    program,
                    instruction->function,
                    instruction->as.store.slot,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                !study_ir_value_is_available(
                    program,
                    instruction->function,
                    instruction->as.store.value,
                    available,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                program->slots[instruction->as.store.slot].type !=
                    program->values[instruction->as.store.value].type) {
                study_ir_verify_error(
                    diagnostic,
                    source,
                    &instruction->range,
                    "store value does not match its slot type"
                );
                return false;
            }
            break;
        case STUDY_IR_ADD:
        case STUDY_IR_SUBTRACT:
        case STUDY_IR_MULTIPLY:
            if (!study_ir_verify_binary_i32(
                    program,
                    instruction,
                    available,
                    STUDY_SYNTAX_TYPE_I32,
                    diagnostic,
                    source
                )) {
                return false;
            }
            break;
        case STUDY_IR_LESS:
        case STUDY_IR_LESS_EQUAL:
        case STUDY_IR_GREATER:
        case STUDY_IR_GREATER_EQUAL:
            if (!study_ir_verify_binary_i32(
                    program,
                    instruction,
                    available,
                    STUDY_SYNTAX_TYPE_BOOL,
                    diagnostic,
                    source
                )) {
                return false;
            }
            break;
        case STUDY_IR_NEGATE:
        case STUDY_IR_NOT:
            operand = instruction->as.unary.operand;
            if (!study_ir_value_is_available(
                    program,
                    instruction->function,
                    operand,
                    available,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                !study_ir_value_has_type(
                    program,
                    operand,
                    instruction->opcode == STUDY_IR_NEGATE
                        ? STUDY_SYNTAX_TYPE_I32
                        : STUDY_SYNTAX_TYPE_BOOL,
                    diagnostic,
                    source,
                    &instruction->range,
                    "a unary instruction has the wrong operand type"
                ) ||
                program->values[instruction->result].type !=
                    program->values[operand].type) {
                study_ir_verify_error(
                    diagnostic,
                    source,
                    &instruction->range,
                    "a unary instruction has inconsistent types"
                );
                return false;
            }
            break;
        case STUDY_IR_EQUAL:
        case STUDY_IR_NOT_EQUAL:
            if (!study_ir_verify_equal(
                    program,
                    instruction,
                    available,
                    diagnostic,
                    source
                )) {
                return false;
            }
            break;
        case STUDY_IR_CALL:
            if (!study_ir_verify_call(
                    program,
                    instruction,
                    available,
                    diagnostic,
                    source
                )) {
                return false;
            }
            break;
        case STUDY_IR_PRINT:
            operand = instruction->as.print.value;
            if (!study_ir_value_is_available(
                    program,
                    instruction->function,
                    operand,
                    available,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                !study_ir_value_has_type(
                    program,
                    operand,
                    STUDY_SYNTAX_TYPE_I32,
                    diagnostic,
                    source,
                    &instruction->range,
                    "print has a non-i32 operand"
                )) {
                return false;
            }
            break;
        case STUDY_IR_BRANCH:
            operand = instruction->as.branch.condition;
            if (!study_ir_value_is_available(
                    program,
                    instruction->function,
                    operand,
                    available,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                !study_ir_value_has_type(
                    program,
                    operand,
                    STUDY_SYNTAX_TYPE_BOOL,
                    diagnostic,
                    source,
                    &instruction->range,
                    "branch has a non-bool condition"
                ) ||
                !study_ir_block_is_valid_for_function(
                    program,
                    instruction->function,
                    instruction->as.branch.then_block,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                !study_ir_block_is_valid_for_function(
                    program,
                    instruction->function,
                    instruction->as.branch.else_block,
                    diagnostic,
                    source,
                    &instruction->range
                )) {
                return false;
            }
            break;
        case STUDY_IR_JUMP:
            if (!study_ir_block_is_valid_for_function(
                    program,
                    instruction->function,
                    instruction->as.jump.target,
                    diagnostic,
                    source,
                    &instruction->range
                )) {
                return false;
            }
            break;
        case STUDY_IR_RETURN:
            operand = instruction->as.return_value.value;
            if (!study_ir_value_is_available(
                    program,
                    instruction->function,
                    operand,
                    available,
                    diagnostic,
                    source,
                    &instruction->range
                ) ||
                program->values[operand].type !=
                    program->functions[instruction->function].result_type) {
                study_ir_verify_error(
                    diagnostic,
                    source,
                    &instruction->range,
                    "return value does not match its function result type"
                );
                return false;
            }
            break;
    }

    if (instruction->result != STUDY_IR_VALUE_NONE) {
        available[instruction->result] = true;
    }
    return true;
}

static bool study_ir_verify_block(
    const StudyIrProgram *program,
    StudyIrFunctionId function_id,
    StudyIrBlockId block_id,
    bool *instruction_seen,
    bool *value_seen,
    bool *available,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyIrBlock *block = &program->blocks[block_id];
    StudyIrInstructionId instruction_id = block->first_instruction;
    StudyIrInstructionId last = STUDY_IR_INSTRUCTION_NONE;
    size_t visited = 0U;

    if (instruction_id == STUDY_IR_INSTRUCTION_NONE ||
        block->last_instruction == STUDY_IR_INSTRUCTION_NONE) {
        study_ir_verify_error(
            diagnostic,
            source,
            NULL,
            "a basic block is empty"
        );
        return false;
    }

    if (program->value_count > 0U) {
        (void)memset(available, 0, program->value_count * sizeof(*available));
    }

    while (instruction_id != STUDY_IR_INSTRUCTION_NONE) {
        const StudyIrInstruction *instruction;

        if (instruction_id >= program->instruction_count ||
            instruction_seen[instruction_id] ||
            visited >= program->instruction_count) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "an instruction chain is invalid or shared by blocks"
            );
            return false;
        }

        instruction = &program->instructions[instruction_id];
        if (instruction->function != function_id ||
            instruction->block != block_id) {
            study_ir_verify_error(
                diagnostic,
                source,
                &instruction->range,
                "an instruction has the wrong function or block owner"
            );
            return false;
        }
        if (study_ir_opcode_is_terminator(instruction->opcode) &&
            instruction->next_instruction != STUDY_IR_INSTRUCTION_NONE) {
            study_ir_verify_error(
                diagnostic,
                source,
                &instruction->range,
                "a terminator is not the last instruction in its block"
            );
            return false;
        }

        instruction_seen[instruction_id] = true;
        if (!study_ir_verify_instruction(
                program,
                instruction_id,
                available,
                value_seen,
                diagnostic,
                source
            )) {
            return false;
        }

        last = instruction_id;
        instruction_id = instruction->next_instruction;
        visited += 1U;
    }

    if (last != block->last_instruction ||
        !study_ir_opcode_is_terminator(program->instructions[last].opcode)) {
        study_ir_verify_error(
            diagnostic,
            source,
            &program->instructions[last].range,
            "a basic block does not end in branch, jump, or return"
        );
        return false;
    }

    return true;
}

static bool study_ir_mark_reachable(
    const StudyIrProgram *program,
    StudyIrBlockId entry,
    bool *reachable,
    StudyIrBlockId *stack
) {
    size_t stack_count = 0U;

    reachable[entry] = true;
    stack[stack_count++] = entry;
    while (stack_count > 0U) {
        StudyIrBlockId block_id = stack[--stack_count];
        StudyIrInstructionId last_id;
        const StudyIrInstruction *last;

        last_id = program->blocks[block_id].last_instruction;
        last = &program->instructions[last_id];

        if (last->opcode == STUDY_IR_JUMP) {
            StudyIrBlockId target = last->as.jump.target;

            if (!reachable[target]) {
                reachable[target] = true;
                stack[stack_count++] = target;
            }
        } else if (last->opcode == STUDY_IR_BRANCH) {
            StudyIrBlockId then_block = last->as.branch.then_block;
            StudyIrBlockId else_block = last->as.branch.else_block;

            if (!reachable[then_block]) {
                reachable[then_block] = true;
                stack[stack_count++] = then_block;
            }
            if (!reachable[else_block]) {
                reachable[else_block] = true;
                stack[stack_count++] = else_block;
            }
        }
    }

    return true;
}

static bool study_ir_verify_function(
    const StudyIrProgram *program,
    StudyIrFunctionId function_id,
    bool *block_seen,
    bool *instruction_seen,
    bool *value_seen,
    bool *available,
    bool *reachable,
    StudyIrBlockId *stack,
    bool *parameter_slot_seen,
    StudyDiagnostic *diagnostic,
    const StudySource *source
) {
    const StudyIrFunction *function = &program->functions[function_id];
    StudyIrBlockId block_id;
    StudyIrBlockId last = STUDY_IR_BLOCK_NONE;
    size_t visited = 0U;
    size_t index;

    if (!study_ir_type_is_valid(function->result_type) ||
        function->parameter_count > (size_t)STUDY_MAX_FUNCTION_PARAMETERS ||
        function->first_block >= program->block_count ||
        function->last_block >= program->block_count ||
        function->entry_block >= program->block_count ||
        function->entry_block != function->first_block) {
        study_ir_verify_error(
            diagnostic,
            source,
            NULL,
            "a function header or entry block is invalid"
        );
        return false;
    }

    for (index = 0U; index < function->parameter_count; index++) {
        StudyIrSlotId slot_id = function->parameter_slots[index];

        if (slot_id >= program->slot_count || parameter_slot_seen[slot_id] ||
            program->slots[slot_id].function != function_id ||
            !program->slots[slot_id].is_parameter ||
            program->slots[slot_id].parameter_index != index) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a function parameter does not match its parameter slot"
            );
            return false;
        }
        parameter_slot_seen[slot_id] = true;
    }

    block_id = function->first_block;
    while (block_id != STUDY_IR_BLOCK_NONE) {
        if (block_id >= program->block_count || block_seen[block_id] ||
            visited >= program->block_count ||
            program->blocks[block_id].function != function_id) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a function block list is invalid or shared"
            );
            return false;
        }
        block_seen[block_id] = true;
        if (!study_ir_verify_block(
                program,
                function_id,
                block_id,
                instruction_seen,
                value_seen,
                available,
                diagnostic,
                source
            )) {
            return false;
        }
        last = block_id;
        block_id = program->blocks[block_id].next_block;
        visited += 1U;
    }

    if (last != function->last_block) {
        study_ir_verify_error(
            diagnostic,
            source,
            NULL,
            "a function last-block record is inconsistent"
        );
        return false;
    }

    if (program->block_count > 0U) {
        (void)memset(reachable, 0, program->block_count * sizeof(*reachable));
    }
    if (!study_ir_mark_reachable(
            program,
            function->entry_block,
            reachable,
            stack
        )) {
        return false;
    }

    block_id = function->first_block;
    while (block_id != STUDY_IR_BLOCK_NONE) {
        if (!reachable[block_id]) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a function contains an unreachable basic block"
            );
            return false;
        }
        block_id = program->blocks[block_id].next_block;
    }

    return true;
}

bool study_ir_verify(
    const StudyIrProgram *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    bool *block_seen = NULL;
    bool *instruction_seen = NULL;
    bool *value_seen = NULL;
    bool *available = NULL;
    bool *reachable = NULL;
    bool *parameter_slot_seen = NULL;
    StudyIrBlockId *stack = NULL;
    StudyIrFunctionId function_id;
    size_t index;
    bool success = false;

    study_diagnostic_clear(diagnostic);
    if (!study_ir_program_shape_is_valid(program, source, diagnostic)) {
        return false;
    }

    block_seen = calloc(program->block_count, sizeof(*block_seen));
    instruction_seen = calloc(
        program->instruction_count,
        sizeof(*instruction_seen)
    );
    reachable = calloc(program->block_count, sizeof(*reachable));
    stack = malloc(program->block_count * sizeof(*stack));
    if (program->value_count > 0U) {
        value_seen = calloc(program->value_count, sizeof(*value_seen));
        available = calloc(program->value_count, sizeof(*available));
    }
    if (program->slot_count > 0U) {
        parameter_slot_seen = calloc(
            program->slot_count,
            sizeof(*parameter_slot_seen)
        );
    }

    if (block_seen == NULL || instruction_seen == NULL || reachable == NULL ||
        stack == NULL ||
        (program->value_count > 0U &&
         (value_seen == NULL || available == NULL)) ||
        (program->slot_count > 0U && parameter_slot_seen == NULL)) {
        study_ir_verify_error(
            diagnostic,
            source,
            NULL,
            "not enough memory to verify the program"
        );
        goto cleanup;
    }

    for (index = 0U; index < program->slot_count; index++) {
        if (program->slots[index].function >= program->function_count ||
            !study_ir_type_is_valid(program->slots[index].type) ||
            (!program->slots[index].is_parameter &&
             program->slots[index].parameter_index != SIZE_MAX)) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a slot record is invalid"
            );
            goto cleanup;
        }
    }

    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        if (!study_ir_verify_function(
                program,
                function_id,
                block_seen,
                instruction_seen,
                value_seen,
                available,
                reachable,
                stack,
                parameter_slot_seen,
                diagnostic,
                source
            )) {
            goto cleanup;
        }
    }

    for (index = 0U; index < program->block_count; index++) {
        if (!block_seen[index]) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a block is not owned by any function"
            );
            goto cleanup;
        }
    }
    for (index = 0U; index < program->instruction_count; index++) {
        if (!instruction_seen[index]) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "an instruction is not owned by any block"
            );
            goto cleanup;
        }
    }
    for (index = 0U; index < program->value_count; index++) {
        if (!value_seen[index]) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a value is not defined by exactly one instruction"
            );
            goto cleanup;
        }
    }
    for (index = 0U; index < program->slot_count; index++) {
        if (program->slots[index].is_parameter &&
            !parameter_slot_seen[index]) {
            study_ir_verify_error(
                diagnostic,
                source,
                NULL,
                "a parameter slot is not listed by its function"
            );
            goto cleanup;
        }
    }

    if (program->functions[program->main_function].parameter_count != 0U ||
        program->functions[program->main_function].result_type !=
            STUDY_SYNTAX_TYPE_I32) {
        study_ir_verify_error(
            diagnostic,
            source,
            NULL,
            "the main function does not have signature fn main() -> i32"
        );
        goto cleanup;
    }

    success = true;

cleanup:
    free(block_seen);
    free(instruction_seen);
    free(value_seen);
    free(available);
    free(reachable);
    free(parameter_slot_seen);
    free(stack);
    return success;
}
