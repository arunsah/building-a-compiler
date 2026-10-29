#include "study/x86_64_assembly.h"

#include "study/x86_64_verify.h"

#include <inttypes.h>
#include <stdarg.h>

typedef struct StudyX86AssemblyEmitter {
    FILE *stream;
    const StudyX86Program *program;
    const StudySource *source;
    StudyDiagnostic *diagnostic;
} StudyX86AssemblyEmitter;

static void study_x86_assembly_error(
    StudyX86AssemblyEmitter *emitter,
    const char *message
) {
    study_diagnostic_set(
        emitter->diagnostic,
        STUDY_DIAGNOSTIC_IO,
        emitter->source != NULL ? emitter->source->path : NULL,
        "x86-64 assembly rendering: %s",
        message
    );
}

static bool study_x86_assembly_write(
    StudyX86AssemblyEmitter *emitter,
    const char *format,
    ...
) STUDY_PRINTF_FORMAT(2, 3);

static bool study_x86_assembly_write(
    StudyX86AssemblyEmitter *emitter,
    const char *format,
    ...
) {
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vfprintf(emitter->stream, format, arguments);
    va_end(arguments);
    if (written < 0) {
        study_x86_assembly_error(emitter, "could not write assembly text");
        return false;
    }
    return true;
}

static const char *study_x86_assembly_register_name(
    StudyX86Register register_id,
    StudyX86Width width
) {
    static const char *const names_8[] = {
        "%al", "%bpl", "%spl", "%dil", "%sil", "%dl", "%cl", "%r8b",
        "%r9b"
    };
    static const char *const names_32[] = {
        "%eax", "%ebp", "%esp", "%edi", "%esi", "%edx", "%ecx",
        "%r8d", "%r9d"
    };
    static const char *const names_64[] = {
        "%rax", "%rbp", "%rsp", "%rdi", "%rsi", "%rdx", "%rcx",
        "%r8", "%r9"
    };

    if ((size_t)register_id >= sizeof(names_8) / sizeof(names_8[0])) {
        return NULL;
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
    return NULL;
}

static const char *study_x86_assembly_suffix(StudyX86Width width) {
    switch (width) {
        case STUDY_X86_WIDTH_8:
            return "b";
        case STUDY_X86_WIDTH_32:
            return "l";
        case STUDY_X86_WIDTH_64:
            return "q";
    }

    return NULL;
}

static const char *study_x86_assembly_condition(
    StudyX86Condition condition
) {
    switch (condition) {
        case STUDY_X86_CONDITION_EQUAL:
            return "e";
        case STUDY_X86_CONDITION_NOT_EQUAL:
            return "ne";
        case STUDY_X86_CONDITION_LESS:
            return "l";
        case STUDY_X86_CONDITION_LESS_EQUAL:
            return "le";
        case STUDY_X86_CONDITION_GREATER:
            return "g";
        case STUDY_X86_CONDITION_GREATER_EQUAL:
            return "ge";
        case STUDY_X86_CONDITION_NOT_SIGN:
            return "ns";
    }

    return NULL;
}

static bool study_x86_assembly_function_name(
    StudyX86AssemblyEmitter *emitter,
    StudyX86FunctionId function_id
) {
    const StudyX86Function *function = &emitter->program->functions[function_id];

    switch (function->kind) {
        case STUDY_X86_FUNCTION_START:
            return study_x86_assembly_write(emitter, "_start");
        case STUDY_X86_FUNCTION_PRINT_I32:
            return study_x86_assembly_write(emitter, "study_rt_print_i32");
        case STUDY_X86_FUNCTION_STUDY:
            return study_x86_assembly_write(
                emitter,
                "study_fn_%zu",
                function->ir_function
            );
    }

    return false;
}

static bool study_x86_assembly_block_name(
    StudyX86AssemblyEmitter *emitter,
    StudyX86BlockId block_id
) {
    return study_x86_assembly_write(emitter, ".Lstudy_x86_b%zu", block_id);
}

static bool study_x86_assembly_operand(
    StudyX86AssemblyEmitter *emitter,
    const StudyX86Operand *operand
) {
    const char *register_name;
    const char *base_name;

    switch (operand->kind) {
        case STUDY_X86_OPERAND_REGISTER:
            register_name = study_x86_assembly_register_name(
                operand->as.register_id,
                operand->width
            );
            return register_name != NULL &&
                   study_x86_assembly_write(emitter, "%s", register_name);
        case STUDY_X86_OPERAND_IMMEDIATE:
            if (operand->as.immediate.is_signed) {
                return study_x86_assembly_write(
                    emitter,
                    "$%" PRId64,
                    (int64_t)operand->as.immediate.value
                );
            }
            if (operand->width == STUDY_X86_WIDTH_32) {
                return study_x86_assembly_write(
                    emitter,
                    "$0x%08" PRIx32,
                    (uint32_t)operand->as.immediate.value
                );
            }
            return study_x86_assembly_write(
                emitter,
                "$0x%" PRIx64,
                operand->as.immediate.value
            );
        case STUDY_X86_OPERAND_MEMORY:
            base_name = study_x86_assembly_register_name(
                operand->as.memory.base,
                STUDY_X86_WIDTH_64
            );
            if (base_name == NULL) {
                return false;
            }
            if (operand->as.memory.displacement == 0) {
                return study_x86_assembly_write(
                    emitter,
                    "(%s)",
                    base_name
                );
            }
            return study_x86_assembly_write(
                emitter,
                "%" PRId32 "(%s)",
                operand->as.memory.displacement,
                base_name
            );
        case STUDY_X86_OPERAND_NONE:
            break;
    }

    return false;
}

static bool study_x86_assembly_binary(
    StudyX86AssemblyEmitter *emitter,
    const char *mnemonic,
    StudyX86Width width,
    const StudyX86Operand *first,
    const StudyX86Operand *second
) {
    const char *suffix = study_x86_assembly_suffix(width);

    return suffix != NULL &&
           study_x86_assembly_write(emitter, "  %s%s ", mnemonic, suffix) &&
           study_x86_assembly_operand(emitter, first) &&
           study_x86_assembly_write(emitter, ", ") &&
           study_x86_assembly_operand(emitter, second) &&
           study_x86_assembly_write(emitter, "\n");
}

static bool study_x86_assembly_unary(
    StudyX86AssemblyEmitter *emitter,
    const char *mnemonic,
    const StudyX86Operand *operand
) {
    const char *suffix = study_x86_assembly_suffix(operand->width);

    return suffix != NULL &&
           study_x86_assembly_write(emitter, "  %s%s ", mnemonic, suffix) &&
           study_x86_assembly_operand(emitter, operand) &&
           study_x86_assembly_write(emitter, "\n");
}

static bool study_x86_assembly_instruction(
    StudyX86AssemblyEmitter *emitter,
    const StudyX86Instruction *instruction
) {
    const char *condition;

    switch (instruction->opcode) {
        case STUDY_X86_MOV:
            return study_x86_assembly_binary(
                emitter,
                "mov",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_MOVZX:
            return study_x86_assembly_write(emitter, "  movzbl ") &&
                   study_x86_assembly_operand(emitter, &instruction->first) &&
                   study_x86_assembly_write(emitter, ", ") &&
                   study_x86_assembly_operand(emitter, &instruction->second) &&
                   study_x86_assembly_write(emitter, "\n");
        case STUDY_X86_LEA:
            return study_x86_assembly_binary(
                emitter,
                "lea",
                STUDY_X86_WIDTH_64,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_ADD:
            return study_x86_assembly_binary(
                emitter,
                "add",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_SUBTRACT:
            return study_x86_assembly_binary(
                emitter,
                "sub",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_MULTIPLY:
            return study_x86_assembly_binary(
                emitter,
                "imul",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_NEGATE:
            return study_x86_assembly_unary(emitter, "neg", &instruction->first);
        case STUDY_X86_XOR:
            return study_x86_assembly_binary(
                emitter,
                "xor",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_TEST:
            return study_x86_assembly_binary(
                emitter,
                "test",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_COMPARE:
            return study_x86_assembly_binary(
                emitter,
                "cmp",
                instruction->second.width,
                &instruction->first,
                &instruction->second
            );
        case STUDY_X86_SET_CONDITION:
            condition = study_x86_assembly_condition(instruction->condition);
            return condition != NULL &&
                   study_x86_assembly_write(emitter, "  set%s ", condition) &&
                   study_x86_assembly_operand(emitter, &instruction->first) &&
                   study_x86_assembly_write(emitter, "\n");
        case STUDY_X86_DIVIDE_UNSIGNED:
            return study_x86_assembly_unary(emitter, "div", &instruction->first);
        case STUDY_X86_INCREMENT:
            return study_x86_assembly_unary(emitter, "inc", &instruction->first);
        case STUDY_X86_DECREMENT:
            return study_x86_assembly_unary(emitter, "dec", &instruction->first);
        case STUDY_X86_PUSH:
            return study_x86_assembly_unary(emitter, "push", &instruction->first);
        case STUDY_X86_CALL:
            return study_x86_assembly_write(emitter, "  call ") &&
                   study_x86_assembly_function_name(
                       emitter,
                       instruction->target_function
                   ) &&
                   study_x86_assembly_write(emitter, "\n");
        case STUDY_X86_JUMP:
            return study_x86_assembly_write(emitter, "  jmp ") &&
                   study_x86_assembly_block_name(
                       emitter,
                       instruction->then_block
                   ) &&
                   study_x86_assembly_write(emitter, "\n");
        case STUDY_X86_BRANCH_CONDITION:
            condition = study_x86_assembly_condition(instruction->condition);
            return condition != NULL &&
                   study_x86_assembly_write(emitter, "  j%s ", condition) &&
                   study_x86_assembly_block_name(
                       emitter,
                       instruction->then_block
                   ) &&
                   study_x86_assembly_write(emitter, "\n  jmp ") &&
                   study_x86_assembly_block_name(
                       emitter,
                       instruction->else_block
                   ) &&
                   study_x86_assembly_write(emitter, "\n");
        case STUDY_X86_LEAVE:
            return study_x86_assembly_write(emitter, "  leave\n");
        case STUDY_X86_RETURN:
            return study_x86_assembly_write(emitter, "  ret\n");
        case STUDY_X86_SYSCALL:
            return study_x86_assembly_write(emitter, "  syscall\n");
        case STUDY_X86_UNDEFINED:
            return study_x86_assembly_write(emitter, "  ud2\n");
    }

    return false;
}

static bool study_x86_assembly_function(
    StudyX86AssemblyEmitter *emitter,
    StudyX86FunctionId function_id
) {
    const StudyX86Function *function = &emitter->program->functions[function_id];
    StudyX86BlockId block_id = function->first_block;

    if (function->kind == STUDY_X86_FUNCTION_START &&
        !study_x86_assembly_write(emitter, ".globl _start\n")) {
        return false;
    }
    if (function->kind != STUDY_X86_FUNCTION_START &&
        !study_x86_assembly_write(emitter, "\n.p2align 4\n")) {
        return false;
    }
    if (!study_x86_assembly_write(emitter, ".type ") ||
        !study_x86_assembly_function_name(emitter, function_id) ||
        !study_x86_assembly_write(emitter, ", @function\n") ||
        !study_x86_assembly_function_name(emitter, function_id) ||
        !study_x86_assembly_write(emitter, ":\n")) {
        return false;
    }

    while (block_id != STUDY_X86_BLOCK_NONE) {
        StudyX86InstructionId instruction_id =
            emitter->program->blocks[block_id].first_instruction;

        if (!study_x86_assembly_block_name(emitter, block_id) ||
            !study_x86_assembly_write(emitter, ":\n")) {
            return false;
        }
        while (instruction_id != STUDY_X86_INSTRUCTION_NONE) {
            const StudyX86Instruction *instruction =
                &emitter->program->instructions[instruction_id];

            if (!study_x86_assembly_instruction(emitter, instruction)) {
                return false;
            }
            instruction_id = instruction->next_instruction;
        }
        block_id = emitter->program->blocks[block_id].next_block;
    }

    return study_x86_assembly_write(emitter, ".size ") &&
           study_x86_assembly_function_name(emitter, function_id) &&
           study_x86_assembly_write(emitter, ", .-") &&
           study_x86_assembly_function_name(emitter, function_id) &&
           study_x86_assembly_write(emitter, "\n");
}

bool study_x86_64_emit_assembly(
    FILE *stream,
    const StudyX86Program *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    StudyX86AssemblyEmitter emitter;
    StudyX86FunctionId function_id;

    study_diagnostic_clear(diagnostic);
    if (stream == NULL || source == NULL ||
        !study_x86_64_verify(program, source, diagnostic)) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_diagnostic_set(
                diagnostic,
                STUDY_DIAGNOSTIC_INTERNAL,
                source != NULL ? source->path : NULL,
                "assembly rendering requires an output stream and target program"
            );
        }
        return false;
    }

    emitter.stream = stream;
    emitter.program = program;
    emitter.source = source;
    emitter.diagnostic = diagnostic;
    if (!study_x86_assembly_write(&emitter, ".text\n")) {
        return false;
    }
    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        if (!study_x86_assembly_function(&emitter, function_id)) {
            if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
                study_x86_assembly_error(
                    &emitter,
                    "could not render an instruction"
                );
            }
            return false;
        }
    }
    return study_x86_assembly_write(
        &emitter,
        "\n.section .note.GNU-stack,\"\",@progbits\n"
    );
}
