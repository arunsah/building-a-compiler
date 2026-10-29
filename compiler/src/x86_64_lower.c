#include "study/x86_64_lower.h"

#include "study/ir_verify.h"
#include "study/x86_64_verify.h"

#include <limits.h>
#include <stdlib.h>

enum { STUDY_X86_INITIAL_CAPACITY = 32 };

typedef struct StudyX86Builder {
    const StudyIrProgram *ir;
    const StudySource *source;
    StudyX86Program program;
    StudyX86FunctionId *ir_functions;
    StudyX86BlockId *ir_blocks;
    int32_t *slot_displacements;
    int32_t *value_displacements;
    StudyX86FunctionId current_function;
    StudyX86BlockId current_block;
    StudyDiagnostic *diagnostic;
} StudyX86Builder;

static bool study_x86_program_is_empty(const StudyX86Program *program) {
    return program != NULL && program->functions == NULL &&
           program->function_count == 0U &&
           program->function_capacity == 0U && program->blocks == NULL &&
           program->block_count == 0U && program->block_capacity == 0U &&
           program->instructions == NULL && program->instruction_count == 0U &&
           program->instruction_capacity == 0U && program->storage == NULL &&
           program->storage_count == 0U && program->storage_capacity == 0U &&
           program->start_function == STUDY_X86_FUNCTION_NONE &&
           program->print_function == STUDY_X86_FUNCTION_NONE;
}

static void study_x86_lower_error(
    StudyX86Builder *builder,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        builder->diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        builder->source != NULL ? builder->source->path : NULL,
        "could not lower typed IR to x86-64: %s",
        message
    );
    if (range != NULL && range->start.line > 0U) {
        study_diagnostic_set_location(
            builder->diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static void *study_x86_reserve_array(
    void *items,
    size_t *capacity,
    size_t count,
    size_t item_size,
    StudyX86Builder *builder
) {
    size_t next_capacity;
    void *grown;

    if (count < *capacity) {
        return items;
    }
    if (*capacity == 0U) {
        next_capacity = STUDY_X86_INITIAL_CAPACITY;
    } else {
        if (*capacity > SIZE_MAX / 2U) {
            study_x86_lower_error(builder, NULL, "a target array grew too large");
            return NULL;
        }
        next_capacity = *capacity * 2U;
    }
    if (next_capacity > SIZE_MAX / item_size) {
        study_x86_lower_error(builder, NULL, "a target array grew too large");
        return NULL;
    }

    grown = realloc(items, next_capacity * item_size);
    if (grown == NULL) {
        study_x86_lower_error(
            builder,
            NULL,
            "not enough memory for the x86-64 target program"
        );
        return NULL;
    }
    *capacity = next_capacity;
    return grown;
}

static bool study_x86_reserve_function(StudyX86Builder *builder) {
    void *grown = study_x86_reserve_array(
        builder->program.functions,
        &builder->program.function_capacity,
        builder->program.function_count,
        sizeof(*builder->program.functions),
        builder
    );

    if (grown == NULL) {
        return false;
    }
    builder->program.functions = grown;
    return true;
}

static bool study_x86_reserve_block(StudyX86Builder *builder) {
    void *grown = study_x86_reserve_array(
        builder->program.blocks,
        &builder->program.block_capacity,
        builder->program.block_count,
        sizeof(*builder->program.blocks),
        builder
    );

    if (grown == NULL) {
        return false;
    }
    builder->program.blocks = grown;
    return true;
}

static bool study_x86_reserve_instruction(StudyX86Builder *builder) {
    void *grown = study_x86_reserve_array(
        builder->program.instructions,
        &builder->program.instruction_capacity,
        builder->program.instruction_count,
        sizeof(*builder->program.instructions),
        builder
    );

    if (grown == NULL) {
        return false;
    }
    builder->program.instructions = grown;
    return true;
}

static bool study_x86_reserve_storage(StudyX86Builder *builder) {
    void *grown = study_x86_reserve_array(
        builder->program.storage,
        &builder->program.storage_capacity,
        builder->program.storage_count,
        sizeof(*builder->program.storage),
        builder
    );

    if (grown == NULL) {
        return false;
    }
    builder->program.storage = grown;
    return true;
}

static StudyX86Operand study_x86_none(void) {
    StudyX86Operand operand = {0};

    operand.kind = STUDY_X86_OPERAND_NONE;
    return operand;
}

static StudyX86Operand study_x86_register(
    StudyX86Register register_id,
    StudyX86Width width
) {
    StudyX86Operand operand = {0};

    operand.kind = STUDY_X86_OPERAND_REGISTER;
    operand.width = width;
    operand.as.register_id = register_id;
    return operand;
}

static StudyX86Operand study_x86_unsigned(
    uint64_t value,
    StudyX86Width width
) {
    StudyX86Operand operand = {0};

    operand.kind = STUDY_X86_OPERAND_IMMEDIATE;
    operand.width = width;
    operand.as.immediate.value = value;
    operand.as.immediate.is_signed = false;
    return operand;
}

static StudyX86Operand study_x86_signed(
    int64_t value,
    StudyX86Width width
) {
    StudyX86Operand operand = {0};

    operand.kind = STUDY_X86_OPERAND_IMMEDIATE;
    operand.width = width;
    operand.as.immediate.value = (uint64_t)value;
    operand.as.immediate.is_signed = true;
    return operand;
}

static StudyX86Operand study_x86_memory(
    StudyX86Register base,
    int32_t displacement,
    StudyX86Width width
) {
    StudyX86Operand operand = {0};

    operand.kind = STUDY_X86_OPERAND_MEMORY;
    operand.width = width;
    operand.as.memory.base = base;
    operand.as.memory.displacement = displacement;
    return operand;
}

static StudyX86Instruction study_x86_instruction(
    StudyX86Opcode opcode,
    StudySourceRange range
) {
    StudyX86Instruction instruction = {0};

    instruction.opcode = opcode;
    instruction.range = range;
    instruction.first = study_x86_none();
    instruction.second = study_x86_none();
    instruction.target_function = STUDY_X86_FUNCTION_NONE;
    instruction.then_block = STUDY_X86_BLOCK_NONE;
    instruction.else_block = STUDY_X86_BLOCK_NONE;
    instruction.next_instruction = STUDY_X86_INSTRUCTION_NONE;
    return instruction;
}

static bool study_x86_add_function(
    StudyX86Builder *builder,
    StudyX86FunctionKind kind,
    StudyIrFunctionId ir_function,
    StudyX86FunctionId *result
) {
    StudyX86Function function = {0};

    if (!study_x86_reserve_function(builder)) {
        return false;
    }
    function.kind = kind;
    function.ir_function = ir_function;
    function.first_storage = builder->program.storage_count;
    function.entry_block = STUDY_X86_BLOCK_NONE;
    function.first_block = STUDY_X86_BLOCK_NONE;
    function.last_block = STUDY_X86_BLOCK_NONE;
    *result = builder->program.function_count;
    builder->program.functions[builder->program.function_count++] = function;
    return true;
}

static bool study_x86_add_block(
    StudyX86Builder *builder,
    StudyX86FunctionId function_id,
    StudyX86BlockId *result
) {
    StudyX86Block block;
    StudyX86Function *function;

    if (function_id >= builder->program.function_count ||
        !study_x86_reserve_block(builder)) {
        return false;
    }
    block.function = function_id;
    block.first_instruction = STUDY_X86_INSTRUCTION_NONE;
    block.last_instruction = STUDY_X86_INSTRUCTION_NONE;
    block.next_block = STUDY_X86_BLOCK_NONE;
    *result = builder->program.block_count;
    builder->program.blocks[builder->program.block_count++] = block;

    function = &builder->program.functions[function_id];
    if (function->first_block == STUDY_X86_BLOCK_NONE) {
        function->first_block = *result;
        function->entry_block = *result;
    } else {
        builder->program.blocks[function->last_block].next_block = *result;
    }
    function->last_block = *result;
    return true;
}

static bool study_x86_append_instruction(
    StudyX86Builder *builder,
    StudyX86Instruction instruction
) {
    StudyX86Block *block;
    StudyX86InstructionId instruction_id;

    if (builder->current_function >= builder->program.function_count ||
        builder->current_block >= builder->program.block_count ||
        builder->program.blocks[builder->current_block].function !=
            builder->current_function ||
        !study_x86_reserve_instruction(builder)) {
        return false;
    }

    block = &builder->program.blocks[builder->current_block];
    instruction.function = builder->current_function;
    instruction.block = builder->current_block;
    instruction.next_instruction = STUDY_X86_INSTRUCTION_NONE;
    instruction_id = builder->program.instruction_count;
    builder->program.instructions[builder->program.instruction_count++] =
        instruction;
    if (block->first_instruction == STUDY_X86_INSTRUCTION_NONE) {
        block->first_instruction = instruction_id;
    } else {
        builder->program.instructions[block->last_instruction]
            .next_instruction = instruction_id;
    }
    block->last_instruction = instruction_id;
    return true;
}

static bool study_x86_emit_binary(
    StudyX86Builder *builder,
    StudyX86Opcode opcode,
    StudyX86Operand first,
    StudyX86Operand second,
    StudySourceRange range
) {
    StudyX86Instruction instruction = study_x86_instruction(opcode, range);

    instruction.first = first;
    instruction.second = second;
    return study_x86_append_instruction(builder, instruction);
}

static bool study_x86_emit_unary(
    StudyX86Builder *builder,
    StudyX86Opcode opcode,
    StudyX86Operand operand,
    StudySourceRange range
) {
    StudyX86Instruction instruction = study_x86_instruction(opcode, range);

    instruction.first = operand;
    return study_x86_append_instruction(builder, instruction);
}

static bool study_x86_emit_simple(
    StudyX86Builder *builder,
    StudyX86Opcode opcode,
    StudySourceRange range
) {
    return study_x86_append_instruction(
        builder,
        study_x86_instruction(opcode, range)
    );
}

static bool study_x86_emit_set(
    StudyX86Builder *builder,
    StudyX86Condition condition,
    StudyX86Operand destination,
    StudySourceRange range
) {
    StudyX86Instruction instruction =
        study_x86_instruction(STUDY_X86_SET_CONDITION, range);

    instruction.condition = condition;
    instruction.first = destination;
    return study_x86_append_instruction(builder, instruction);
}

static bool study_x86_emit_call(
    StudyX86Builder *builder,
    StudyX86FunctionId target,
    StudySourceRange range
) {
    StudyX86Instruction instruction =
        study_x86_instruction(STUDY_X86_CALL, range);

    instruction.target_function = target;
    return study_x86_append_instruction(builder, instruction);
}

static bool study_x86_emit_jump(
    StudyX86Builder *builder,
    StudyX86BlockId target,
    StudySourceRange range
) {
    StudyX86Instruction instruction =
        study_x86_instruction(STUDY_X86_JUMP, range);

    instruction.then_block = target;
    return study_x86_append_instruction(builder, instruction);
}

static bool study_x86_emit_branch(
    StudyX86Builder *builder,
    StudyX86Condition condition,
    StudyX86BlockId then_block,
    StudyX86BlockId else_block,
    StudySourceRange range
) {
    StudyX86Instruction instruction =
        study_x86_instruction(STUDY_X86_BRANCH_CONDITION, range);

    instruction.condition = condition;
    instruction.then_block = then_block;
    instruction.else_block = else_block;
    return study_x86_append_instruction(builder, instruction);
}

static bool study_x86_add_storage(
    StudyX86Builder *builder,
    StudyX86StorageKind kind,
    size_t ir_id,
    int32_t displacement
) {
    StudyX86Storage storage;
    StudyX86Function *function;

    if (!study_x86_reserve_storage(builder)) {
        return false;
    }
    storage.function = builder->current_function;
    storage.kind = kind;
    storage.ir_id = ir_id;
    storage.displacement = displacement;
    storage.width = STUDY_X86_WIDTH_32;
    builder->program.storage[builder->program.storage_count++] = storage;
    function = &builder->program.functions[builder->current_function];
    function->storage_count += 1U;
    return true;
}

static bool study_x86_layout_function(
    StudyX86Builder *builder,
    StudyIrFunctionId ir_function
) {
    size_t offset = 0U;
    size_t index;
    StudyX86Function *function;

    builder->current_function = builder->ir_functions[ir_function];
    function = &builder->program.functions[builder->current_function];
    function->first_storage = builder->program.storage_count;

    for (index = 0U; index < builder->ir->slot_count; index++) {
        if (builder->ir->slots[index].function == ir_function) {
            if (offset > (size_t)INT32_MAX - 8U) {
                study_x86_lower_error(builder, NULL, "a stack frame grew too large");
                return false;
            }
            offset += 8U;
            builder->slot_displacements[index] = -(int32_t)offset;
            if (!study_x86_add_storage(
                    builder,
                    STUDY_X86_STORAGE_SLOT,
                    index,
                    builder->slot_displacements[index]
                )) {
                return false;
            }
        }
    }
    for (index = 0U; index < builder->ir->value_count; index++) {
        if (builder->ir->values[index].function == ir_function) {
            if (offset > (size_t)INT32_MAX - 8U) {
                study_x86_lower_error(builder, NULL, "a stack frame grew too large");
                return false;
            }
            offset += 8U;
            builder->value_displacements[index] = -(int32_t)offset;
            if (!study_x86_add_storage(
                    builder,
                    STUDY_X86_STORAGE_VALUE,
                    index,
                    builder->value_displacements[index]
                )) {
                return false;
            }
        }
    }

    if (offset > (size_t)INT32_MAX - 15U) {
        study_x86_lower_error(builder, NULL, "a stack frame grew too large");
        return false;
    }
    function->frame_size = (offset + 15U) & ~(size_t)15U;
    return true;
}

static StudyX86Operand study_x86_slot(
    StudyX86Builder *builder,
    StudyIrSlotId slot
) {
    if (slot >= builder->ir->slot_count ||
        builder->slot_displacements[slot] == 0) {
        study_x86_lower_error(builder, NULL, "an IR slot has no stack location");
        return study_x86_none();
    }
    return study_x86_memory(
        STUDY_X86_REGISTER_RBP,
        builder->slot_displacements[slot],
        STUDY_X86_WIDTH_32
    );
}

static StudyX86Operand study_x86_value(
    StudyX86Builder *builder,
    StudyIrValueId value
) {
    if (value >= builder->ir->value_count ||
        builder->value_displacements[value] == 0) {
        study_x86_lower_error(builder, NULL, "an IR value has no stack location");
        return study_x86_none();
    }
    return study_x86_memory(
        STUDY_X86_REGISTER_RBP,
        builder->value_displacements[value],
        STUDY_X86_WIDTH_32
    );
}

static bool study_x86_store_eax(
    StudyX86Builder *builder,
    StudyIrValueId value,
    StudySourceRange range
) {
    StudyX86Operand destination = study_x86_value(builder, value);

    return destination.kind != STUDY_X86_OPERAND_NONE &&
           study_x86_emit_binary(
               builder,
               STUDY_X86_MOV,
               study_x86_register(
                   STUDY_X86_REGISTER_RAX,
                   STUDY_X86_WIDTH_32
               ),
               destination,
               range
           );
}

static bool study_x86_condition_for_ir(
    StudyIrOpcode opcode,
    StudyX86Condition *condition
) {
    switch (opcode) {
        case STUDY_IR_EQUAL:
            *condition = STUDY_X86_CONDITION_EQUAL;
            return true;
        case STUDY_IR_NOT_EQUAL:
            *condition = STUDY_X86_CONDITION_NOT_EQUAL;
            return true;
        case STUDY_IR_LESS:
            *condition = STUDY_X86_CONDITION_LESS;
            return true;
        case STUDY_IR_LESS_EQUAL:
            *condition = STUDY_X86_CONDITION_LESS_EQUAL;
            return true;
        case STUDY_IR_GREATER:
            *condition = STUDY_X86_CONDITION_GREATER;
            return true;
        case STUDY_IR_GREATER_EQUAL:
            *condition = STUDY_X86_CONDITION_GREATER_EQUAL;
            return true;
        default:
            return false;
    }
}

static bool study_x86_lower_ir_instruction(
    StudyX86Builder *builder,
    const StudyIrInstruction *instruction
) {
    static const StudyX86Register argument_registers[] = {
        STUDY_X86_REGISTER_RDI,
        STUDY_X86_REGISTER_RSI,
        STUDY_X86_REGISTER_RDX,
        STUDY_X86_REGISTER_RCX,
        STUDY_X86_REGISTER_R8,
        STUDY_X86_REGISTER_R9
    };
    StudyX86Operand first;
    StudyX86Operand second;
    StudyX86Condition condition;
    size_t index;

    switch (instruction->opcode) {
        case STUDY_IR_CONST_I32:
            return study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       study_x86_unsigned(
                           instruction->as.integer,
                           STUDY_X86_WIDTH_32
                       ),
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_CONST_BOOL:
            return study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       study_x86_unsigned(
                           instruction->as.boolean ? 1U : 0U,
                           STUDY_X86_WIDTH_32
                       ),
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_LOAD:
            first = study_x86_slot(builder, instruction->as.load.slot);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_STORE:
            first = study_x86_value(builder, instruction->as.store.value);
            second = study_x86_slot(builder, instruction->as.store.slot);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   second.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       second,
                       instruction->range
                   );
        case STUDY_IR_ADD:
        case STUDY_IR_SUBTRACT:
        case STUDY_IR_MULTIPLY:
            first = study_x86_value(builder, instruction->as.binary.left);
            second = study_x86_value(builder, instruction->as.binary.right);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   second.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_binary(
                       builder,
                       instruction->opcode == STUDY_IR_ADD
                           ? STUDY_X86_ADD
                           : instruction->opcode == STUDY_IR_SUBTRACT
                                 ? STUDY_X86_SUBTRACT
                                 : STUDY_X86_MULTIPLY,
                       second,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_NEGATE:
            first = study_x86_value(builder, instruction->as.unary.operand);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_unary(
                       builder,
                       STUDY_X86_NEGATE,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_NOT:
            first = study_x86_value(builder, instruction->as.unary.operand);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_COMPARE,
                       study_x86_unsigned(0U, STUDY_X86_WIDTH_32),
                       first,
                       instruction->range
                   ) &&
                   study_x86_emit_set(
                       builder,
                       STUDY_X86_CONDITION_EQUAL,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_8
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOVZX,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_8
                       ),
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_EQUAL:
        case STUDY_IR_NOT_EQUAL:
        case STUDY_IR_LESS:
        case STUDY_IR_LESS_EQUAL:
        case STUDY_IR_GREATER:
        case STUDY_IR_GREATER_EQUAL:
            first = study_x86_value(builder, instruction->as.binary.left);
            second = study_x86_value(builder, instruction->as.binary.right);
            if (first.kind == STUDY_X86_OPERAND_NONE ||
                second.kind == STUDY_X86_OPERAND_NONE ||
                !study_x86_condition_for_ir(instruction->opcode, &condition)) {
                return false;
            }
            return study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_COMPARE,
                       second,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_set(
                       builder,
                       condition,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_8
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOVZX,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_8
                       ),
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_CALL:
            for (index = 0U; index < instruction->as.call.argument_count; index++) {
                first = study_x86_value(
                    builder,
                    instruction->as.call.arguments[index]
                );
                if (first.kind == STUDY_X86_OPERAND_NONE ||
                    !study_x86_emit_binary(
                        builder,
                        STUDY_X86_MOV,
                        first,
                        study_x86_register(
                            argument_registers[index],
                            STUDY_X86_WIDTH_32
                        ),
                        instruction->range
                    )) {
                    return false;
                }
            }
            return study_x86_emit_call(
                       builder,
                       builder->ir_functions[instruction->as.call.function],
                       instruction->range
                   ) &&
                   study_x86_store_eax(
                       builder,
                       instruction->result,
                       instruction->range
                   );
        case STUDY_IR_PRINT:
            first = study_x86_value(builder, instruction->as.print.value);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RDI,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_call(
                       builder,
                       builder->program.print_function,
                       instruction->range
                   );
        case STUDY_IR_BRANCH:
            first = study_x86_value(builder, instruction->as.branch.condition);
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_COMPARE,
                       study_x86_unsigned(0U, STUDY_X86_WIDTH_32),
                       first,
                       instruction->range
                   ) &&
                   study_x86_emit_branch(
                       builder,
                       STUDY_X86_CONDITION_NOT_EQUAL,
                       builder->ir_blocks[instruction->as.branch.then_block],
                       builder->ir_blocks[instruction->as.branch.else_block],
                       instruction->range
                   );
        case STUDY_IR_JUMP:
            return study_x86_emit_jump(
                builder,
                builder->ir_blocks[instruction->as.jump.target],
                instruction->range
            );
        case STUDY_IR_RETURN:
            first = study_x86_value(
                builder,
                instruction->as.return_value.value
            );
            return first.kind != STUDY_X86_OPERAND_NONE &&
                   study_x86_emit_binary(
                       builder,
                       STUDY_X86_MOV,
                       first,
                       study_x86_register(
                           STUDY_X86_REGISTER_RAX,
                           STUDY_X86_WIDTH_32
                       ),
                       instruction->range
                   ) &&
                   study_x86_emit_simple(
                       builder,
                       STUDY_X86_LEAVE,
                       instruction->range
                   ) &&
                   study_x86_emit_simple(
                       builder,
                       STUDY_X86_RETURN,
                       instruction->range
                   );
    }

    study_x86_lower_error(builder, &instruction->range, "unknown typed IR opcode");
    return false;
}

static bool study_x86_lower_start(StudyX86Builder *builder) {
    StudySourceRange no_range = {0};
    StudyX86BlockId block;
    StudyX86FunctionId main_target =
        builder->ir_functions[builder->ir->main_function];

    builder->current_function = builder->program.start_function;
    if (!study_x86_add_block(builder, builder->current_function, &block)) {
        return false;
    }
    builder->current_block = block;
    return study_x86_emit_call(builder, main_target, no_range) &&
           study_x86_emit_binary(
               builder,
               STUDY_X86_MOV,
               study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
               study_x86_register(STUDY_X86_REGISTER_RDI, STUDY_X86_WIDTH_32),
               no_range
           ) &&
           study_x86_emit_binary(
               builder,
               STUDY_X86_MOV,
               study_x86_unsigned(60U, STUDY_X86_WIDTH_32),
               study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
               no_range
           ) &&
           study_x86_emit_simple(builder, STUDY_X86_SYSCALL, no_range) &&
           study_x86_emit_simple(builder, STUDY_X86_UNDEFINED, no_range);
}

static bool study_x86_lower_runtime(StudyX86Builder *builder) {
    StudySourceRange no_range = {0};
    StudyX86BlockId entry;
    StudyX86BlockId negative;
    StudyX86BlockId magnitude;
    StudyX86BlockId zero;
    StudyX86BlockId digit;
    StudyX86BlockId sign;
    StudyX86BlockId add_sign;
    StudyX86BlockId write;
    StudyX86BlockId write_again;
    StudyX86BlockId check_result;
    StudyX86BlockId advance;
    StudyX86BlockId success;
    StudyX86BlockId failure;
    StudyX86Function *function;

    builder->current_function = builder->program.print_function;
    function = &builder->program.functions[builder->current_function];
    function->frame_size = 64U;
    if (!study_x86_add_block(builder, builder->current_function, &entry) ||
        !study_x86_add_block(builder, builder->current_function, &negative) ||
        !study_x86_add_block(builder, builder->current_function, &magnitude) ||
        !study_x86_add_block(builder, builder->current_function, &zero) ||
        !study_x86_add_block(builder, builder->current_function, &digit) ||
        !study_x86_add_block(builder, builder->current_function, &sign) ||
        !study_x86_add_block(builder, builder->current_function, &add_sign) ||
        !study_x86_add_block(builder, builder->current_function, &write) ||
        !study_x86_add_block(builder, builder->current_function, &write_again) ||
        !study_x86_add_block(builder, builder->current_function, &check_result) ||
        !study_x86_add_block(builder, builder->current_function, &advance) ||
        !study_x86_add_block(builder, builder->current_function, &success) ||
        !study_x86_add_block(builder, builder->current_function, &failure)) {
        return false;
    }

    builder->current_block = entry;
    if (!study_x86_emit_unary(
            builder,
            STUDY_X86_PUSH,
            study_x86_register(STUDY_X86_REGISTER_RBP, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_register(STUDY_X86_REGISTER_RSP, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RBP, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_SUBTRACT,
            study_x86_unsigned(64U, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RSP, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_XOR,
            study_x86_register(STUDY_X86_REGISTER_R8, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_R8, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_register(STUDY_X86_REGISTER_RDI, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_TEST,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_NOT_SIGN,
            magnitude,
            negative,
            no_range
        )) {
        return false;
    }

    builder->current_block = negative;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(1U, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_R8, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_unary(
            builder,
            STUDY_X86_NEGATE,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_jump(builder, magnitude, no_range)) {
        return false;
    }

    builder->current_block = magnitude;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_LEA,
            study_x86_memory(
                STUDY_X86_REGISTER_RBP,
                -1,
                STUDY_X86_WIDTH_64
            ),
            study_x86_register(STUDY_X86_REGISTER_RSI, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(10U, STUDY_X86_WIDTH_8),
            study_x86_memory(STUDY_X86_REGISTER_RSI, 0, STUDY_X86_WIDTH_8),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(1U, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_R9, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_TEST,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_NOT_EQUAL,
            digit,
            zero,
            no_range
        )) {
        return false;
    }

    builder->current_block = zero;
    if (!study_x86_emit_unary(
            builder,
            STUDY_X86_DECREMENT,
            study_x86_register(STUDY_X86_REGISTER_RSI, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(48U, STUDY_X86_WIDTH_8),
            study_x86_memory(STUDY_X86_REGISTER_RSI, 0, STUDY_X86_WIDTH_8),
            no_range
        ) ||
        !study_x86_emit_unary(
            builder,
            STUDY_X86_INCREMENT,
            study_x86_register(STUDY_X86_REGISTER_R9, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_jump(builder, sign, no_range)) {
        return false;
    }

    builder->current_block = digit;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_XOR,
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(10U, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RCX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_unary(
            builder,
            STUDY_X86_DIVIDE_UNSIGNED,
            study_x86_register(STUDY_X86_REGISTER_RCX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_ADD,
            study_x86_unsigned(48U, STUDY_X86_WIDTH_8),
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_8),
            no_range
        ) ||
        !study_x86_emit_unary(
            builder,
            STUDY_X86_DECREMENT,
            study_x86_register(STUDY_X86_REGISTER_RSI, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_8),
            study_x86_memory(STUDY_X86_REGISTER_RSI, 0, STUDY_X86_WIDTH_8),
            no_range
        ) ||
        !study_x86_emit_unary(
            builder,
            STUDY_X86_INCREMENT,
            study_x86_register(STUDY_X86_REGISTER_R9, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_TEST,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_NOT_EQUAL,
            digit,
            sign,
            no_range
        )) {
        return false;
    }

    builder->current_block = sign;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_TEST,
            study_x86_register(STUDY_X86_REGISTER_R8, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_R8, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_EQUAL,
            write,
            add_sign,
            no_range
        )) {
        return false;
    }

    builder->current_block = add_sign;
    if (!study_x86_emit_unary(
            builder,
            STUDY_X86_DECREMENT,
            study_x86_register(STUDY_X86_REGISTER_RSI, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(45U, STUDY_X86_WIDTH_8),
            study_x86_memory(STUDY_X86_REGISTER_RSI, 0, STUDY_X86_WIDTH_8),
            no_range
        ) ||
        !study_x86_emit_unary(
            builder,
            STUDY_X86_INCREMENT,
            study_x86_register(STUDY_X86_REGISTER_R9, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_jump(builder, write, no_range)) {
        return false;
    }

    builder->current_block = write;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_register(STUDY_X86_REGISTER_R9, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_jump(builder, write_again, no_range)) {
        return false;
    }

    builder->current_block = write_again;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(1U, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_unsigned(1U, STUDY_X86_WIDTH_32),
            study_x86_register(STUDY_X86_REGISTER_RDI, STUDY_X86_WIDTH_32),
            no_range
        ) ||
        !study_x86_emit_simple(builder, STUDY_X86_SYSCALL, no_range) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_COMPARE,
            study_x86_signed(-4, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_EQUAL,
            write_again,
            check_result,
            no_range
        )) {
        return false;
    }

    builder->current_block = check_result;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_TEST,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_LESS_EQUAL,
            failure,
            advance,
            no_range
        )) {
        return false;
    }

    builder->current_block = advance;
    if (!study_x86_emit_binary(
            builder,
            STUDY_X86_ADD,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RSI, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_SUBTRACT,
            study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_TEST,
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RDX, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_branch(
            builder,
            STUDY_X86_CONDITION_NOT_EQUAL,
            write_again,
            success,
            no_range
        )) {
        return false;
    }

    builder->current_block = success;
    if (!study_x86_emit_simple(builder, STUDY_X86_LEAVE, no_range) ||
        !study_x86_emit_simple(builder, STUDY_X86_RETURN, no_range)) {
        return false;
    }

    builder->current_block = failure;
    return study_x86_emit_binary(
               builder,
               STUDY_X86_MOV,
               study_x86_unsigned(74U, STUDY_X86_WIDTH_32),
               study_x86_register(STUDY_X86_REGISTER_RDI, STUDY_X86_WIDTH_32),
               no_range
           ) &&
           study_x86_emit_binary(
               builder,
               STUDY_X86_MOV,
               study_x86_unsigned(60U, STUDY_X86_WIDTH_32),
               study_x86_register(STUDY_X86_REGISTER_RAX, STUDY_X86_WIDTH_32),
               no_range
           ) &&
           study_x86_emit_simple(builder, STUDY_X86_SYSCALL, no_range) &&
           study_x86_emit_simple(builder, STUDY_X86_UNDEFINED, no_range);
}

static bool study_x86_create_ir_blocks(
    StudyX86Builder *builder,
    StudyIrFunctionId ir_function
) {
    StudyIrBlockId ir_block = builder->ir->functions[ir_function].first_block;
    StudyX86FunctionId target_function = builder->ir_functions[ir_function];

    while (ir_block != STUDY_IR_BLOCK_NONE) {
        if (!study_x86_add_block(
                builder,
                target_function,
                &builder->ir_blocks[ir_block]
            )) {
            return false;
        }
        ir_block = builder->ir->blocks[ir_block].next_block;
    }
    return true;
}

static bool study_x86_lower_ir_function(
    StudyX86Builder *builder,
    StudyIrFunctionId ir_function
) {
    static const StudyX86Register parameter_registers[] = {
        STUDY_X86_REGISTER_RDI,
        STUDY_X86_REGISTER_RSI,
        STUDY_X86_REGISTER_RDX,
        STUDY_X86_REGISTER_RCX,
        STUDY_X86_REGISTER_R8,
        STUDY_X86_REGISTER_R9
    };
    const StudyIrFunction *function = &builder->ir->functions[ir_function];
    const StudyX86Function *target_function;
    StudyIrBlockId ir_block = function->first_block;
    StudySourceRange no_range = {0};
    size_t index;

    builder->current_function = builder->ir_functions[ir_function];
    target_function = &builder->program.functions[builder->current_function];
    builder->current_block = builder->ir_blocks[function->entry_block];
    if (!study_x86_emit_unary(
            builder,
            STUDY_X86_PUSH,
            study_x86_register(STUDY_X86_REGISTER_RBP, STUDY_X86_WIDTH_64),
            no_range
        ) ||
        !study_x86_emit_binary(
            builder,
            STUDY_X86_MOV,
            study_x86_register(STUDY_X86_REGISTER_RSP, STUDY_X86_WIDTH_64),
            study_x86_register(STUDY_X86_REGISTER_RBP, STUDY_X86_WIDTH_64),
            no_range
        )) {
        return false;
    }
    if (target_function->frame_size > 0U &&
        !study_x86_emit_binary(
            builder,
            STUDY_X86_SUBTRACT,
            study_x86_unsigned(
                target_function->frame_size,
                STUDY_X86_WIDTH_64
            ),
            study_x86_register(STUDY_X86_REGISTER_RSP, STUDY_X86_WIDTH_64),
            no_range
        )) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; index++) {
        StudyX86Operand destination =
            study_x86_slot(builder, function->parameter_slots[index]);

        if (destination.kind == STUDY_X86_OPERAND_NONE ||
            !study_x86_emit_binary(
                builder,
                STUDY_X86_MOV,
                study_x86_register(
                    parameter_registers[index],
                    STUDY_X86_WIDTH_32
                ),
                destination,
                no_range
            )) {
            return false;
        }
    }

    while (ir_block != STUDY_IR_BLOCK_NONE) {
        StudyIrInstructionId instruction_id =
            builder->ir->blocks[ir_block].first_instruction;

        builder->current_block = builder->ir_blocks[ir_block];
        while (instruction_id != STUDY_IR_INSTRUCTION_NONE) {
            const StudyIrInstruction *instruction =
                &builder->ir->instructions[instruction_id];

            if (!study_x86_lower_ir_instruction(builder, instruction)) {
                return false;
            }
            instruction_id = instruction->next_instruction;
        }
        ir_block = builder->ir->blocks[ir_block].next_block;
    }
    return true;
}

static bool study_x86_allocate_maps(StudyX86Builder *builder) {
    size_t function_count = builder->ir->function_count > 0U
                                ? builder->ir->function_count
                                : 1U;
    size_t block_count = builder->ir->block_count > 0U
                             ? builder->ir->block_count
                             : 1U;
    size_t slot_count = builder->ir->slot_count > 0U
                            ? builder->ir->slot_count
                            : 1U;
    size_t value_count = builder->ir->value_count > 0U
                             ? builder->ir->value_count
                             : 1U;

    builder->ir_functions = calloc(
        function_count,
        sizeof(*builder->ir_functions)
    );
    builder->ir_blocks = calloc(block_count, sizeof(*builder->ir_blocks));
    builder->slot_displacements = calloc(
        slot_count,
        sizeof(*builder->slot_displacements)
    );
    builder->value_displacements = calloc(
        value_count,
        sizeof(*builder->value_displacements)
    );
    if (builder->ir_functions == NULL || builder->ir_blocks == NULL ||
        builder->slot_displacements == NULL ||
        builder->value_displacements == NULL) {
        study_x86_lower_error(builder, NULL, "not enough memory for lowering maps");
        return false;
    }
    return true;
}

static void study_x86_free_maps(StudyX86Builder *builder) {
    free(builder->ir_functions);
    free(builder->ir_blocks);
    free(builder->slot_displacements);
    free(builder->value_displacements);
    builder->ir_functions = NULL;
    builder->ir_blocks = NULL;
    builder->slot_displacements = NULL;
    builder->value_displacements = NULL;
}

bool study_lower_to_x86_64(
    const StudyIrProgram *ir,
    const StudySource *source,
    StudyX86Program *output,
    StudyDiagnostic *diagnostic
) {
    StudyX86Builder builder;
    StudyX86FunctionId function_id;
    StudyIrFunctionId ir_function;
    bool success = false;

    study_diagnostic_clear(diagnostic);
    if (!study_x86_program_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "x86-64 lowering requires an initialised empty output program"
        );
        return false;
    }
    if (source == NULL || !study_ir_verify(ir, source, diagnostic)) {
        return false;
    }

    builder.ir = ir;
    builder.source = source;
    study_x86_64_program_init(&builder.program);
    builder.ir_functions = NULL;
    builder.ir_blocks = NULL;
    builder.slot_displacements = NULL;
    builder.value_displacements = NULL;
    builder.current_function = STUDY_X86_FUNCTION_NONE;
    builder.current_block = STUDY_X86_BLOCK_NONE;
    builder.diagnostic = diagnostic;

    if (!study_x86_allocate_maps(&builder) ||
        !study_x86_add_function(
            &builder,
            STUDY_X86_FUNCTION_START,
            STUDY_IR_FUNCTION_NONE,
            &builder.program.start_function
        ) ||
        !study_x86_add_function(
            &builder,
            STUDY_X86_FUNCTION_PRINT_I32,
            STUDY_IR_FUNCTION_NONE,
            &builder.program.print_function
        )) {
        goto cleanup;
    }

    for (ir_function = 0U;
         ir_function < ir->function_count;
         ir_function++) {
        if (!study_x86_add_function(
                &builder,
                STUDY_X86_FUNCTION_STUDY,
                ir_function,
                &function_id
            )) {
            goto cleanup;
        }
        builder.ir_functions[ir_function] = function_id;
    }
    for (ir_function = 0U;
         ir_function < ir->function_count;
         ir_function++) {
        if (!study_x86_layout_function(&builder, ir_function)) {
            goto cleanup;
        }
    }
    if (!study_x86_lower_start(&builder) ||
        !study_x86_lower_runtime(&builder)) {
        goto cleanup;
    }
    for (ir_function = 0U;
         ir_function < ir->function_count;
         ir_function++) {
        if (!study_x86_create_ir_blocks(&builder, ir_function)) {
            goto cleanup;
        }
    }
    for (ir_function = 0U;
         ir_function < ir->function_count;
         ir_function++) {
        if (!study_x86_lower_ir_function(&builder, ir_function)) {
            goto cleanup;
        }
    }
    if (!study_x86_64_verify(&builder.program, source, diagnostic)) {
        goto cleanup;
    }

    *output = builder.program;
    study_x86_64_program_init(&builder.program);
    success = true;

cleanup:
    study_x86_free_maps(&builder);
    study_x86_64_program_free(&builder.program);
    return success;
}
