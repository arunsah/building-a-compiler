#include "study/x86_64_assembly.h"

#include "study/ir_verify.h"

#include <limits.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct StudyX86Frame {
    size_t *slot_offsets;
    size_t *value_offsets;
    size_t size;
} StudyX86Frame;

typedef struct StudyX86Emitter {
    FILE *stream;
    const StudyIrProgram *program;
    const StudySource *source;
    StudyDiagnostic *diagnostic;
} StudyX86Emitter;

static void study_x86_error(
    StudyX86Emitter *emitter,
    const StudySourceRange *range,
    const char *message
) {
    study_diagnostic_set(
        emitter->diagnostic,
        STUDY_DIAGNOSTIC_INTERNAL,
        emitter->source != NULL ? emitter->source->path : NULL,
        "x86-64 assembly emission: %s",
        message
    );
    if (range != NULL) {
        study_diagnostic_set_location(
            emitter->diagnostic,
            range->start.byte_offset,
            range->start.line,
            range->start.column
        );
    }
}

static bool study_x86_write(
    StudyX86Emitter *emitter,
    const char *format,
    ...
) STUDY_PRINTF_FORMAT(2, 3);

static bool study_x86_write(StudyX86Emitter *emitter, const char *format, ...) {
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vfprintf(emitter->stream, format, arguments);
    va_end(arguments);
    if (written < 0) {
        study_x86_error(emitter, NULL, "could not write assembly text");
        return false;
    }
    return true;
}

static void study_x86_frame_free(StudyX86Frame *frame) {
    free(frame->slot_offsets);
    free(frame->value_offsets);
    frame->slot_offsets = NULL;
    frame->value_offsets = NULL;
    frame->size = 0U;
}

static bool study_x86_frame_build(
    StudyX86Emitter *emitter,
    StudyIrFunctionId function_id,
    StudyX86Frame *frame
) {
    const StudyIrProgram *program = emitter->program;
    size_t offset = 0U;
    size_t slot_capacity = program->slot_count > 0U
                               ? program->slot_count
                               : 1U;
    size_t value_capacity = program->value_count > 0U
                                ? program->value_count
                                : 1U;
    size_t index;

    frame->slot_offsets = NULL;
    frame->value_offsets = NULL;
    frame->size = 0U;

    frame->slot_offsets = calloc(
        slot_capacity,
        sizeof(*frame->slot_offsets)
    );
    frame->value_offsets = calloc(
        value_capacity,
        sizeof(*frame->value_offsets)
    );
    if (frame->slot_offsets == NULL || frame->value_offsets == NULL) {
        study_x86_error(emitter, NULL, "not enough memory for a stack layout");
        study_x86_frame_free(frame);
        return false;
    }

    for (index = 0U; index < program->slot_count; index++) {
        if (program->slots[index].function == function_id) {
            if (offset > (size_t)INT32_MAX - 8U) {
                study_x86_error(emitter, NULL, "a stack frame grew too large");
                study_x86_frame_free(frame);
                return false;
            }
            offset += 8U;
            frame->slot_offsets[index] = offset;
        }
    }
    for (index = 0U; index < program->value_count; index++) {
        if (program->values[index].function == function_id) {
            if (offset > (size_t)INT32_MAX - 8U) {
                study_x86_error(emitter, NULL, "a stack frame grew too large");
                study_x86_frame_free(frame);
                return false;
            }
            offset += 8U;
            frame->value_offsets[index] = offset;
        }
    }

    if (offset > (size_t)INT32_MAX - 15U) {
        study_x86_error(emitter, NULL, "a stack frame grew too large");
        study_x86_frame_free(frame);
        return false;
    }
    frame->size = (offset + 15U) & ~(size_t)15U;
    return true;
}

static bool study_x86_value_operand(
    StudyX86Emitter *emitter,
    const StudyX86Frame *frame,
    StudyIrValueId value,
    const char *suffix
) {
    if (value >= emitter->program->value_count ||
        frame->value_offsets[value] == 0U) {
        study_x86_error(emitter, NULL, "a value has no stack location");
        return false;
    }
    return study_x86_write(
        emitter,
        "-%zu(%%rbp)%s",
        frame->value_offsets[value],
        suffix
    );
}

static bool study_x86_slot_operand(
    StudyX86Emitter *emitter,
    const StudyX86Frame *frame,
    StudyIrSlotId slot,
    const char *suffix
) {
    if (slot >= emitter->program->slot_count ||
        frame->slot_offsets[slot] == 0U) {
        study_x86_error(emitter, NULL, "a slot has no stack location");
        return false;
    }
    return study_x86_write(
        emitter,
        "-%zu(%%rbp)%s",
        frame->slot_offsets[slot],
        suffix
    );
}

static bool study_x86_block_label(
    StudyX86Emitter *emitter,
    StudyIrFunctionId function_id,
    StudyIrBlockId block_id,
    const char *suffix
) {
    return study_x86_write(
        emitter,
        ".Lstudy_f%zu_b%zu%s",
        function_id,
        block_id,
        suffix
    );
}

static bool study_x86_store_result(
    StudyX86Emitter *emitter,
    const StudyX86Frame *frame,
    const StudyIrInstruction *instruction
) {
    if (!study_x86_write(emitter, "  movl %%eax, ") ||
        !study_x86_value_operand(
            emitter,
            frame,
            instruction->result,
            "\n"
        )) {
        return false;
    }
    return true;
}

static const char *study_x86_condition_code(StudyIrOpcode opcode) {
    switch (opcode) {
        case STUDY_IR_EQUAL:
            return "e";
        case STUDY_IR_NOT_EQUAL:
            return "ne";
        case STUDY_IR_LESS:
            return "l";
        case STUDY_IR_LESS_EQUAL:
            return "le";
        case STUDY_IR_GREATER:
            return "g";
        case STUDY_IR_GREATER_EQUAL:
            return "ge";
        default:
            return NULL;
    }
}

static bool study_x86_emit_instruction(
    StudyX86Emitter *emitter,
    const StudyX86Frame *frame,
    StudyIrFunctionId function_id,
    const StudyIrInstruction *instruction
) {
    const char *condition_code;
    static const char *argument_registers[STUDY_MAX_FUNCTION_PARAMETERS] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"
    };
    size_t index;

    switch (instruction->opcode) {
        case STUDY_IR_CONST_I32:
            return study_x86_write(
                       emitter,
                       "  movl $0x%08" PRIx32 ", %%eax\n",
                       instruction->as.integer
                   ) &&
                   study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_CONST_BOOL:
            return study_x86_write(
                       emitter,
                       "  movl $%u, %%eax\n",
                       instruction->as.boolean ? 1U : 0U
                   ) &&
                   study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_LOAD:
            return study_x86_write(emitter, "  movl ") &&
                   study_x86_slot_operand(
                       emitter,
                       frame,
                       instruction->as.load.slot,
                       ", %eax\n"
                   ) &&
                   study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_STORE:
            return study_x86_write(emitter, "  movl ") &&
                   study_x86_value_operand(
                       emitter,
                       frame,
                       instruction->as.store.value,
                       ", %eax\n"
                   ) &&
                   study_x86_write(emitter, "  movl %%eax, ") &&
                   study_x86_slot_operand(
                       emitter,
                       frame,
                       instruction->as.store.slot,
                       "\n"
                   );
        case STUDY_IR_ADD:
        case STUDY_IR_SUBTRACT:
        case STUDY_IR_MULTIPLY:
            if (!study_x86_write(emitter, "  movl ") ||
                !study_x86_value_operand(
                    emitter,
                    frame,
                    instruction->as.binary.left,
                    ", %eax\n"
                ) ||
                !study_x86_write(
                    emitter,
                    instruction->opcode == STUDY_IR_ADD
                        ? "  addl "
                        : instruction->opcode == STUDY_IR_SUBTRACT
                            ? "  subl "
                            : "  imull "
                ) ||
                !study_x86_value_operand(
                    emitter,
                    frame,
                    instruction->as.binary.right,
                    ", %eax\n"
                )) {
                return false;
            }
            return study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_NEGATE:
            return study_x86_write(emitter, "  movl ") &&
                   study_x86_value_operand(
                       emitter,
                       frame,
                       instruction->as.unary.operand,
                       ", %eax\n"
                   ) &&
                   study_x86_write(emitter, "  negl %%eax\n") &&
                   study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_NOT:
            return study_x86_write(emitter, "  cmpl $0, ") &&
                   study_x86_value_operand(
                       emitter,
                       frame,
                       instruction->as.unary.operand,
                       "\n"
                   ) &&
                   study_x86_write(
                       emitter,
                       "  sete %%al\n"
                       "  movzbl %%al, %%eax\n"
                   ) &&
                   study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_EQUAL:
        case STUDY_IR_NOT_EQUAL:
        case STUDY_IR_LESS:
        case STUDY_IR_LESS_EQUAL:
        case STUDY_IR_GREATER:
        case STUDY_IR_GREATER_EQUAL:
            condition_code = study_x86_condition_code(instruction->opcode);
            if (condition_code == NULL ||
                !study_x86_write(emitter, "  movl ") ||
                !study_x86_value_operand(
                    emitter,
                    frame,
                    instruction->as.binary.left,
                    ", %eax\n"
                ) ||
                !study_x86_write(emitter, "  cmpl ") ||
                !study_x86_value_operand(
                    emitter,
                    frame,
                    instruction->as.binary.right,
                    ", %eax\n"
                ) ||
                !study_x86_write(
                    emitter,
                    "  set%s %%al\n"
                    "  movzbl %%al, %%eax\n",
                    condition_code
                )) {
                return false;
            }
            return study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_CALL:
            for (index = 0U;
                 index < instruction->as.call.argument_count;
                 index++) {
                if (!study_x86_write(emitter, "  movl ") ||
                    !study_x86_value_operand(
                        emitter,
                        frame,
                        instruction->as.call.arguments[index],
                        ", "
                    ) ||
                    !study_x86_write(
                        emitter,
                        "%s\n",
                        argument_registers[index]
                    )) {
                    return false;
                }
            }
            return study_x86_write(
                       emitter,
                       "  call study_fn_%zu\n",
                       instruction->as.call.function
                   ) &&
                   study_x86_store_result(emitter, frame, instruction);
        case STUDY_IR_PRINT:
            return study_x86_write(emitter, "  movl ") &&
                   study_x86_value_operand(
                       emitter,
                       frame,
                       instruction->as.print.value,
                       ", %edi\n"
                   ) &&
                   study_x86_write(emitter, "  call study_rt_print_i32\n");
        case STUDY_IR_BRANCH:
            if (!study_x86_write(emitter, "  cmpl $0, ") ||
                !study_x86_value_operand(
                    emitter,
                    frame,
                    instruction->as.branch.condition,
                    "\n  jne "
                ) ||
                !study_x86_block_label(
                    emitter,
                    function_id,
                    instruction->as.branch.then_block,
                    "\n  jmp "
                ) ||
                !study_x86_block_label(
                    emitter,
                    function_id,
                    instruction->as.branch.else_block,
                    "\n"
                )) {
                return false;
            }
            return true;
        case STUDY_IR_JUMP:
            return study_x86_write(emitter, "  jmp ") &&
                   study_x86_block_label(
                       emitter,
                       function_id,
                       instruction->as.jump.target,
                       "\n"
                   );
        case STUDY_IR_RETURN:
            return study_x86_write(emitter, "  movl ") &&
                   study_x86_value_operand(
                       emitter,
                       frame,
                       instruction->as.return_value.value,
                       ", %eax\n"
                   ) &&
                   study_x86_write(
                       emitter,
                       "  leave\n"
                       "  ret\n"
                   );
    }

    study_x86_error(emitter, &instruction->range, "unknown IR opcode");
    return false;
}

static bool study_x86_emit_function(
    StudyX86Emitter *emitter,
    StudyIrFunctionId function_id
) {
    const StudyIrProgram *program = emitter->program;
    const StudyIrFunction *function = &program->functions[function_id];
    static const char *parameter_registers[STUDY_MAX_FUNCTION_PARAMETERS] = {
        "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"
    };
    StudyX86Frame frame;
    StudyIrBlockId block_id;
    size_t index;
    bool success = false;

    if (!study_x86_frame_build(emitter, function_id, &frame)) {
        return false;
    }

    if (!study_x86_write(
            emitter,
            "\n.p2align 4\n"
            ".type study_fn_%zu, @function\n"
            "study_fn_%zu:\n"
            "  pushq %%rbp\n"
            "  movq %%rsp, %%rbp\n",
            function_id,
            function_id
        ) ||
        (frame.size > 0U &&
         !study_x86_write(emitter, "  subq $%zu, %%rsp\n", frame.size))) {
        goto cleanup;
    }

    for (index = 0U; index < function->parameter_count; index++) {
        StudyIrSlotId slot = function->parameter_slots[index];

        if (!study_x86_write(
                emitter,
                "  movl %s, ",
                parameter_registers[index]
            ) ||
            !study_x86_slot_operand(emitter, &frame, slot, "\n")) {
            goto cleanup;
        }
    }

    block_id = function->first_block;
    while (block_id != STUDY_IR_BLOCK_NONE) {
        StudyIrInstructionId instruction_id =
            program->blocks[block_id].first_instruction;

        if (!study_x86_block_label(
                emitter,
                function_id,
                block_id,
                ":\n"
            )) {
            goto cleanup;
        }
        while (instruction_id != STUDY_IR_INSTRUCTION_NONE) {
            const StudyIrInstruction *instruction =
                &program->instructions[instruction_id];

            if (!study_x86_emit_instruction(
                    emitter,
                    &frame,
                    function_id,
                    instruction
                )) {
                goto cleanup;
            }
            instruction_id = instruction->next_instruction;
        }
        block_id = program->blocks[block_id].next_block;
    }

    if (!study_x86_write(
            emitter,
            ".size study_fn_%zu, .-study_fn_%zu\n",
            function_id,
            function_id
        )) {
        goto cleanup;
    }
    success = true;

cleanup:
    study_x86_frame_free(&frame);
    return success;
}

static bool study_x86_emit_runtime(StudyX86Emitter *emitter) {
    return study_x86_write(
        emitter,
        "\n.p2align 4\n"
        ".type study_rt_print_i32, @function\n"
        "study_rt_print_i32:\n"
        "  pushq %%rbp\n"
        "  movq %%rsp, %%rbp\n"
        "  subq $64, %%rsp\n"
        "  xorl %%r8d, %%r8d\n"
        "  movl %%edi, %%eax\n"
        "  testl %%eax, %%eax\n"
        "  jns .Lstudy_print_magnitude\n"
        "  movl $1, %%r8d\n"
        "  negl %%eax\n"
        ".Lstudy_print_magnitude:\n"
        "  leaq -1(%%rbp), %%rsi\n"
        "  movb $10, (%%rsi)\n"
        "  movl $1, %%r9d\n"
        "  testl %%eax, %%eax\n"
        "  jne .Lstudy_print_digit\n"
        "  decq %%rsi\n"
        "  movb $48, (%%rsi)\n"
        "  incq %%r9\n"
        "  jmp .Lstudy_print_sign\n"
        ".Lstudy_print_digit:\n"
        "  xorl %%edx, %%edx\n"
        "  movl $10, %%ecx\n"
        "  divl %%ecx\n"
        "  addb $48, %%dl\n"
        "  decq %%rsi\n"
        "  movb %%dl, (%%rsi)\n"
        "  incq %%r9\n"
        "  testl %%eax, %%eax\n"
        "  jne .Lstudy_print_digit\n"
        ".Lstudy_print_sign:\n"
        "  testl %%r8d, %%r8d\n"
        "  je .Lstudy_print_write\n"
        "  decq %%rsi\n"
        "  movb $45, (%%rsi)\n"
        "  incq %%r9\n"
        ".Lstudy_print_write:\n"
        "  movq %%r9, %%rdx\n"
        ".Lstudy_print_write_again:\n"
        "  movl $1, %%eax\n"
        "  movl $1, %%edi\n"
        "  syscall\n"
        "  cmpq $-4, %%rax\n"
        "  je .Lstudy_print_write_again\n"
        "  testq %%rax, %%rax\n"
        "  jle .Lstudy_print_failure\n"
        "  addq %%rax, %%rsi\n"
        "  subq %%rax, %%rdx\n"
        "  jne .Lstudy_print_write_again\n"
        "  leave\n"
        "  ret\n"
        ".Lstudy_print_failure:\n"
        "  movl $74, %%edi\n"
        "  movl $60, %%eax\n"
        "  syscall\n"
        "  ud2\n"
        ".size study_rt_print_i32, .-study_rt_print_i32\n"
    );
}

bool study_x86_64_emit_assembly(
    FILE *stream,
    const StudyIrProgram *program,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    StudyX86Emitter emitter;
    StudyIrFunctionId function_id;

    study_diagnostic_clear(diagnostic);
    if (stream == NULL || source == NULL ||
        !study_ir_verify(program, source, diagnostic)) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_diagnostic_set(
                diagnostic,
                STUDY_DIAGNOSTIC_INTERNAL,
                source != NULL ? source->path : NULL,
                "x86-64 assembly emission requires an output stream and source"
            );
        }
        return false;
    }

    emitter.stream = stream;
    emitter.program = program;
    emitter.source = source;
    emitter.diagnostic = diagnostic;

    if (!study_x86_write(
            &emitter,
            ".text\n"
            ".globl _start\n"
            ".type _start, @function\n"
            "_start:\n"
            "  call study_fn_%zu\n"
            "  movl %%eax, %%edi\n"
            "  movl $60, %%eax\n"
            "  syscall\n"
            ".size _start, .-_start\n",
            program->main_function
        ) ||
        !study_x86_emit_runtime(&emitter)) {
        return false;
    }

    for (function_id = 0U;
         function_id < program->function_count;
         function_id++) {
        if (!study_x86_emit_function(&emitter, function_id)) {
            return false;
        }
    }

    if (!study_x86_write(
            &emitter,
            "\n.section .note.GNU-stack,\"\",@progbits\n"
        ) ||
        fflush(stream) != 0) {
        if (diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
            study_x86_error(&emitter, NULL, "could not flush assembly text");
        }
        return false;
    }
    return true;
}
