#include "study/lower.h"

#include "study/ir_verify.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { STUDY_IR_INITIAL_CAPACITY = 32 };

typedef struct StudyIrBuilder {
    const StudySource *source;
    const StudyTokenList *tokens;
    const StudySyntaxTree *tree;
    const StudyCheckedProgram *checked;
    StudyIrProgram program;
    StudyIrFunctionId *declaration_functions;
    StudyIrSlotId *declaration_slots;
    StudyIrFunctionId current_function;
    StudyIrBlockId current_block;
    StudyDiagnostic *diagnostic;
} StudyIrBuilder;

static bool study_ir_lower_expression(
    StudyIrBuilder *builder,
    StudySyntaxNodeId node_id,
    StudyIrValueId *result
);

static bool study_ir_program_is_empty(const StudyIrProgram *program) {
    return program != NULL && program->functions == NULL &&
           program->function_count == 0U && program->function_capacity == 0U &&
           program->blocks == NULL && program->block_count == 0U &&
           program->block_capacity == 0U && program->instructions == NULL &&
           program->instruction_count == 0U &&
           program->instruction_capacity == 0U && program->slots == NULL &&
           program->slot_count == 0U && program->slot_capacity == 0U &&
           program->values == NULL && program->value_count == 0U &&
           program->value_capacity == 0U &&
           program->main_function == STUDY_IR_FUNCTION_NONE;
}

static const StudySyntaxNode *study_ir_builder_node(
    const StudyIrBuilder *builder,
    StudySyntaxNodeId node_id
) {
    return &builder->tree->nodes[node_id];
}

static void study_ir_lower_error(
    StudyIrBuilder *builder,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        builder->diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        builder->source->path,
        "could not lower checked program: %s",
        message
    );
    if (range != NULL) {
        study_diagnostic_set_location(
            builder->diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static void *study_ir_reserve_array(
    void *items,
    size_t *capacity,
    size_t count,
    size_t item_size,
    StudyIrBuilder *builder
) {
    size_t next_capacity;
    void *grown;

    if (count < *capacity) {
        return items;
    }

    if (*capacity == 0U) {
        next_capacity = STUDY_IR_INITIAL_CAPACITY;
    } else {
        if (*capacity > SIZE_MAX / 2U) {
            study_ir_lower_error(builder, NULL, "a representation grew too large");
            return NULL;
        }
        next_capacity = *capacity * 2U;
    }

    if (next_capacity > SIZE_MAX / item_size) {
        study_ir_lower_error(builder, NULL, "a representation grew too large");
        return NULL;
    }

    grown = realloc(items, next_capacity * item_size);
    if (grown == NULL) {
        study_ir_lower_error(builder, NULL, "not enough memory for typed IR");
        return NULL;
    }

    *capacity = next_capacity;
    return grown;
}

static bool study_ir_reserve_function(StudyIrBuilder *builder) {
    void *grown = study_ir_reserve_array(
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

static bool study_ir_reserve_block(StudyIrBuilder *builder) {
    void *grown = study_ir_reserve_array(
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

static bool study_ir_reserve_instruction(StudyIrBuilder *builder) {
    void *grown = study_ir_reserve_array(
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

static bool study_ir_reserve_slot(StudyIrBuilder *builder) {
    void *grown = study_ir_reserve_array(
        builder->program.slots,
        &builder->program.slot_capacity,
        builder->program.slot_count,
        sizeof(*builder->program.slots),
        builder
    );

    if (grown == NULL) {
        return false;
    }
    builder->program.slots = grown;
    return true;
}

static bool study_ir_reserve_value(StudyIrBuilder *builder) {
    void *grown = study_ir_reserve_array(
        builder->program.values,
        &builder->program.value_capacity,
        builder->program.value_count,
        sizeof(*builder->program.values),
        builder
    );

    if (grown == NULL) {
        return false;
    }
    builder->program.values = grown;
    return true;
}

static size_t study_ir_token_length(
    const StudyIrBuilder *builder,
    size_t token_index
) {
    const StudyToken *token = &builder->tokens->items[token_index];

    return token->range.end.byte_offset - token->range.start.byte_offset;
}

static bool study_ir_name_is(
    const StudyIrBuilder *builder,
    size_t token_index,
    const char *text
) {
    size_t length = study_ir_token_length(builder, token_index);

    return length == strlen(text) &&
           memcmp(
               builder->source->bytes +
                   builder->tokens->items[token_index].range.start.byte_offset,
               text,
               length
           ) == 0;
}

static bool study_ir_add_function(
    StudyIrBuilder *builder,
    const StudySyntaxNode *syntax,
    StudyIrFunctionId *function_id
) {
    StudyIrFunction function = {0};
    size_t index;

    if (!study_ir_reserve_function(builder)) {
        return false;
    }

    function.name_token = syntax->as.function.name_token;
    function.result_type = syntax->as.function.result_type;
    function.entry_block = STUDY_IR_BLOCK_NONE;
    function.first_block = STUDY_IR_BLOCK_NONE;
    function.last_block = STUDY_IR_BLOCK_NONE;
    for (index = 0U; index < (size_t)STUDY_MAX_FUNCTION_PARAMETERS; index++) {
        function.parameter_slots[index] = STUDY_IR_SLOT_NONE;
    }

    *function_id = builder->program.function_count;
    builder->program.functions[builder->program.function_count++] = function;
    return true;
}

static bool study_ir_add_block(
    StudyIrBuilder *builder,
    StudyIrBlockId *block_id
) {
    StudyIrBlock block;
    StudyIrFunction *function;

    if (!study_ir_reserve_block(builder)) {
        return false;
    }

    block.function = builder->current_function;
    block.first_instruction = STUDY_IR_INSTRUCTION_NONE;
    block.last_instruction = STUDY_IR_INSTRUCTION_NONE;
    block.next_block = STUDY_IR_BLOCK_NONE;
    *block_id = builder->program.block_count;
    builder->program.blocks[builder->program.block_count++] = block;

    function = &builder->program.functions[builder->current_function];
    if (function->first_block == STUDY_IR_BLOCK_NONE) {
        function->first_block = *block_id;
        function->entry_block = *block_id;
    } else {
        builder->program.blocks[function->last_block].next_block = *block_id;
    }
    function->last_block = *block_id;
    return true;
}

static bool study_ir_add_slot(
    StudyIrBuilder *builder,
    size_t name_token,
    StudySyntaxType type,
    bool is_parameter,
    size_t parameter_index,
    StudyIrSlotId *slot_id
) {
    StudyIrSlot slot;

    if (!study_ir_reserve_slot(builder)) {
        return false;
    }

    slot.function = builder->current_function;
    slot.name_token = name_token;
    slot.type = type;
    slot.is_parameter = is_parameter;
    slot.parameter_index = is_parameter ? parameter_index : SIZE_MAX;
    *slot_id = builder->program.slot_count;
    builder->program.slots[builder->program.slot_count++] = slot;
    return true;
}

static bool study_ir_append_instruction(
    StudyIrBuilder *builder,
    StudyIrInstruction *instruction,
    StudyIrInstructionId *instruction_id
) {
    StudyIrBlock *block;

    if (builder->current_block >= builder->program.block_count ||
        !study_ir_reserve_instruction(builder)) {
        return false;
    }

    block = &builder->program.blocks[builder->current_block];
    if (block->last_instruction != STUDY_IR_INSTRUCTION_NONE &&
        builder->program.instructions[block->last_instruction]
                .next_instruction != STUDY_IR_INSTRUCTION_NONE) {
        study_ir_lower_error(
            builder,
            &instruction->range,
            "the current basic block has an invalid instruction chain"
        );
        return false;
    }

    instruction->function = builder->current_function;
    instruction->block = builder->current_block;
    instruction->next_instruction = STUDY_IR_INSTRUCTION_NONE;
    *instruction_id = builder->program.instruction_count;
    builder->program.instructions[builder->program.instruction_count++] =
        *instruction;

    if (block->first_instruction == STUDY_IR_INSTRUCTION_NONE) {
        block->first_instruction = *instruction_id;
    } else {
        builder->program.instructions[block->last_instruction]
            .next_instruction = *instruction_id;
    }
    block->last_instruction = *instruction_id;
    return true;
}

static bool study_ir_emit_value(
    StudyIrBuilder *builder,
    StudyIrInstruction *instruction,
    StudySyntaxType type,
    StudyIrValueId *value_id
) {
    StudyIrInstructionId instruction_id;
    StudyIrValue value;

    if (!study_ir_reserve_value(builder)) {
        return false;
    }

    *value_id = builder->program.value_count;
    instruction->result = *value_id;
    if (!study_ir_append_instruction(builder, instruction, &instruction_id)) {
        return false;
    }

    value.function = builder->current_function;
    value.type = type;
    value.definition = instruction_id;
    builder->program.values[builder->program.value_count++] = value;
    return true;
}

static bool study_ir_emit_effect(
    StudyIrBuilder *builder,
    StudyIrInstruction *instruction
) {
    StudyIrInstructionId instruction_id;

    instruction->result = STUDY_IR_VALUE_NONE;
    return study_ir_append_instruction(builder, instruction, &instruction_id);
}

static StudyIrInstruction study_ir_instruction(
    StudyIrOpcode opcode,
    StudySourceRange range
) {
    StudyIrInstruction instruction = {0};

    instruction.opcode = opcode;
    instruction.range = range;
    instruction.result = STUDY_IR_VALUE_NONE;
    instruction.next_instruction = STUDY_IR_INSTRUCTION_NONE;
    return instruction;
}

static bool study_ir_emit_jump(
    StudyIrBuilder *builder,
    StudyIrBlockId target,
    StudySourceRange range
) {
    StudyIrInstruction instruction = study_ir_instruction(STUDY_IR_JUMP, range);

    instruction.as.jump.target = target;
    return study_ir_emit_effect(builder, &instruction);
}

static bool study_ir_emit_branch(
    StudyIrBuilder *builder,
    StudyIrValueId condition,
    StudyIrBlockId then_block,
    StudyIrBlockId else_block,
    StudySourceRange range
) {
    StudyIrInstruction instruction =
        study_ir_instruction(STUDY_IR_BRANCH, range);

    instruction.as.branch.condition = condition;
    instruction.as.branch.then_block = then_block;
    instruction.as.branch.else_block = else_block;
    return study_ir_emit_effect(builder, &instruction);
}

static bool study_ir_binary_opcode(
    StudyTokenKind operator_kind,
    StudyIrOpcode *opcode
) {
    switch (operator_kind) {
        case STUDY_TOKEN_PLUS:
            *opcode = STUDY_IR_ADD;
            return true;
        case STUDY_TOKEN_MINUS:
            *opcode = STUDY_IR_SUBTRACT;
            return true;
        case STUDY_TOKEN_STAR:
            *opcode = STUDY_IR_MULTIPLY;
            return true;
        case STUDY_TOKEN_EQUAL_EQUAL:
            *opcode = STUDY_IR_EQUAL;
            return true;
        case STUDY_TOKEN_BANG_EQUAL:
            *opcode = STUDY_IR_NOT_EQUAL;
            return true;
        case STUDY_TOKEN_LESS:
            *opcode = STUDY_IR_LESS;
            return true;
        case STUDY_TOKEN_LESS_EQUAL:
            *opcode = STUDY_IR_LESS_EQUAL;
            return true;
        case STUDY_TOKEN_GREATER:
            *opcode = STUDY_IR_GREATER;
            return true;
        case STUDY_TOKEN_GREATER_EQUAL:
            *opcode = STUDY_IR_GREATER_EQUAL;
            return true;
        default:
            return false;
    }
}

static bool study_ir_lower_call(
    StudyIrBuilder *builder,
    StudySyntaxNodeId node_id,
    StudyIrValueId *result
) {
    const StudySyntaxNode *node = study_ir_builder_node(builder, node_id);
    const StudyCheckedNode *checked = &builder->checked->nodes[node_id];
    StudyIrInstruction instruction =
        study_ir_instruction(STUDY_IR_CALL, node->range);
    StudySyntaxNodeId argument = node->first_child;

    if (checked->declaration >= builder->checked->declaration_count ||
        builder->declaration_functions[checked->declaration] ==
            STUDY_IR_FUNCTION_NONE) {
        study_ir_lower_error(builder, &node->range, "a call is unresolved");
        return false;
    }

    instruction.as.call.function =
        builder->declaration_functions[checked->declaration];
    while (argument != STUDY_SYNTAX_NODE_NONE) {
        if (instruction.as.call.argument_count >=
                (size_t)STUDY_MAX_FUNCTION_PARAMETERS ||
            !study_ir_lower_expression(
                builder,
                argument,
                &instruction.as.call.arguments[
                    instruction.as.call.argument_count
                ]
            )) {
            return false;
        }
        instruction.as.call.argument_count += 1U;
        argument = study_ir_builder_node(builder, argument)->next_sibling;
    }

    return study_ir_emit_value(builder, &instruction, checked->type, result);
}

static bool study_ir_lower_expression(
    StudyIrBuilder *builder,
    StudySyntaxNodeId node_id,
    StudyIrValueId *result
) {
    const StudySyntaxNode *node = study_ir_builder_node(builder, node_id);
    const StudyCheckedNode *checked = &builder->checked->nodes[node_id];
    StudyIrInstruction instruction;
    StudyIrOpcode binary_opcode;
    StudySyntaxNodeId left;
    StudySyntaxNodeId right;
    StudyIrValueId left_value;
    StudyIrValueId right_value;

    if (!checked->has_type) {
        study_ir_lower_error(
            builder,
            &node->range,
            "an expression has no checked type"
        );
        return false;
    }

    switch (node->kind) {
        case STUDY_SYNTAX_INTEGER_EXPRESSION:
            instruction = study_ir_instruction(STUDY_IR_CONST_I32, node->range);
            instruction.as.integer = node->as.integer.value;
            return study_ir_emit_value(
                builder,
                &instruction,
                STUDY_SYNTAX_TYPE_I32,
                result
            );
        case STUDY_SYNTAX_BOOLEAN_EXPRESSION:
            instruction = study_ir_instruction(STUDY_IR_CONST_BOOL, node->range);
            instruction.as.boolean = node->as.boolean.value;
            return study_ir_emit_value(
                builder,
                &instruction,
                STUDY_SYNTAX_TYPE_BOOL,
                result
            );
        case STUDY_SYNTAX_NAME_EXPRESSION:
            if (checked->declaration >= builder->checked->declaration_count ||
                builder->declaration_slots[checked->declaration] ==
                    STUDY_IR_SLOT_NONE) {
                study_ir_lower_error(builder, &node->range, "a name is unresolved");
                return false;
            }
            instruction = study_ir_instruction(STUDY_IR_LOAD, node->range);
            instruction.as.load.slot =
                builder->declaration_slots[checked->declaration];
            return study_ir_emit_value(
                builder,
                &instruction,
                checked->type,
                result
            );
        case STUDY_SYNTAX_CALL_EXPRESSION:
            return study_ir_lower_call(builder, node_id, result);
        case STUDY_SYNTAX_GROUP_EXPRESSION:
            if (node->first_child == STUDY_SYNTAX_NODE_NONE) {
                study_ir_lower_error(builder, &node->range, "a group is empty");
                return false;
            }
            return study_ir_lower_expression(
                builder,
                node->first_child,
                result
            );
        case STUDY_SYNTAX_UNARY_EXPRESSION:
            left = node->first_child;
            if (left == STUDY_SYNTAX_NODE_NONE ||
                !study_ir_lower_expression(builder, left, &left_value)) {
                return false;
            }
            instruction = study_ir_instruction(
                node->as.operation.operator_kind == STUDY_TOKEN_MINUS
                    ? STUDY_IR_NEGATE
                    : STUDY_IR_NOT,
                node->range
            );
            instruction.as.unary.operand = left_value;
            return study_ir_emit_value(
                builder,
                &instruction,
                checked->type,
                result
            );
        case STUDY_SYNTAX_BINARY_EXPRESSION:
            left = node->first_child;
            right = left != STUDY_SYNTAX_NODE_NONE
                        ? study_ir_builder_node(builder, left)->next_sibling
                        : STUDY_SYNTAX_NODE_NONE;
            if (left == STUDY_SYNTAX_NODE_NONE ||
                right == STUDY_SYNTAX_NODE_NONE ||
                !study_ir_lower_expression(builder, left, &left_value) ||
                !study_ir_lower_expression(builder, right, &right_value)) {
                return false;
            }
            if (!study_ir_binary_opcode(
                    node->as.operation.operator_kind,
                    &binary_opcode
                )) {
                study_ir_lower_error(
                    builder,
                    &node->range,
                    "a binary operator is unsupported"
                );
                return false;
            }
            instruction = study_ir_instruction(binary_opcode, node->range);
            instruction.as.binary.left = left_value;
            instruction.as.binary.right = right_value;
            return study_ir_emit_value(
                builder,
                &instruction,
                checked->type,
                result
            );
        default:
            study_ir_lower_error(
                builder,
                &node->range,
                "a non-expression reached expression lowering"
            );
            return false;
    }
}

static bool study_ir_lower_block_contents(
    StudyIrBuilder *builder,
    StudySyntaxNodeId block_id,
    bool *path_live
);

static bool study_ir_lower_if(
    StudyIrBuilder *builder,
    StudySyntaxNodeId node_id,
    bool *path_live
) {
    const StudySyntaxNode *node = study_ir_builder_node(builder, node_id);
    StudySyntaxNodeId condition_node = node->first_child;
    StudySyntaxNodeId then_node;
    StudySyntaxNodeId else_node;
    StudyIrValueId condition;
    StudyIrBlockId then_block;
    StudyIrBlockId else_block;
    StudyIrBlockId continuation = STUDY_IR_BLOCK_NONE;
    StudyIrBlockId then_end;
    StudyIrBlockId else_end;
    bool then_live;
    bool else_live;

    if (condition_node == STUDY_SYNTAX_NODE_NONE) {
        study_ir_lower_error(builder, &node->range, "an if condition is missing");
        return false;
    }
    then_node = study_ir_builder_node(builder, condition_node)->next_sibling;
    else_node = then_node != STUDY_SYNTAX_NODE_NONE
                    ? study_ir_builder_node(builder, then_node)->next_sibling
                    : STUDY_SYNTAX_NODE_NONE;
    if (then_node == STUDY_SYNTAX_NODE_NONE ||
        !study_ir_lower_expression(builder, condition_node, &condition) ||
        !study_ir_add_block(builder, &then_block)) {
        return false;
    }

    if (else_node == STUDY_SYNTAX_NODE_NONE) {
        if (!study_ir_add_block(builder, &continuation) ||
            !study_ir_emit_branch(
                builder,
                condition,
                then_block,
                continuation,
                node->range
            )) {
            return false;
        }
        builder->current_block = then_block;
        then_live = true;
        if (!study_ir_lower_block_contents(builder, then_node, &then_live)) {
            return false;
        }
        if (then_live && !study_ir_emit_jump(builder, continuation, node->range)) {
            return false;
        }
        builder->current_block = continuation;
        *path_live = true;
        return true;
    }

    if (!study_ir_add_block(builder, &else_block) ||
        !study_ir_emit_branch(
            builder,
            condition,
            then_block,
            else_block,
            node->range
        )) {
        return false;
    }

    builder->current_block = then_block;
    then_live = true;
    if (!study_ir_lower_block_contents(builder, then_node, &then_live)) {
        return false;
    }
    then_end = builder->current_block;

    builder->current_block = else_block;
    else_live = true;
    if (!study_ir_lower_block_contents(builder, else_node, &else_live)) {
        return false;
    }
    else_end = builder->current_block;

    if (!then_live && !else_live) {
        *path_live = false;
        return true;
    }

    if (!study_ir_add_block(builder, &continuation)) {
        return false;
    }
    if (then_live) {
        builder->current_block = then_end;
        if (!study_ir_emit_jump(builder, continuation, node->range)) {
            return false;
        }
    }
    if (else_live) {
        builder->current_block = else_end;
        if (!study_ir_emit_jump(builder, continuation, node->range)) {
            return false;
        }
    }
    builder->current_block = continuation;
    *path_live = true;
    return true;
}

static bool study_ir_lower_while(
    StudyIrBuilder *builder,
    StudySyntaxNodeId node_id,
    bool *path_live
) {
    const StudySyntaxNode *node = study_ir_builder_node(builder, node_id);
    StudySyntaxNodeId condition_node = node->first_child;
    StudySyntaxNodeId body_node;
    StudyIrBlockId condition_block;
    StudyIrBlockId body_block;
    StudyIrBlockId after_block;
    StudyIrValueId condition;
    bool body_live = true;

    if (condition_node == STUDY_SYNTAX_NODE_NONE) {
        study_ir_lower_error(builder, &node->range, "a while condition is missing");
        return false;
    }
    body_node = study_ir_builder_node(builder, condition_node)->next_sibling;
    if (body_node == STUDY_SYNTAX_NODE_NONE ||
        !study_ir_add_block(builder, &condition_block) ||
        !study_ir_add_block(builder, &body_block) ||
        !study_ir_add_block(builder, &after_block) ||
        !study_ir_emit_jump(builder, condition_block, node->range)) {
        return false;
    }

    builder->current_block = condition_block;
    if (!study_ir_lower_expression(builder, condition_node, &condition) ||
        !study_ir_emit_branch(
            builder,
            condition,
            body_block,
            after_block,
            node->range
        )) {
        return false;
    }

    builder->current_block = body_block;
    if (!study_ir_lower_block_contents(builder, body_node, &body_live)) {
        return false;
    }
    if (body_live && !study_ir_emit_jump(builder, condition_block, node->range)) {
        return false;
    }

    builder->current_block = after_block;
    *path_live = true;
    return true;
}

static bool study_ir_lower_statement(
    StudyIrBuilder *builder,
    StudySyntaxNodeId node_id,
    bool *path_live
) {
    const StudySyntaxNode *node = study_ir_builder_node(builder, node_id);
    const StudyCheckedNode *checked = &builder->checked->nodes[node_id];
    StudySyntaxNodeId child;
    StudyIrValueId value;
    StudyIrSlotId slot_id;
    StudyIrInstruction instruction;

    switch (node->kind) {
        case STUDY_SYNTAX_BLOCK:
            return study_ir_lower_block_contents(builder, node_id, path_live);
        case STUDY_SYNTAX_VAR_STATEMENT:
            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_ir_lower_expression(builder, child, &value) ||
                checked->declaration >= builder->checked->declaration_count ||
                !study_ir_add_slot(
                    builder,
                    node->as.binding.name_token,
                    node->as.binding.type,
                    false,
                    SIZE_MAX,
                    &slot_id
                )) {
                return false;
            }
            builder->declaration_slots[checked->declaration] = slot_id;
            instruction = study_ir_instruction(STUDY_IR_STORE, node->range);
            instruction.as.store.slot = slot_id;
            instruction.as.store.value = value;
            return study_ir_emit_effect(builder, &instruction);
        case STUDY_SYNTAX_ASSIGNMENT_STATEMENT:
            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_ir_lower_expression(builder, child, &value) ||
                checked->declaration >= builder->checked->declaration_count ||
                builder->declaration_slots[checked->declaration] ==
                    STUDY_IR_SLOT_NONE) {
                study_ir_lower_error(
                    builder,
                    &node->range,
                    "an assignment target has no slot"
                );
                return false;
            }
            instruction = study_ir_instruction(STUDY_IR_STORE, node->range);
            instruction.as.store.slot =
                builder->declaration_slots[checked->declaration];
            instruction.as.store.value = value;
            return study_ir_emit_effect(builder, &instruction);
        case STUDY_SYNTAX_IF_STATEMENT:
            return study_ir_lower_if(builder, node_id, path_live);
        case STUDY_SYNTAX_WHILE_STATEMENT:
            return study_ir_lower_while(builder, node_id, path_live);
        case STUDY_SYNTAX_RETURN_STATEMENT:
            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_ir_lower_expression(builder, child, &value)) {
                return false;
            }
            instruction = study_ir_instruction(STUDY_IR_RETURN, node->range);
            instruction.as.return_value.value = value;
            if (!study_ir_emit_effect(builder, &instruction)) {
                return false;
            }
            *path_live = false;
            return true;
        case STUDY_SYNTAX_PRINT_STATEMENT:
            child = node->first_child;
            if (child == STUDY_SYNTAX_NODE_NONE ||
                !study_ir_lower_expression(builder, child, &value)) {
                return false;
            }
            instruction = study_ir_instruction(STUDY_IR_PRINT, node->range);
            instruction.as.print.value = value;
            return study_ir_emit_effect(builder, &instruction);
        default:
            study_ir_lower_error(
                builder,
                &node->range,
                "a non-statement reached statement lowering"
            );
            return false;
    }
}

static bool study_ir_lower_block_contents(
    StudyIrBuilder *builder,
    StudySyntaxNodeId block_id,
    bool *path_live
) {
    const StudySyntaxNode *block = study_ir_builder_node(builder, block_id);
    StudySyntaxNodeId statement = block->first_child;

    if (block->kind != STUDY_SYNTAX_BLOCK) {
        study_ir_lower_error(builder, &block->range, "expected a block");
        return false;
    }

    while (statement != STUDY_SYNTAX_NODE_NONE && *path_live) {
        if (!study_ir_lower_statement(builder, statement, path_live)) {
            return false;
        }
        statement = study_ir_builder_node(builder, statement)->next_sibling;
    }

    return true;
}

static bool study_ir_collect_functions(StudyIrBuilder *builder) {
    StudySyntaxNodeId function_node =
        study_ir_builder_node(builder, builder->tree->root)->first_child;

    while (function_node != STUDY_SYNTAX_NODE_NONE) {
        const StudySyntaxNode *syntax =
            study_ir_builder_node(builder, function_node);
        StudyDeclarationId declaration =
            builder->checked->nodes[function_node].declaration;
        StudyIrFunctionId function_id;

        if (syntax->kind != STUDY_SYNTAX_FUNCTION ||
            declaration >= builder->checked->declaration_count ||
            !study_ir_add_function(builder, syntax, &function_id)) {
            study_ir_lower_error(
                builder,
                &syntax->range,
                "a function declaration is invalid"
            );
            return false;
        }
        builder->declaration_functions[declaration] = function_id;
        if (study_ir_name_is(builder, syntax->as.function.name_token, "main")) {
            builder->program.main_function = function_id;
        }
        function_node = syntax->next_sibling;
    }

    if (builder->program.main_function == STUDY_IR_FUNCTION_NONE) {
        study_ir_lower_error(builder, NULL, "the checked program has no main");
        return false;
    }
    return true;
}

static bool study_ir_lower_function(
    StudyIrBuilder *builder,
    StudySyntaxNodeId function_node
) {
    const StudySyntaxNode *function =
        study_ir_builder_node(builder, function_node);
    StudyDeclarationId function_declaration =
        builder->checked->nodes[function_node].declaration;
    StudySyntaxNodeId child = function->first_child;
    StudyIrBlockId entry;
    bool path_live = true;

    builder->current_function =
        builder->declaration_functions[function_declaration];
    if (builder->current_function == STUDY_IR_FUNCTION_NONE ||
        !study_ir_add_block(builder, &entry)) {
        return false;
    }
    builder->current_block = entry;

    while (child != STUDY_SYNTAX_NODE_NONE &&
           study_ir_builder_node(builder, child)->kind ==
               STUDY_SYNTAX_PARAMETER) {
        const StudySyntaxNode *parameter = study_ir_builder_node(builder, child);
        StudyDeclarationId declaration =
            builder->checked->nodes[child].declaration;
        StudyIrFunction *ir_function =
            &builder->program.functions[builder->current_function];
        StudyIrSlotId slot;
        size_t parameter_index = ir_function->parameter_count;

        if (declaration >= builder->checked->declaration_count ||
            parameter_index >= (size_t)STUDY_MAX_FUNCTION_PARAMETERS ||
            !study_ir_add_slot(
                builder,
                parameter->as.binding.name_token,
                parameter->as.binding.type,
                true,
                parameter_index,
                &slot
            )) {
            return false;
        }
        builder->declaration_slots[declaration] = slot;
        ir_function->parameter_slots[parameter_index] = slot;
        ir_function->parameter_count += 1U;
        child = parameter->next_sibling;
    }

    if (child == STUDY_SYNTAX_NODE_NONE ||
        !study_ir_lower_block_contents(builder, child, &path_live)) {
        return false;
    }
    if (path_live) {
        study_ir_lower_error(
            builder,
            &function->range,
            "a checked function can still reach its end"
        );
        return false;
    }

    return true;
}

static bool study_ir_lower_functions(StudyIrBuilder *builder) {
    StudySyntaxNodeId function_node =
        study_ir_builder_node(builder, builder->tree->root)->first_child;

    while (function_node != STUDY_SYNTAX_NODE_NONE) {
        if (!study_ir_lower_function(builder, function_node)) {
            return false;
        }
        function_node =
            study_ir_builder_node(builder, function_node)->next_sibling;
    }

    return true;
}

bool study_lower_to_ir(
    const StudySource *source,
    const StudyTokenList *tokens,
    const StudySyntaxTree *tree,
    const StudyCheckedProgram *checked,
    StudyIrProgram *output,
    StudyDiagnostic *diagnostic
) {
    StudyIrBuilder builder;
    size_t index;
    bool success = false;

    study_diagnostic_clear(diagnostic);
    if (!study_ir_program_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "typed IR output must be initialized and empty"
        );
        return false;
    }

    if (source == NULL || source->bytes == NULL || source->path == NULL ||
        tokens == NULL || tokens->items == NULL || tree == NULL ||
        tree->nodes == NULL || tree->root >= tree->count || checked == NULL ||
        checked->nodes == NULL || checked->node_count != tree->count ||
        checked->declarations == NULL || checked->declaration_count == 0U) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "typed IR input is invalid"
        );
        return false;
    }

    builder.source = source;
    builder.tokens = tokens;
    builder.tree = tree;
    builder.checked = checked;
    study_ir_program_init(&builder.program);
    builder.declaration_functions = malloc(
        checked->declaration_count * sizeof(*builder.declaration_functions)
    );
    builder.declaration_slots = malloc(
        checked->declaration_count * sizeof(*builder.declaration_slots)
    );
    builder.current_function = STUDY_IR_FUNCTION_NONE;
    builder.current_block = STUDY_IR_BLOCK_NONE;
    builder.diagnostic = diagnostic;

    if (builder.declaration_functions == NULL ||
        builder.declaration_slots == NULL) {
        study_ir_lower_error(&builder, NULL, "not enough memory for name maps");
        goto cleanup;
    }
    for (index = 0U; index < checked->declaration_count; index++) {
        builder.declaration_functions[index] = STUDY_IR_FUNCTION_NONE;
        builder.declaration_slots[index] = STUDY_IR_SLOT_NONE;
    }

    if (!study_ir_collect_functions(&builder) ||
        !study_ir_lower_functions(&builder) ||
        !study_ir_verify(&builder.program, source, diagnostic)) {
        goto cleanup;
    }

    *output = builder.program;
    study_ir_program_init(&builder.program);
    success = true;

cleanup:
    free(builder.declaration_functions);
    free(builder.declaration_slots);
    study_ir_program_free(&builder.program);
    return success;
}
