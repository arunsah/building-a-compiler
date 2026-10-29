#include "study/ir.h"

#include <inttypes.h>
#include <stdlib.h>

void study_ir_program_init(StudyIrProgram *program) {
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
    program->slots = NULL;
    program->slot_count = 0U;
    program->slot_capacity = 0U;
    program->values = NULL;
    program->value_count = 0U;
    program->value_capacity = 0U;
    program->main_function = STUDY_IR_FUNCTION_NONE;
}

void study_ir_program_free(StudyIrProgram *program) {
    if (program == NULL) {
        return;
    }

    free(program->functions);
    free(program->blocks);
    free(program->instructions);
    free(program->slots);
    free(program->values);
    study_ir_program_init(program);
}

const char *study_ir_opcode_name(StudyIrOpcode opcode) {
    switch (opcode) {
        case STUDY_IR_CONST_I32:
            return "const.i32";
        case STUDY_IR_CONST_BOOL:
            return "const.bool";
        case STUDY_IR_LOAD:
            return "load";
        case STUDY_IR_STORE:
            return "store";
        case STUDY_IR_ADD:
            return "add.i32";
        case STUDY_IR_SUBTRACT:
            return "subtract.i32";
        case STUDY_IR_MULTIPLY:
            return "multiply.i32";
        case STUDY_IR_NEGATE:
            return "negate.i32";
        case STUDY_IR_NOT:
            return "not.bool";
        case STUDY_IR_EQUAL:
            return "equal";
        case STUDY_IR_NOT_EQUAL:
            return "not-equal";
        case STUDY_IR_LESS:
            return "less.i32";
        case STUDY_IR_LESS_EQUAL:
            return "less-equal.i32";
        case STUDY_IR_GREATER:
            return "greater.i32";
        case STUDY_IR_GREATER_EQUAL:
            return "greater-equal.i32";
        case STUDY_IR_CALL:
            return "call";
        case STUDY_IR_PRINT:
            return "print.i32";
        case STUDY_IR_BRANCH:
            return "branch";
        case STUDY_IR_JUMP:
            return "jump";
        case STUDY_IR_RETURN:
            return "return";
    }

    return "unknown";
}

static bool study_ir_dump_spelling(
    FILE *stream,
    const StudySource *source,
    const StudyTokenList *tokens,
    size_t token_index
) {
    const StudyToken *token;
    size_t length;

    if (token_index >= tokens->count) {
        return false;
    }

    token = &tokens->items[token_index];
    if (token->range.start.byte_offset > token->range.end.byte_offset ||
        token->range.end.byte_offset > source->length) {
        return false;
    }

    length = token->range.end.byte_offset - token->range.start.byte_offset;
    return fwrite(
        source->bytes + token->range.start.byte_offset,
        1U,
        length,
        stream
    ) == length;
}

static bool study_ir_dump_value(FILE *stream, StudyIrValueId value) {
    return fprintf(stream, "%%v%zu", value) >= 0;
}

static bool study_ir_dump_instruction(
    FILE *stream,
    const StudyIrProgram *program,
    const StudySource *source,
    const StudyTokenList *tokens,
    StudyIrInstructionId instruction_id
) {
    const StudyIrInstruction *instruction;
    size_t index;

    if (instruction_id >= program->instruction_count) {
        return false;
    }

    instruction = &program->instructions[instruction_id];
    if (fputs("    ", stream) == EOF) {
        return false;
    }

    if (instruction->result != STUDY_IR_VALUE_NONE) {
        if (instruction->result >= program->value_count ||
            !study_ir_dump_value(stream, instruction->result) ||
            fprintf(
                stream,
                ": %s = ",
                study_syntax_type_name(
                    program->values[instruction->result].type
                )
            ) < 0) {
            return false;
        }
    }

    if (fputs(study_ir_opcode_name(instruction->opcode), stream) == EOF) {
        return false;
    }

    switch (instruction->opcode) {
        case STUDY_IR_CONST_I32:
            if (fprintf(stream, " %" PRIu32, instruction->as.integer) < 0) {
                return false;
            }
            break;
        case STUDY_IR_CONST_BOOL:
            if (fprintf(
                    stream,
                    " %s",
                    instruction->as.boolean ? "true" : "false"
                ) < 0) {
                return false;
            }
            break;
        case STUDY_IR_LOAD:
            if (fprintf(stream, " %%s%zu", instruction->as.load.slot) < 0) {
                return false;
            }
            break;
        case STUDY_IR_STORE:
            if (fprintf(stream, " %%s%zu, ", instruction->as.store.slot) < 0 ||
                !study_ir_dump_value(stream, instruction->as.store.value)) {
                return false;
            }
            break;
        case STUDY_IR_ADD:
        case STUDY_IR_SUBTRACT:
        case STUDY_IR_MULTIPLY:
        case STUDY_IR_EQUAL:
        case STUDY_IR_NOT_EQUAL:
        case STUDY_IR_LESS:
        case STUDY_IR_LESS_EQUAL:
        case STUDY_IR_GREATER:
        case STUDY_IR_GREATER_EQUAL:
            if (fputc(' ', stream) == EOF ||
                !study_ir_dump_value(stream, instruction->as.binary.left) ||
                fputs(", ", stream) == EOF ||
                !study_ir_dump_value(stream, instruction->as.binary.right)) {
                return false;
            }
            break;
        case STUDY_IR_NEGATE:
        case STUDY_IR_NOT:
            if (fputc(' ', stream) == EOF ||
                !study_ir_dump_value(stream, instruction->as.unary.operand)) {
                return false;
            }
            break;
        case STUDY_IR_CALL:
            if (instruction->as.call.argument_count >
                    (size_t)STUDY_MAX_FUNCTION_PARAMETERS ||
                fprintf(stream, " @") < 0 ||
                instruction->as.call.function >= program->function_count ||
                !study_ir_dump_spelling(
                    stream,
                    source,
                    tokens,
                    program->functions[instruction->as.call.function].name_token
                ) ||
                fputc('(', stream) == EOF) {
                return false;
            }
            for (index = 0U; index < instruction->as.call.argument_count; index++) {
                if ((index > 0U && fputs(", ", stream) == EOF) ||
                    !study_ir_dump_value(
                        stream,
                        instruction->as.call.arguments[index]
                    )) {
                    return false;
                }
            }
            if (fputc(')', stream) == EOF) {
                return false;
            }
            break;
        case STUDY_IR_PRINT:
            if (fputc(' ', stream) == EOF ||
                !study_ir_dump_value(stream, instruction->as.print.value)) {
                return false;
            }
            break;
        case STUDY_IR_BRANCH:
            if (fputc(' ', stream) == EOF ||
                !study_ir_dump_value(stream, instruction->as.branch.condition) ||
                fprintf(
                    stream,
                    ", b%zu, b%zu",
                    instruction->as.branch.then_block,
                    instruction->as.branch.else_block
                ) < 0) {
                return false;
            }
            break;
        case STUDY_IR_JUMP:
            if (fprintf(stream, " b%zu", instruction->as.jump.target) < 0) {
                return false;
            }
            break;
        case STUDY_IR_RETURN:
            if (fputc(' ', stream) == EOF ||
                !study_ir_dump_value(
                    stream,
                    instruction->as.return_value.value
                )) {
                return false;
            }
            break;
    }

    return fprintf(
        stream,
        "  ; %zu:%zu..%zu:%zu\n",
        instruction->range.start.line,
        instruction->range.start.column,
        instruction->range.end.line,
        instruction->range.end.column
    ) >= 0;
}

static bool study_ir_dump_function(
    FILE *stream,
    const StudyIrProgram *program,
    const StudySource *source,
    const StudyTokenList *tokens,
    StudyIrFunctionId function_id
) {
    const StudyIrFunction *function = &program->functions[function_id];
    StudyIrBlockId block_id;
    size_t index;
    size_t block_count = 0U;

    if (fputs("function @", stream) == EOF ||
        !study_ir_dump_spelling(
            stream,
            source,
            tokens,
            function->name_token
        ) ||
        fputc('(', stream) == EOF) {
        return false;
    }

    if (function->parameter_count >
        (size_t)STUDY_MAX_FUNCTION_PARAMETERS) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; index++) {
        StudyIrSlotId slot_id = function->parameter_slots[index];

        if (slot_id >= program->slot_count ||
            (index > 0U && fputs(", ", stream) == EOF) ||
            fprintf(
                stream,
                "%%s%zu: %s",
                slot_id,
                study_syntax_type_name(program->slots[slot_id].type)
            ) < 0) {
            return false;
        }
    }

    if (fprintf(
            stream,
            ") -> %s {\n",
            study_syntax_type_name(function->result_type)
        ) < 0) {
        return false;
    }

    if (fputs("  slots:\n", stream) == EOF) {
        return false;
    }
    for (index = 0U; index < program->slot_count; index++) {
        const StudyIrSlot *slot = &program->slots[index];

        if (slot->function != function_id) {
            continue;
        }
        if (fprintf(stream, "    %%s%zu ", index) < 0 ||
            !study_ir_dump_spelling(
                stream,
                source,
                tokens,
                slot->name_token
            ) ||
            fprintf(
                stream,
                ": %s%s\n",
                study_syntax_type_name(slot->type),
                slot->is_parameter ? "  parameter" : ""
            ) < 0) {
            return false;
        }
    }

    block_id = function->first_block;
    while (block_id != STUDY_IR_BLOCK_NONE) {
        const StudyIrBlock *block;
        StudyIrInstructionId instruction_id;
        size_t instruction_count = 0U;

        if (block_id >= program->block_count ||
            block_count >= program->block_count) {
            return false;
        }
        block = &program->blocks[block_id];
        if (fprintf(
                stream,
                "  b%zu:%s\n",
                block_id,
                block_id == function->entry_block ? "  ; entry" : ""
            ) < 0) {
            return false;
        }

        instruction_id = block->first_instruction;
        while (instruction_id != STUDY_IR_INSTRUCTION_NONE) {
            if (instruction_count >= program->instruction_count ||
                !study_ir_dump_instruction(
                    stream,
                    program,
                    source,
                    tokens,
                    instruction_id
                )) {
                return false;
            }
            instruction_id = program->instructions[instruction_id]
                                 .next_instruction;
            instruction_count += 1U;
        }
        block_id = block->next_block;
        block_count += 1U;
    }

    return fputs("}\n", stream) != EOF;
}

bool study_ir_program_dump(
    FILE *stream,
    const StudyIrProgram *program,
    const StudySource *source,
    const StudyTokenList *tokens
) {
    StudyIrFunctionId function_id;

    if (stream == NULL || program == NULL || source == NULL || tokens == NULL ||
        program->functions == NULL || program->function_count == 0U) {
        return false;
    }

    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        if (function_id > 0U && fputc('\n', stream) == EOF) {
            return false;
        }
        if (!study_ir_dump_function(
                stream,
                program,
                source,
                tokens,
                function_id
            )) {
            return false;
        }
    }

    return true;
}
