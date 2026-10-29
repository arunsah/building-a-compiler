#include "study/interpreter.h"

#include "study/ir_verify.h"

#include <inttypes.h>
#include <stdlib.h>

enum {
    STUDY_INTERPRETER_MAX_CALL_DEPTH = 256,
    STUDY_INTERPRETER_MAX_INSTRUCTIONS = 1000000
};

typedef struct StudyRuntimeValue {
    StudySyntaxType type;
    uint32_t bits;
} StudyRuntimeValue;

typedef struct StudyInterpreter {
    const StudyIrProgram *program;
    const StudySource *source;
    FILE *stream;
    size_t instruction_count;
    size_t call_depth;
    StudyDiagnostic *diagnostic;
} StudyInterpreter;

static void study_interpreter_error(
    StudyInterpreter *interpreter,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        interpreter->diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        interpreter->source != NULL ? interpreter->source->path : NULL,
        "reference interpreter: %s",
        message
    );
    if (range != NULL) {
        study_diagnostic_set_location(
            interpreter->diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static int64_t study_i32_signed_value(uint32_t bits) {
    if (bits <= UINT32_C(2147483647)) {
        return (int64_t)bits;
    }

    return (int64_t)bits - INT64_C(4294967296);
}

static bool study_interpret_function(
    StudyInterpreter *interpreter,
    StudyIrFunctionId function_id,
    const StudyRuntimeValue *arguments,
    size_t argument_count,
    StudyRuntimeValue *result
) {
    const StudyIrProgram *program = interpreter->program;
    const StudyIrFunction *function = &program->functions[function_id];
    StudyRuntimeValue *slots = NULL;
    StudyRuntimeValue *values = NULL;
    bool *slot_initialized = NULL;
    bool *value_initialized = NULL;
    StudyIrBlockId block_id = function->entry_block;
    size_t index;
    size_t slot_capacity = program->slot_count > 0U
                               ? program->slot_count
                               : 1U;
    size_t value_capacity = program->value_count > 0U
                                ? program->value_count
                                : 1U;
    bool success = false;

    if (interpreter->call_depth >=
        (size_t)STUDY_INTERPRETER_MAX_CALL_DEPTH) {
        study_interpreter_error(
            interpreter,
            NULL,
            "call depth exceeded 256 functions"
        );
        return false;
    }
    if (argument_count != function->parameter_count) {
        study_interpreter_error(
            interpreter,
            NULL,
            "a call reached a function with the wrong argument count"
        );
        return false;
    }

    slots = calloc(slot_capacity, sizeof(*slots));
    slot_initialized = calloc(slot_capacity, sizeof(*slot_initialized));
    values = calloc(value_capacity, sizeof(*values));
    value_initialized = calloc(value_capacity, sizeof(*value_initialized));
    if (slots == NULL || slot_initialized == NULL || values == NULL ||
        value_initialized == NULL) {
        study_interpreter_error(
            interpreter,
            NULL,
            "not enough memory for a call frame"
        );
        goto cleanup;
    }

    for (index = 0U; index < argument_count; index++) {
        StudyIrSlotId slot_id = function->parameter_slots[index];

        slots[slot_id] = arguments[index];
        slot_initialized[slot_id] = true;
    }

    interpreter->call_depth += 1U;
    for (;;) {
        StudyIrInstructionId instruction_id =
            program->blocks[block_id].first_instruction;
        bool moved_to_block = false;

        while (instruction_id != STUDY_IR_INSTRUCTION_NONE) {
            const StudyIrInstruction *instruction =
                &program->instructions[instruction_id];
            StudyRuntimeValue value = {0};
            StudyRuntimeValue left;
            StudyRuntimeValue right;
            StudyIrValueId result_id = instruction->result;

            if (interpreter->instruction_count >=
                (size_t)STUDY_INTERPRETER_MAX_INSTRUCTIONS) {
                study_interpreter_error(
                    interpreter,
                    &instruction->range,
                    "execution exceeded 1000000 instructions"
                );
                goto leave_function;
            }
            interpreter->instruction_count += 1U;

            switch (instruction->opcode) {
                case STUDY_IR_CONST_I32:
                    value.type = STUDY_SYNTAX_TYPE_I32;
                    value.bits = instruction->as.integer;
                    break;
                case STUDY_IR_CONST_BOOL:
                    value.type = STUDY_SYNTAX_TYPE_BOOL;
                    value.bits = instruction->as.boolean ? 1U : 0U;
                    break;
                case STUDY_IR_LOAD: {
                    StudyIrSlotId slot_id = instruction->as.load.slot;

                    if (!slot_initialized[slot_id]) {
                        study_interpreter_error(
                            interpreter,
                            &instruction->range,
                            "load reached an uninitialised slot"
                        );
                        goto leave_function;
                    }
                    value = slots[slot_id];
                    break;
                }
                case STUDY_IR_STORE: {
                    StudyIrValueId source_id = instruction->as.store.value;

                    if (!value_initialized[source_id]) {
                        study_interpreter_error(
                            interpreter,
                            &instruction->range,
                            "store reached an unavailable value"
                        );
                        goto leave_function;
                    }
                    slots[instruction->as.store.slot] = values[source_id];
                    slot_initialized[instruction->as.store.slot] = true;
                    break;
                }
                case STUDY_IR_ADD:
                case STUDY_IR_SUBTRACT:
                case STUDY_IR_MULTIPLY:
                case STUDY_IR_EQUAL:
                case STUDY_IR_NOT_EQUAL:
                case STUDY_IR_LESS:
                case STUDY_IR_LESS_EQUAL:
                case STUDY_IR_GREATER:
                case STUDY_IR_GREATER_EQUAL:
                    left = values[instruction->as.binary.left];
                    right = values[instruction->as.binary.right];
                    if (instruction->opcode == STUDY_IR_ADD) {
                        value.type = STUDY_SYNTAX_TYPE_I32;
                        value.bits = left.bits + right.bits;
                    } else if (instruction->opcode == STUDY_IR_SUBTRACT) {
                        value.type = STUDY_SYNTAX_TYPE_I32;
                        value.bits = left.bits - right.bits;
                    } else if (instruction->opcode == STUDY_IR_MULTIPLY) {
                        value.type = STUDY_SYNTAX_TYPE_I32;
                        value.bits = left.bits * right.bits;
                    } else {
                        int64_t signed_left = study_i32_signed_value(left.bits);
                        int64_t signed_right = study_i32_signed_value(right.bits);

                        value.type = STUDY_SYNTAX_TYPE_BOOL;
                        if (instruction->opcode == STUDY_IR_EQUAL) {
                            value.bits = left.bits == right.bits ? 1U : 0U;
                        } else if (instruction->opcode == STUDY_IR_NOT_EQUAL) {
                            value.bits = left.bits != right.bits ? 1U : 0U;
                        } else if (instruction->opcode == STUDY_IR_LESS) {
                            value.bits = signed_left < signed_right ? 1U : 0U;
                        } else if (instruction->opcode == STUDY_IR_LESS_EQUAL) {
                            value.bits = signed_left <= signed_right ? 1U : 0U;
                        } else if (instruction->opcode == STUDY_IR_GREATER) {
                            value.bits = signed_left > signed_right ? 1U : 0U;
                        } else {
                            value.bits = signed_left >= signed_right ? 1U : 0U;
                        }
                    }
                    break;
                case STUDY_IR_NEGATE:
                    value.type = STUDY_SYNTAX_TYPE_I32;
                    value.bits =
                        0U - values[instruction->as.unary.operand].bits;
                    break;
                case STUDY_IR_NOT:
                    value.type = STUDY_SYNTAX_TYPE_BOOL;
                    value.bits =
                        values[instruction->as.unary.operand].bits == 0U
                            ? 1U
                            : 0U;
                    break;
                case STUDY_IR_CALL: {
                    StudyRuntimeValue call_arguments[
                        STUDY_MAX_FUNCTION_PARAMETERS
                    ];

                    for (index = 0U;
                         index < instruction->as.call.argument_count;
                         index++) {
                        call_arguments[index] = values[
                            instruction->as.call.arguments[index]
                        ];
                    }
                    if (!study_interpret_function(
                            interpreter,
                            instruction->as.call.function,
                            call_arguments,
                            instruction->as.call.argument_count,
                            &value
                        )) {
                        goto leave_function;
                    }
                    break;
                }
                case STUDY_IR_PRINT: {
                    StudyRuntimeValue printed =
                        values[instruction->as.print.value];

                    if (fprintf(
                            interpreter->stream,
                            "%" PRId64 "\n",
                            study_i32_signed_value(printed.bits)
                        ) < 0) {
                        study_interpreter_error(
                            interpreter,
                            &instruction->range,
                            "could not write program output"
                        );
                        goto leave_function;
                    }
                    break;
                }
                case STUDY_IR_BRANCH:
                    block_id =
                        values[instruction->as.branch.condition].bits != 0U
                            ? instruction->as.branch.then_block
                            : instruction->as.branch.else_block;
                    moved_to_block = true;
                    break;
                case STUDY_IR_JUMP:
                    block_id = instruction->as.jump.target;
                    moved_to_block = true;
                    break;
                case STUDY_IR_RETURN:
                    *result = values[instruction->as.return_value.value];
                    success = true;
                    goto leave_function;
            }

            if (result_id != STUDY_IR_VALUE_NONE) {
                values[result_id] = value;
                value_initialized[result_id] = true;
            }
            if (moved_to_block) {
                break;
            }
            instruction_id = instruction->next_instruction;
        }

        if (!moved_to_block) {
            study_interpreter_error(
                interpreter,
                NULL,
                "execution left a block without a terminator"
            );
            break;
        }
    }

leave_function:
    interpreter->call_depth -= 1U;

cleanup:
    free(slots);
    free(values);
    free(slot_initialized);
    free(value_initialized);
    return success;
}

bool study_ir_interpret(
    const StudyIrProgram *program,
    const StudySource *source,
    FILE *stream,
    uint32_t *main_result,
    StudyDiagnostic *diagnostic
) {
    StudyInterpreter interpreter;
    StudyRuntimeValue result;

    study_diagnostic_clear(diagnostic);
    if (stream == NULL || main_result == NULL ||
        !study_ir_verify(program, source, diagnostic)) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_diagnostic_set(
                diagnostic,
                STUDY_DIAGNOSTIC_INTERNAL,
                source != NULL ? source->path : NULL,
                "reference interpreter requires an output stream and result"
            );
        }
        return false;
    }

    interpreter.program = program;
    interpreter.source = source;
    interpreter.stream = stream;
    interpreter.instruction_count = 0U;
    interpreter.call_depth = 0U;
    interpreter.diagnostic = diagnostic;

    if (!study_interpret_function(
            &interpreter,
            program->main_function,
            NULL,
            0U,
            &result
        )) {
        return false;
    }

    if (fflush(stream) != 0) {
        study_interpreter_error(
            &interpreter,
            NULL,
            "could not flush program output"
        );
        return false;
    }

    *main_result = result.bits;
    return true;
}
