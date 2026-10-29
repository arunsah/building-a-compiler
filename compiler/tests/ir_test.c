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
    test_loop_call_print_and_result();
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
