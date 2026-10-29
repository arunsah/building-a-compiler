#ifndef STUDY_IR_H
#define STUDY_IR_H

#include "study/location.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef size_t StudyIrFunctionId;
typedef size_t StudyIrBlockId;
typedef size_t StudyIrInstructionId;
typedef size_t StudyIrValueId;
typedef size_t StudyIrSlotId;

#define STUDY_IR_FUNCTION_NONE SIZE_MAX
#define STUDY_IR_BLOCK_NONE SIZE_MAX
#define STUDY_IR_INSTRUCTION_NONE SIZE_MAX
#define STUDY_IR_VALUE_NONE SIZE_MAX
#define STUDY_IR_SLOT_NONE SIZE_MAX

typedef enum StudyIrOpcode {
    STUDY_IR_CONST_I32 = 0,
    STUDY_IR_CONST_BOOL,
    STUDY_IR_LOAD,
    STUDY_IR_STORE,
    STUDY_IR_ADD,
    STUDY_IR_SUBTRACT,
    STUDY_IR_MULTIPLY,
    STUDY_IR_NEGATE,
    STUDY_IR_NOT,
    STUDY_IR_EQUAL,
    STUDY_IR_NOT_EQUAL,
    STUDY_IR_LESS,
    STUDY_IR_LESS_EQUAL,
    STUDY_IR_GREATER,
    STUDY_IR_GREATER_EQUAL,
    STUDY_IR_CALL,
    STUDY_IR_PRINT,
    STUDY_IR_BRANCH,
    STUDY_IR_JUMP,
    STUDY_IR_RETURN
} StudyIrOpcode;

typedef struct StudyIrFunction {
    size_t name_token;
    StudySyntaxType result_type;
    size_t parameter_count;
    StudyIrSlotId parameter_slots[STUDY_MAX_FUNCTION_PARAMETERS];
    StudyIrBlockId entry_block;
    StudyIrBlockId first_block;
    StudyIrBlockId last_block;
} StudyIrFunction;

typedef struct StudyIrBlock {
    StudyIrFunctionId function;
    StudyIrInstructionId first_instruction;
    StudyIrInstructionId last_instruction;
    StudyIrBlockId next_block;
} StudyIrBlock;

typedef struct StudyIrSlot {
    StudyIrFunctionId function;
    size_t name_token;
    StudySyntaxType type;
    bool is_parameter;
    size_t parameter_index;
} StudyIrSlot;

typedef struct StudyIrValue {
    StudyIrFunctionId function;
    StudySyntaxType type;
    StudyIrInstructionId definition;
} StudyIrValue;

typedef struct StudyIrInstruction {
    StudyIrOpcode opcode;
    StudyIrFunctionId function;
    StudyIrBlockId block;
    StudySourceRange range;
    StudyIrValueId result;
    StudyIrInstructionId next_instruction;
    union {
        uint32_t integer;
        bool boolean;
        struct {
            StudyIrSlotId slot;
        } load;
        struct {
            StudyIrSlotId slot;
            StudyIrValueId value;
        } store;
        struct {
            StudyIrValueId left;
            StudyIrValueId right;
        } binary;
        struct {
            StudyIrValueId operand;
        } unary;
        struct {
            StudyIrFunctionId function;
            size_t argument_count;
            StudyIrValueId arguments[STUDY_MAX_FUNCTION_PARAMETERS];
        } call;
        struct {
            StudyIrValueId value;
        } print;
        struct {
            StudyIrValueId condition;
            StudyIrBlockId then_block;
            StudyIrBlockId else_block;
        } branch;
        struct {
            StudyIrBlockId target;
        } jump;
        struct {
            StudyIrValueId value;
        } return_value;
    } as;
} StudyIrInstruction;

typedef struct StudyIrProgram {
    StudyIrFunction *functions;
    size_t function_count;
    size_t function_capacity;
    StudyIrBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    StudyIrInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    StudyIrSlot *slots;
    size_t slot_count;
    size_t slot_capacity;
    StudyIrValue *values;
    size_t value_count;
    size_t value_capacity;
    StudyIrFunctionId main_function;
} StudyIrProgram;

void study_ir_program_init(StudyIrProgram *program);
void study_ir_program_free(StudyIrProgram *program);

const char *study_ir_opcode_name(StudyIrOpcode opcode);

bool study_ir_program_dump(
    FILE *stream,
    const StudyIrProgram *program,
    const StudySource *source,
    const StudyTokenList *tokens
);

#endif
