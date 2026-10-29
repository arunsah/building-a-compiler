#include "study/checked.h"
#include "study/checker.h"
#include "study/diagnostic.h"
#include "study/interpreter.h"
#include "study/ir.h"
#include "study/ir_verify.h"
#include "study/lexer.h"
#include "study/lower.h"
#include "study/parser.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"
#include "study/x86_64.h"
#include "study/x86_64_assembly.h"
#include "study/x86_64_encode.h"
#include "study/x86_64_lower.h"
#include "study/x86_64_verify.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct IrFixture {
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyCheckedProgram checked;
    StudyIrProgram ir;
} IrFixture;

static void ir_fixture_init(IrFixture *fixture) {
    study_source_init(&fixture->source);
    study_token_list_init(&fixture->tokens);
    study_syntax_tree_init(&fixture->tree);
    study_checked_program_init(&fixture->checked);
    study_ir_program_init(&fixture->ir);
}

static void ir_fixture_free(IrFixture *fixture) {
    study_ir_program_free(&fixture->ir);
    study_checked_program_free(&fixture->checked);
    study_syntax_tree_free(&fixture->tree);
    study_token_list_free(&fixture->tokens);
    study_source_free(&fixture->source);
}

static void lower_text(IrFixture *fixture, const char *text) {
    StudyDiagnostic diagnostic;

    ir_fixture_init(fixture);
    study_diagnostic_clear(&diagnostic);
    assert(study_source_copy(
        &fixture->source,
        "memory.study",
        (const unsigned char *)text,
        strlen(text),
        &diagnostic
    ));
    assert(study_lex(&fixture->source, &fixture->tokens, &diagnostic));
    assert(study_parse(
        &fixture->source,
        &fixture->tokens,
        &fixture->tree,
        &diagnostic
    ));
    assert(study_check_program(
        &fixture->source,
        &fixture->tokens,
        &fixture->tree,
        &fixture->checked,
        &diagnostic
    ));
    assert(study_lower_to_ir(
        &fixture->source,
        &fixture->tokens,
        &fixture->tree,
        &fixture->checked,
        &fixture->ir,
        &diagnostic
    ));
    assert(study_ir_verify(&fixture->ir, &fixture->source, &diagnostic));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
}

static StudyIrInstructionId find_opcode(
    const StudyIrProgram *program,
    StudyIrOpcode opcode,
    size_t occurrence
) {
    size_t index;

    for (index = 0U; index < program->instruction_count; index++) {
        if (program->instructions[index].opcode == opcode) {
            if (occurrence == 0U) {
                return index;
            }
            occurrence -= 1U;
        }
    }

    assert(false);
    return STUDY_IR_INSTRUCTION_NONE;
}

static void read_stream(FILE *stream, char *buffer, size_t capacity) {
    size_t count;

    assert(capacity > 0U);
    assert(fflush(stream) == 0);
    assert(fseek(stream, 0L, SEEK_SET) == 0);
    count = fread(buffer, 1U, capacity - 1U, stream);
    assert(!ferror(stream));
    buffer[count] = '\0';
}

static uint32_t interpret_fixture(
    IrFixture *fixture,
    char *output,
    size_t output_capacity
) {
    StudyDiagnostic diagnostic;
    FILE *stream = tmpfile();
    uint32_t result = 0U;

    assert(stream != NULL);
    study_diagnostic_clear(&diagnostic);
    assert(study_ir_interpret(
        &fixture->ir,
        &fixture->source,
        stream,
        &result,
        &diagnostic
    ));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
    read_stream(stream, output, output_capacity);
    assert(fclose(stream) == 0);
    return result;
}

static void emit_assembly_fixture(
    IrFixture *fixture,
    char *output,
    size_t output_capacity
) {
    StudyDiagnostic diagnostic;
    StudyX86Program x86;
    FILE *stream = tmpfile();

    assert(stream != NULL);
    study_x86_64_program_init(&x86);
    study_diagnostic_clear(&diagnostic);
    assert(study_lower_to_x86_64(
        &fixture->ir,
        &fixture->source,
        &x86,
        &diagnostic
    ));
    assert(study_x86_64_emit_assembly(
        stream,
        &x86,
        &fixture->source,
        &diagnostic
    ));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
    read_stream(stream, output, output_capacity);
    assert(fclose(stream) == 0);
    study_x86_64_program_free(&x86);
}

static void lower_x86_fixture(
    IrFixture *fixture,
    StudyX86Program *x86
) {
    StudyDiagnostic diagnostic;

    study_x86_64_program_init(x86);
    study_diagnostic_clear(&diagnostic);
    assert(study_lower_to_x86_64(
        &fixture->ir,
        &fixture->source,
        x86,
        &diagnostic
    ));
    assert(study_x86_64_verify(x86, &fixture->source, &diagnostic));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
}

static void encode_x86_fixture(
    IrFixture *fixture,
    StudyX86Program *x86,
    StudyX86Image *image
) {
    StudyDiagnostic diagnostic;

    lower_x86_fixture(fixture, x86);
    study_x86_64_image_init(image);
    study_diagnostic_clear(&diagnostic);
    assert(study_x86_64_encode(
        x86,
        &fixture->source,
        image,
        &diagnostic
    ));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
}

static int32_t read_little_endian_i32(const unsigned char *bytes) {
    uint32_t value = (uint32_t)bytes[0] |
                     ((uint32_t)bytes[1] << 8U) |
                     ((uint32_t)bytes[2] << 16U) |
                     ((uint32_t)bytes[3] << 24U);

    return (int32_t)value;
}

static void expect_verify_error(IrFixture *fixture, const char *message) {
    StudyDiagnostic diagnostic;

    study_diagnostic_clear(&diagnostic);
    assert(!study_ir_verify(&fixture->ir, &fixture->source, &diagnostic));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);
    if (strstr(diagnostic.message, message) == NULL) {
        (void)fprintf(
            stderr,
            "expected verifier message containing: %s\nreceived: %s\n",
            message,
            diagnostic.message
        );
    }
    assert(strstr(diagnostic.message, message) != NULL);
}

static void test_small_ir_shape(void) {
    IrFixture fixture;
    const StudyIrFunction *function;
    const StudyIrBlock *block;
    const StudyIrInstruction *constant;
    const StudyIrInstruction *return_instruction;

    lower_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n"
    );

    assert(fixture.ir.function_count == 1U);
    assert(fixture.ir.main_function == 0U);
    assert(fixture.ir.block_count == 1U);
    assert(fixture.ir.instruction_count == 2U);
    assert(fixture.ir.value_count == 1U);
    function = &fixture.ir.functions[0];
    assert(function->parameter_count == 0U);
    assert(function->result_type == STUDY_SYNTAX_TYPE_I32);
    block = &fixture.ir.blocks[function->entry_block];
    constant = &fixture.ir.instructions[block->first_instruction];
    return_instruction = &fixture.ir.instructions[block->last_instruction];
    assert(constant->opcode == STUDY_IR_CONST_I32);
    assert(constant->as.integer == 42U);
    assert(return_instruction->opcode == STUDY_IR_RETURN);
    assert(return_instruction->as.return_value.value == constant->result);

    ir_fixture_free(&fixture);
}

static void test_small_ir_assembly(void) {
    IrFixture fixture;
    char assembly[8192];

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    emit_assembly_fixture(&fixture, assembly, sizeof(assembly));
    assert(strstr(assembly, ".globl _start\n") != NULL);
    assert(strstr(assembly, "  call study_fn_0\n") != NULL);
    assert(strstr(assembly, "  movl $0x0000002a, %eax\n") != NULL);
    assert(strstr(assembly, "  movl %eax, %edi\n") != NULL);
    assert(strstr(assembly, ".section .note.GNU-stack") != NULL);
    ir_fixture_free(&fixture);
}

static void test_small_x86_target_shape_and_dump(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyDiagnostic diagnostic;
    FILE *stream;
    char dump[16384];
    const StudyX86Function *function;

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    lower_x86_fixture(&fixture, &x86);
    assert(x86.function_count == 3U);
    assert(x86.start_function == 0U);
    assert(x86.print_function == 1U);
    assert(x86.functions[0].kind == STUDY_X86_FUNCTION_START);
    assert(x86.functions[1].kind == STUDY_X86_FUNCTION_PRINT_I32);
    function = &x86.functions[2];
    assert(function->kind == STUDY_X86_FUNCTION_STUDY);
    assert(function->ir_function == 0U);
    assert(function->frame_size == 16U);
    assert(function->storage_count == 1U);
    assert(x86.storage[function->first_storage].kind ==
           STUDY_X86_STORAGE_VALUE);
    assert(x86.storage[function->first_storage].ir_id == 0U);
    assert(x86.storage[function->first_storage].displacement == -8);

    stream = tmpfile();
    assert(stream != NULL);
    study_diagnostic_clear(&diagnostic);
    assert(study_x86_64_program_dump(
        stream,
        &x86,
        &fixture.source,
        &diagnostic
    ));
    read_stream(stream, dump, sizeof(dump));
    assert(fclose(stream) == 0);
    assert(strstr(dump, "target x86-64-linux-system-v") != NULL);
    assert(strstr(dump, "function f0 _start") != NULL);
    assert(strstr(dump, "ir.v0 -> width=32 [rbp-8]") != NULL);
    assert(strstr(dump, "mov.i32 #0x0000002a, eax") != NULL);
    assert(strstr(dump, "mov.i32 eax, [rbp-8]") != NULL);

    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_small_x86_encoding_and_dump(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyX86Image image;
    StudyDiagnostic diagnostic;
    FILE *stream;
    char dump[32768];
    int32_t call_displacement;
    size_t call_target;
    size_t index;
    bool found_constant = false;

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    encode_x86_fixture(&fixture, &x86, &image);
    assert(image.length > 0U);
    assert(image.entry_offset == 0U);
    assert(image.function_count == x86.function_count);
    assert(image.block_count == x86.block_count);
    assert(image.instruction_count == x86.instruction_count);
    assert(image.bytes[0] == UINT8_C(0xe8));
    call_displacement = read_little_endian_i32(&image.bytes[1]);
    assert(call_displacement >= 0);
    call_target = 5U + (size_t)call_displacement;
    assert(call_target == image.function_offsets[2]);

    for (index = 0U; index < x86.instruction_count; index++) {
        const StudyX86Instruction *instruction = &x86.instructions[index];

        assert(image.instruction_sizes[index] > 0U);
        if (instruction->opcode == STUDY_X86_MOV &&
            instruction->first.kind == STUDY_X86_OPERAND_IMMEDIATE &&
            instruction->first.width == STUDY_X86_WIDTH_32 &&
            instruction->first.as.immediate.value == 42U) {
            size_t offset = image.instruction_offsets[index];

            assert(image.bytes[offset] == UINT8_C(0xb8));
            assert(image.bytes[offset + 1U] == UINT8_C(0x2a));
            found_constant = true;
        }
    }
    assert(found_constant);

    stream = tmpfile();
    assert(stream != NULL);
    study_diagnostic_clear(&diagnostic);
    assert(study_x86_64_image_dump(
        stream,
        &x86,
        &image,
        &fixture.source,
        &diagnostic
    ));
    read_stream(stream, dump, sizeof(dump));
    assert(fclose(stream) == 0);
    assert(strstr(dump, "image x86-64-linux-system-v") != NULL);
    assert(strstr(dump, "function f0 _start @0x00000000") != NULL);
    assert(strstr(dump, "00000000: e8") != NULL);
    assert(strstr(dump, "i0 call") != NULL);

    study_x86_64_image_free(&image);
    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_x86_encoder_covers_current_opcode_set(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyX86Image image;
    bool seen[STUDY_X86_UNDEFINED + 1U] = {false};
    size_t index;

    lower_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    var value: i32 = 6 * 7;\n"
        "    if value == 42 { print(value); return 0; }\n"
        "    else { return -1; }\n"
        "}\n"
    );
    encode_x86_fixture(&fixture, &x86, &image);
    for (index = 0U; index < x86.instruction_count; index++) {
        seen[x86.instructions[index].opcode] = true;
    }
    for (index = 0U; index <= (size_t)STUDY_X86_UNDEFINED; index++) {
        assert(seen[index]);
    }

    study_x86_64_image_free(&image);
    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_x86_encoder_rejects_nonempty_output(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyX86Image image;
    StudyDiagnostic diagnostic;
    size_t length;

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    encode_x86_fixture(&fixture, &x86, &image);
    length = image.length;
    study_diagnostic_clear(&diagnostic);
    assert(!study_x86_64_encode(
        &x86,
        &fixture.source,
        &image,
        &diagnostic
    ));
    assert(image.length == length);
    assert(strstr(diagnostic.message, "initialised empty output image") != NULL);

    study_x86_64_image_free(&image);
    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_x86_verifier_rejects_cross_function_branch(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyDiagnostic diagnostic;
    size_t index;
    bool changed = false;

    lower_text(
        &fixture,
        "fn main() -> i32 { if true { return 1; } else { return 2; } }\n"
    );
    lower_x86_fixture(&fixture, &x86);
    for (index = 0U; index < x86.instruction_count; index++) {
        if (x86.instructions[index].opcode ==
                STUDY_X86_BRANCH_CONDITION &&
            x86.instructions[index].function == 2U) {
            x86.instructions[index].then_block =
                x86.functions[x86.print_function].entry_block;
            changed = true;
            break;
        }
    }
    assert(changed);
    study_diagnostic_clear(&diagnostic);
    assert(!study_x86_64_verify(&x86, &fixture.source, &diagnostic));
    assert(strstr(diagnostic.message, "invalid operands") != NULL);

    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_x86_verifier_rejects_storage_outside_frame(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyDiagnostic diagnostic;

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    lower_x86_fixture(&fixture, &x86);
    x86.functions[2].frame_size = 0U;
    study_diagnostic_clear(&diagnostic);
    assert(!study_x86_64_verify(&x86, &fixture.source, &diagnostic));
    assert(strstr(diagnostic.message, "outside its owning frame") != NULL);

    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_x86_verifier_rejects_unencodable_immediate(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyDiagnostic diagnostic;
    size_t index;
    bool changed = false;

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    lower_x86_fixture(&fixture, &x86);
    for (index = 0U; index < x86.instruction_count; index++) {
        StudyX86Instruction *instruction = &x86.instructions[index];

        if (instruction->opcode == STUDY_X86_COMPARE &&
            instruction->first.kind == STUDY_X86_OPERAND_IMMEDIATE &&
            instruction->first.width == STUDY_X86_WIDTH_64) {
            instruction->first.as.immediate.value = UINT64_C(0x80000000);
            instruction->first.as.immediate.is_signed = false;
            changed = true;
            break;
        }
    }
    assert(changed);
    study_diagnostic_clear(&diagnostic);
    assert(!study_x86_64_verify(&x86, &fixture.source, &diagnostic));
    assert(strstr(diagnostic.message, "invalid operands") != NULL);

    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_x86_lowering_rejects_nonempty_output(void) {
    IrFixture fixture;
    StudyX86Program x86;
    StudyDiagnostic diagnostic;
    size_t instruction_count;

    lower_text(&fixture, "fn main() -> i32 { return 42; }\n");
    lower_x86_fixture(&fixture, &x86);
    instruction_count = x86.instruction_count;
    study_diagnostic_clear(&diagnostic);
    assert(!study_lower_to_x86_64(
        &fixture.ir,
        &fixture.source,
        &x86,
        &diagnostic
    ));
    assert(x86.instruction_count == instruction_count);
    assert(strstr(diagnostic.message, "initialised empty output") != NULL);

    study_x86_64_program_free(&x86);
    ir_fixture_free(&fixture);
}

static void test_loop_call_print_and_result(void) {
    static const char text[] =
        "fn sum_to(limit: i32) -> i32 {\n"
        "    var current: i32 = 1;\n"
        "    var total: i32 = 0;\n"
        "    while current <= limit {\n"
        "        total = total + current;\n"
        "        current = current + 1;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    var answer: i32 = sum_to(10);\n"
        "    print(answer);\n"
        "    return 0;\n"
        "}\n";
    IrFixture fixture;
    char output[64];
    uint32_t result;

    lower_text(&fixture, text);
    assert(fixture.ir.function_count == 2U);
    assert(fixture.ir.block_count == 5U);
    assert(find_opcode(&fixture.ir, STUDY_IR_CALL, 0U) !=
           STUDY_IR_INSTRUCTION_NONE);
    assert(find_opcode(&fixture.ir, STUDY_IR_BRANCH, 0U) !=
           STUDY_IR_INSTRUCTION_NONE);
    result = interpret_fixture(&fixture, output, sizeof(output));
    assert(result == 0U);
    assert(strcmp(output, "55\n") == 0);

    ir_fixture_free(&fixture);
}

static void test_loop_call_and_print_assembly(void) {
    static const char text[] =
        "fn sum_to(limit: i32) -> i32 {\n"
        "    var current: i32 = 1;\n"
        "    var total: i32 = 0;\n"
        "    while current <= limit {\n"
        "        total = total + current;\n"
        "        current = current + 1;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    var answer: i32 = sum_to(10);\n"
        "    print(answer);\n"
        "    return 0;\n"
        "}\n";
    IrFixture fixture;
    char assembly[16384];

    lower_text(&fixture, text);
    emit_assembly_fixture(&fixture, assembly, sizeof(assembly));
    assert(strstr(assembly, "  call study_fn_1\n") != NULL);
    assert(strstr(assembly, "  call study_fn_0\n") != NULL);
    assert(strstr(assembly, "  setle %al\n") != NULL);
    assert(strstr(assembly, "  jne .Lstudy_x86_b") != NULL);
    assert(strstr(assembly, "  call study_rt_print_i32\n") != NULL);
    assert(strstr(assembly, "  syscall\n  cmpq $-4, %rax\n") != NULL);
    ir_fixture_free(&fixture);
}

static void test_wraparound_and_signed_comparison(void) {
    static const char text[] =
        "fn main() -> i32 {\n"
        "    var value: i32 = 2147483647 + 1;\n"
        "    print(value);\n"
        "    if value < 0 { return value; } else { return 1; }\n"
        "}\n";
    IrFixture fixture;
    char output[64];
    uint32_t result;

    lower_text(&fixture, text);
    result = interpret_fixture(&fixture, output, sizeof(output));
    assert(result == UINT32_C(2147483648));
    assert(strcmp(output, "-2147483648\n") == 0);

    ir_fixture_free(&fixture);
}

static void test_boolean_rules_and_both_returning_branches(void) {
    IrFixture fixture;
    char output[8];
    uint32_t result;

    lower_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    if !(1 + 2 * 3 != 7) { return 11; }\n"
        "    else { return 12; }\n"
        "}\n"
    );
    result = interpret_fixture(&fixture, output, sizeof(output));
    assert(result == 11U);
    assert(strcmp(output, "") == 0);
    ir_fixture_free(&fixture);
}

static void test_recursive_calls(void) {
    static const char text[] =
        "fn count(value: i32) -> i32 {\n"
        "    if value == 0 { return 0; }\n"
        "    else { return count(value - 1) + 1; }\n"
        "}\n"
        "fn main() -> i32 { return count(5); }\n";
    IrFixture fixture;
    char output[8];
    uint32_t result;

    lower_text(&fixture, text);
    result = interpret_fixture(&fixture, output, sizeof(output));
    assert(result == 5U);
    assert(strcmp(output, "") == 0);
    ir_fixture_free(&fixture);
}

static void test_shadowed_slots_keep_their_values(void) {
    static const char text[] =
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    { var value: i32 = 2; print(value); }\n"
        "    print(value);\n"
        "    return 0;\n"
        "}\n";
    IrFixture fixture;
    char output[16];
    uint32_t result;

    lower_text(&fixture, text);
    result = interpret_fixture(&fixture, output, sizeof(output));
    assert(result == 0U);
    assert(strcmp(output, "2\n1\n") == 0);
    ir_fixture_free(&fixture);
}

static void test_interpreter_bounds_recursive_calls(void) {
    static const char text[] =
        "fn again(value: i32) -> i32 { return again(value + 1); }\n"
        "fn main() -> i32 { return again(0); }\n";
    IrFixture fixture;
    StudyDiagnostic diagnostic;
    FILE *stream;
    uint32_t result = 0U;

    lower_text(&fixture, text);
    stream = tmpfile();
    assert(stream != NULL);
    study_diagnostic_clear(&diagnostic);
    assert(!study_ir_interpret(
        &fixture.ir,
        &fixture.source,
        stream,
        &result,
        &diagnostic
    ));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);
    assert(strstr(diagnostic.message, "call depth exceeded 256") != NULL);
    assert(fclose(stream) == 0);
    ir_fixture_free(&fixture);
}

static void test_verifier_rejects_bad_branch_type(void) {
    IrFixture fixture;
    StudyIrInstructionId branch_id;
    StudyIrInstructionId integer_id;

    lower_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    if true { return 1; } else { return 2; }\n"
        "}\n"
    );
    branch_id = find_opcode(&fixture.ir, STUDY_IR_BRANCH, 0U);
    integer_id = find_opcode(&fixture.ir, STUDY_IR_CONST_I32, 0U);
    fixture.ir.instructions[branch_id].as.branch.condition =
        fixture.ir.instructions[integer_id].result;
    expect_verify_error(&fixture, "branch has a non-bool condition");
    ir_fixture_free(&fixture);
}

static void test_verifier_rejects_missing_terminator(void) {
    IrFixture fixture;
    StudyIrInstructionId return_id;
    StudyIrValueId value;

    lower_text(&fixture, "fn main() -> i32 { return 0; }\n");
    return_id = find_opcode(&fixture.ir, STUDY_IR_RETURN, 0U);
    value = fixture.ir.instructions[return_id].as.return_value.value;
    fixture.ir.instructions[return_id].opcode = STUDY_IR_PRINT;
    fixture.ir.instructions[return_id].as.print.value = value;
    expect_verify_error(&fixture, "does not end in branch, jump, or return");
    ir_fixture_free(&fixture);
}

static void test_verifier_rejects_cross_block_value(void) {
    IrFixture fixture;
    StudyIrInstructionId branch_id;
    StudyIrInstructionId first_constant;

    lower_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    while value < 3 { value = value + 1; }\n"
        "    return value;\n"
        "}\n"
    );
    first_constant = find_opcode(&fixture.ir, STUDY_IR_CONST_I32, 0U);
    branch_id = find_opcode(&fixture.ir, STUDY_IR_BRANCH, 0U);
    fixture.ir.instructions[branch_id].as.branch.condition =
        fixture.ir.instructions[first_constant].result;
    expect_verify_error(
        &fixture,
        "operand is not defined earlier in the same block"
    );
    ir_fixture_free(&fixture);
}

static void test_verifier_rejects_cross_function_target(void) {
    IrFixture fixture;
    StudyIrInstructionId branch_id;

    lower_text(
        &fixture,
        "fn choose(flag: bool) -> i32 {\n"
        "    if flag { return 1; } else { return 2; }\n"
        "}\n"
        "fn main() -> i32 { return choose(true); }\n"
    );
    branch_id = find_opcode(&fixture.ir, STUDY_IR_BRANCH, 0U);
    fixture.ir.instructions[branch_id].as.branch.then_block =
        fixture.ir.functions[fixture.ir.main_function].entry_block;
    expect_verify_error(&fixture, "control-flow target is outside its function");
    ir_fixture_free(&fixture);
}

static void test_verifier_rejects_wrong_call_signature(void) {
    IrFixture fixture;
    StudyIrInstructionId call_id;

    lower_text(
        &fixture,
        "fn identity(value: i32) -> i32 { return value; }\n"
        "fn main() -> i32 { return identity(1); }\n"
    );
    call_id = find_opcode(&fixture.ir, STUDY_IR_CALL, 0U);
    fixture.ir.instructions[call_id].as.call.argument_count = 0U;
    expect_verify_error(&fixture, "call does not match its function signature");
    ir_fixture_free(&fixture);
}

static void test_nonempty_output_is_rejected(void) {
    IrFixture fixture;
    StudyDiagnostic diagnostic;
    size_t function_count;

    lower_text(&fixture, "fn main() -> i32 { return 0; }\n");
    function_count = fixture.ir.function_count;
    study_diagnostic_clear(&diagnostic);
    assert(!study_lower_to_ir(
        &fixture.source,
        &fixture.tokens,
        &fixture.tree,
        &fixture.checked,
        &fixture.ir,
        &diagnostic
    ));
    assert(fixture.ir.function_count == function_count);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);
    ir_fixture_free(&fixture);
}

int main(void) {
    test_small_ir_shape();
    test_small_ir_assembly();
    test_small_x86_target_shape_and_dump();
    test_small_x86_encoding_and_dump();
    test_x86_encoder_covers_current_opcode_set();
    test_x86_encoder_rejects_nonempty_output();
    test_x86_verifier_rejects_cross_function_branch();
    test_x86_verifier_rejects_storage_outside_frame();
    test_x86_verifier_rejects_unencodable_immediate();
    test_x86_lowering_rejects_nonempty_output();
    test_loop_call_print_and_result();
    test_loop_call_and_print_assembly();
    test_wraparound_and_signed_comparison();
    test_boolean_rules_and_both_returning_branches();
    test_recursive_calls();
    test_shadowed_slots_keep_their_values();
    test_interpreter_bounds_recursive_calls();
    test_verifier_rejects_bad_branch_type();
    test_verifier_rejects_missing_terminator();
    test_verifier_rejects_cross_block_value();
    test_verifier_rejects_cross_function_target();
    test_verifier_rejects_wrong_call_signature();
    test_nonempty_output_is_rejected();
    return 0;
}
