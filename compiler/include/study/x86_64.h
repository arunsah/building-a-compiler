#ifndef STUDY_X86_64_H
#define STUDY_X86_64_H

#include "study/diagnostic.h"
#include "study/ir.h"
#include "study/location.h"
#include "study/source.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef size_t StudyX86FunctionId;
typedef size_t StudyX86BlockId;
typedef size_t StudyX86InstructionId;

#define STUDY_X86_FUNCTION_NONE SIZE_MAX
#define STUDY_X86_BLOCK_NONE SIZE_MAX
#define STUDY_X86_INSTRUCTION_NONE SIZE_MAX

typedef enum StudyX86FunctionKind {
    STUDY_X86_FUNCTION_START = 0,
    STUDY_X86_FUNCTION_PRINT_I32,
    STUDY_X86_FUNCTION_STUDY
} StudyX86FunctionKind;

typedef enum StudyX86StorageKind {
    STUDY_X86_STORAGE_SLOT = 0,
    STUDY_X86_STORAGE_VALUE
} StudyX86StorageKind;

typedef enum StudyX86Width {
    STUDY_X86_WIDTH_8 = 8,
    STUDY_X86_WIDTH_32 = 32,
    STUDY_X86_WIDTH_64 = 64
} StudyX86Width;

typedef enum StudyX86Register {
    STUDY_X86_REGISTER_RAX = 0,
    STUDY_X86_REGISTER_RBP,
    STUDY_X86_REGISTER_RSP,
    STUDY_X86_REGISTER_RDI,
    STUDY_X86_REGISTER_RSI,
    STUDY_X86_REGISTER_RDX,
    STUDY_X86_REGISTER_RCX,
    STUDY_X86_REGISTER_R8,
    STUDY_X86_REGISTER_R9
} StudyX86Register;

typedef enum StudyX86OperandKind {
    STUDY_X86_OPERAND_NONE = 0,
    STUDY_X86_OPERAND_REGISTER,
    STUDY_X86_OPERAND_IMMEDIATE,
    STUDY_X86_OPERAND_MEMORY
} StudyX86OperandKind;

typedef struct StudyX86Operand {
    StudyX86OperandKind kind;
    StudyX86Width width;
    union {
        StudyX86Register register_id;
        struct {
            uint64_t value;
            bool is_signed;
        } immediate;
        struct {
            StudyX86Register base;
            int32_t displacement;
        } memory;
    } as;
} StudyX86Operand;

typedef enum StudyX86Condition {
    STUDY_X86_CONDITION_EQUAL = 0,
    STUDY_X86_CONDITION_NOT_EQUAL,
    STUDY_X86_CONDITION_LESS,
    STUDY_X86_CONDITION_LESS_EQUAL,
    STUDY_X86_CONDITION_GREATER,
    STUDY_X86_CONDITION_GREATER_EQUAL,
    STUDY_X86_CONDITION_NOT_SIGN
} StudyX86Condition;

typedef enum StudyX86Opcode {
    STUDY_X86_MOV = 0,
    STUDY_X86_MOVZX,
    STUDY_X86_LEA,
    STUDY_X86_ADD,
    STUDY_X86_SUBTRACT,
    STUDY_X86_MULTIPLY,
    STUDY_X86_NEGATE,
    STUDY_X86_XOR,
    STUDY_X86_TEST,
    STUDY_X86_COMPARE,
    STUDY_X86_SET_CONDITION,
    STUDY_X86_DIVIDE_UNSIGNED,
    STUDY_X86_INCREMENT,
    STUDY_X86_DECREMENT,
    STUDY_X86_PUSH,
    STUDY_X86_CALL,
    STUDY_X86_JUMP,
    STUDY_X86_BRANCH_CONDITION,
    STUDY_X86_LEAVE,
    STUDY_X86_RETURN,
    STUDY_X86_SYSCALL,
    STUDY_X86_UNDEFINED
} StudyX86Opcode;

typedef struct StudyX86Function {
    StudyX86FunctionKind kind;
    StudyIrFunctionId ir_function;
    size_t frame_size;
    size_t first_storage;
    size_t storage_count;
    StudyX86BlockId entry_block;
    StudyX86BlockId first_block;
    StudyX86BlockId last_block;
} StudyX86Function;

typedef struct StudyX86Block {
    StudyX86FunctionId function;
    StudyX86InstructionId first_instruction;
    StudyX86InstructionId last_instruction;
    StudyX86BlockId next_block;
} StudyX86Block;

typedef struct StudyX86Storage {
    StudyX86FunctionId function;
    StudyX86StorageKind kind;
    size_t ir_id;
    int32_t displacement;
    StudyX86Width width;
} StudyX86Storage;

typedef struct StudyX86Instruction {
    StudyX86Opcode opcode;
    StudyX86FunctionId function;
    StudyX86BlockId block;
    StudySourceRange range;
    StudyX86Operand first;
    StudyX86Operand second;
    StudyX86Condition condition;
    StudyX86FunctionId target_function;
    StudyX86BlockId then_block;
    StudyX86BlockId else_block;
    StudyX86InstructionId next_instruction;
} StudyX86Instruction;

typedef struct StudyX86Program {
    StudyX86Function *functions;
    size_t function_count;
    size_t function_capacity;
    StudyX86Block *blocks;
    size_t block_count;
    size_t block_capacity;
    StudyX86Instruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    StudyX86Storage *storage;
    size_t storage_count;
    size_t storage_capacity;
    StudyX86FunctionId start_function;
    StudyX86FunctionId print_function;
} StudyX86Program;

void study_x86_64_program_init(StudyX86Program *program);
void study_x86_64_program_free(StudyX86Program *program);

const char *study_x86_64_opcode_name(StudyX86Opcode opcode);

bool study_x86_64_program_dump(
    FILE *stream,
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
);

#endif
