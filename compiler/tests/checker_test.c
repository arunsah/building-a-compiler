#include "study/checked.h"
#include "study/checker.h"
#include "study/diagnostic.h"
#include "study/lexer.h"
#include "study/parser.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct CheckedFixture {
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyCheckedProgram checked;
} CheckedFixture;

static void checked_fixture_init(CheckedFixture *fixture) {
    study_source_init(&fixture->source);
    study_token_list_init(&fixture->tokens);
    study_syntax_tree_init(&fixture->tree);
    study_checked_program_init(&fixture->checked);
}

static void checked_fixture_free(CheckedFixture *fixture) {
    study_checked_program_free(&fixture->checked);
    study_syntax_tree_free(&fixture->tree);
    study_token_list_free(&fixture->tokens);
    study_source_free(&fixture->source);
}

static void parse_text(CheckedFixture *fixture, const char *text) {
    StudyDiagnostic diagnostic;

    checked_fixture_init(fixture);
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
}

static void check_text(CheckedFixture *fixture, const char *text) {
    StudyDiagnostic diagnostic;

    parse_text(fixture, text);
    study_diagnostic_clear(&diagnostic);
    assert(study_check_program(
        &fixture->source,
        &fixture->tokens,
        &fixture->tree,
        &fixture->checked,
        &diagnostic
    ));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
}

static const StudySyntaxNode *node(
    const CheckedFixture *fixture,
    StudySyntaxNodeId node_id
) {
    assert(node_id != STUDY_SYNTAX_NODE_NONE);
    assert(node_id < fixture->tree.count);
    return &fixture->tree.nodes[node_id];
}

static StudySyntaxNodeId child_at(
    const CheckedFixture *fixture,
    StudySyntaxNodeId parent_id,
    size_t child_index
) {
    StudySyntaxNodeId child = node(fixture, parent_id)->first_child;
    size_t index;

    for (index = 0U; index < child_index; index++) {
        child = node(fixture, child)->next_sibling;
    }

    assert(child != STUDY_SYNTAX_NODE_NONE);
    return child;
}

static StudySyntaxNodeId function_body(
    const CheckedFixture *fixture,
    StudySyntaxNodeId function_id
) {
    StudySyntaxNodeId child = node(fixture, function_id)->first_child;

    while (node(fixture, child)->kind == STUDY_SYNTAX_PARAMETER) {
        child = node(fixture, child)->next_sibling;
    }

    assert(node(fixture, child)->kind == STUDY_SYNTAX_BLOCK);
    return child;
}

static void expect_check_error(
    const char *text,
    const char *message,
    size_t line,
    size_t column
) {
    CheckedFixture fixture;
    StudyDiagnostic diagnostic;

    parse_text(&fixture, text);
    study_diagnostic_clear(&diagnostic);
    assert(!study_check_program(
        &fixture.source,
        &fixture.tokens,
        &fixture.tree,
        &fixture.checked,
        &diagnostic
    ));
    assert(fixture.checked.nodes == NULL);
    assert(fixture.checked.node_count == 0U);
    assert(fixture.checked.declarations == NULL);
    assert(fixture.checked.declaration_count == 0U);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_SOURCE);
    assert(diagnostic.has_location);
    if (diagnostic.line != line || diagnostic.column != column ||
        strcmp(diagnostic.message, message) != 0) {
        (void)fprintf(
            stderr,
            "expected %zu:%zu: %s\nreceived %zu:%zu: %s\n",
            line,
            column,
            message,
            diagnostic.line,
            diagnostic.column,
            diagnostic.message
        );
    }
    assert(diagnostic.line == line);
    assert(diagnostic.column == column);
    assert(strcmp(diagnostic.message, message) == 0);
    checked_fixture_free(&fixture);
}

static void test_forward_calls_scopes_and_annotations(void) {
    static const char text[] =
        "fn main() -> i32 {\n"
        "    var result: i32 = later(1);\n"
        "    {\n"
        "        var result: bool = true;\n"
        "        if result { print(1); }\n"
        "    }\n"
        "    return result;\n"
        "}\n"
        "fn later(value: i32) -> i32 { return value + 1; }\n";
    CheckedFixture fixture;
    StudySyntaxNodeId main_function;
    StudySyntaxNodeId main_body;
    StudySyntaxNodeId outer_var;
    StudySyntaxNodeId call;
    StudySyntaxNodeId nested_block;
    StudySyntaxNodeId inner_var;
    StudySyntaxNodeId return_statement;
    StudySyntaxNodeId result_name;
    StudyDeclarationId outer_declaration;
    StudyDeclarationId inner_declaration;

    check_text(&fixture, text);

    assert(fixture.checked.node_count == fixture.tree.count);
    assert(fixture.checked.declaration_count == 5U);
    main_function = child_at(&fixture, fixture.tree.root, 0U);
    assert(fixture.checked.nodes[main_function].declaration == 0U);
    assert(fixture.checked.nodes[main_function].definitely_returns);

    main_body = function_body(&fixture, main_function);
    outer_var = child_at(&fixture, main_body, 0U);
    outer_declaration = fixture.checked.nodes[outer_var].declaration;
    assert(outer_declaration != STUDY_DECLARATION_NONE);
    assert(fixture.checked.declarations[outer_declaration].kind ==
           STUDY_DECLARATION_LOCAL);

    call = child_at(&fixture, outer_var, 0U);
    assert(fixture.checked.nodes[call].has_type);
    assert(fixture.checked.nodes[call].type == STUDY_SYNTAX_TYPE_I32);
    assert(fixture.checked.nodes[call].declaration == 1U);

    nested_block = child_at(&fixture, main_body, 1U);
    inner_var = child_at(&fixture, nested_block, 0U);
    inner_declaration = fixture.checked.nodes[inner_var].declaration;
    assert(inner_declaration != STUDY_DECLARATION_NONE);
    assert(inner_declaration != outer_declaration);

    return_statement = child_at(&fixture, main_body, 2U);
    result_name = child_at(&fixture, return_statement, 0U);
    assert(fixture.checked.nodes[result_name].declaration ==
           outer_declaration);
    assert(fixture.checked.nodes[result_name].type == STUDY_SYNTAX_TYPE_I32);

    checked_fixture_free(&fixture);
}

static void test_both_if_paths_can_return(void) {
    CheckedFixture fixture;

    check_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    if true { return 1; } else { return 2; }\n"
        "}\n"
    );
    assert(fixture.checked.nodes[fixture.tree.root].declaration ==
           STUDY_DECLARATION_NONE);
    checked_fixture_free(&fixture);
}

static void test_scope_and_name_errors(void) {
    expect_check_error(
        "fn main() -> i32 {\n"
        "    var value: i32 = 1;\n"
        "    var value: i32 = 2;\n"
        "    return value;\n"
        "}\n",
        "duplicate declaration of 'value'",
        3U,
        9U
    );
    expect_check_error(
        "fn main() -> i32 { return missing; }",
        "unknown local name 'missing'",
        1U,
        27U
    );
    expect_check_error(
        "fn main() -> i32 { value = 1; return 0; }",
        "assignment to unknown local name 'value'",
        1U,
        20U
    );
    expect_check_error(
        "fn main() -> i32 { var value: i32 = value; return value; }",
        "unknown local name 'value'",
        1U,
        37U
    );
    expect_check_error(
        "fn take(value: i32, value: bool) -> i32 { return 0; }\n"
        "fn main() -> i32 { return 0; }\n",
        "duplicate declaration of 'value'",
        1U,
        21U
    );
}

static void test_type_errors(void) {
    expect_check_error(
        "fn main() -> i32 { var value: i32 = true; return 0; }",
        "variable initializer requires i32, found bool",
        1U,
        37U
    );
    expect_check_error(
        "fn main() -> i32 { var value: i32 = 1; value = false; return 0; }",
        "assignment requires i32, found bool",
        1U,
        48U
    );
    expect_check_error(
        "fn main() -> i32 { if 1 { return 1; } return 0; }",
        "if condition requires bool, found i32",
        1U,
        23U
    );
    expect_check_error(
        "fn main() -> i32 { while 1 { return 1; } return 0; }",
        "while condition requires bool, found i32",
        1U,
        26U
    );
    expect_check_error(
        "fn main() -> i32 { print(true); return 0; }",
        "print requires i32, found bool",
        1U,
        26U
    );
    expect_check_error(
        "fn main() -> i32 { return true; }",
        "return value requires i32, found bool",
        1U,
        27U
    );
    expect_check_error(
        "fn main() -> i32 { if 1 == true { return 1; } return 0; }",
        "equality operands have different types: i32 and bool",
        1U,
        23U
    );
    expect_check_error(
        "fn main() -> i32 { return true + false; }",
        "arithmetic operator requires i32, found bool",
        1U,
        27U
    );
    expect_check_error(
        "fn main() -> i32 { return !1; }",
        "unary '!' requires bool, found i32",
        1U,
        28U
    );
}

static void test_function_and_call_errors(void) {
    expect_check_error(
        "fn main() -> i32 { return unknown(1); }",
        "unknown function 'unknown'",
        1U,
        27U
    );
    expect_check_error(
        "fn take(value: i32) -> i32 { return value; }\n"
        "fn main() -> i32 { return take(); }\n",
        "function 'take' expects 1 argument, found 0",
        2U,
        27U
    );
    expect_check_error(
        "fn take(value: i32) -> i32 { return value; }\n"
        "fn main() -> i32 { return take(true); }\n",
        "function argument requires i32, found bool",
        2U,
        32U
    );
    expect_check_error(
        "fn helper() -> i32 { return 0; }",
        "program must declare fn main() -> i32",
        1U,
        1U
    );
    expect_check_error(
        "fn main(value: i32) -> i32 { return value; }",
        "main must have signature fn main() -> i32",
        1U,
        4U
    );
    expect_check_error(
        "fn main() -> bool { return true; }",
        "main must have signature fn main() -> i32",
        1U,
        4U
    );
    expect_check_error(
        "fn main() -> i32 { return 0; }\n"
        "fn main() -> i32 { return 1; }\n",
        "duplicate function 'main'",
        2U,
        4U
    );
    expect_check_error(
        "fn too_many(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32) -> i32 { return 0; }\n"
        "fn main() -> i32 { return 0; }\n",
        "function 'too_many' has more than six parameters",
        1U,
        4U
    );
}

static void test_return_path_errors(void) {
    expect_check_error(
        "fn main() -> i32 { var value: i32 = 1; }",
        "function 'main' may reach its end without returning i32",
        1U,
        4U
    );
    expect_check_error(
        "fn main() -> i32 { if true { return 1; } }",
        "function 'main' may reach its end without returning i32",
        1U,
        4U
    );
    expect_check_error(
        "fn main() -> i32 { while true { return 1; } }",
        "function 'main' may reach its end without returning i32",
        1U,
        4U
    );
}

static void test_nonempty_output_is_rejected(void) {
    CheckedFixture fixture;
    StudyDiagnostic diagnostic;
    size_t declaration_count;

    check_text(&fixture, "fn main() -> i32 { return 0; }");
    declaration_count = fixture.checked.declaration_count;
    study_diagnostic_clear(&diagnostic);
    assert(!study_check_program(
        &fixture.source,
        &fixture.tokens,
        &fixture.tree,
        &fixture.checked,
        &diagnostic
    ));
    assert(fixture.checked.declaration_count == declaration_count);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);
    checked_fixture_free(&fixture);
}

int main(void) {
    test_forward_calls_scopes_and_annotations();
    test_both_if_paths_can_return();
    test_scope_and_name_errors();
    test_type_errors();
    test_function_and_call_errors();
    test_return_path_errors();
    test_nonempty_output_is_rejected();
    return 0;
}
