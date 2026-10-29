#include "study/diagnostic.h"
#include "study/lexer.h"
#include "study/parser.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct ParsedFixture {
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
} ParsedFixture;

static void parsed_fixture_init(ParsedFixture *fixture) {
    study_source_init(&fixture->source);
    study_token_list_init(&fixture->tokens);
    study_syntax_tree_init(&fixture->tree);
}

static void parsed_fixture_free(ParsedFixture *fixture) {
    study_syntax_tree_free(&fixture->tree);
    study_token_list_free(&fixture->tokens);
    study_source_free(&fixture->source);
}

static void parse_text(ParsedFixture *fixture, const char *text) {
    StudyDiagnostic diagnostic;

    parsed_fixture_init(fixture);
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
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
}

static const StudySyntaxNode *node(
    const StudySyntaxTree *tree,
    StudySyntaxNodeId node_id
) {
    assert(node_id != STUDY_SYNTAX_NODE_NONE);
    assert(node_id < tree->count);
    return &tree->nodes[node_id];
}

static StudySyntaxNodeId child_at(
    const StudySyntaxTree *tree,
    StudySyntaxNodeId parent_id,
    size_t child_index
) {
    StudySyntaxNodeId child = node(tree, parent_id)->first_child;
    size_t index;

    for (index = 0U; index < child_index; index++) {
        child = node(tree, child)->next_sibling;
    }

    assert(child != STUDY_SYNTAX_NODE_NONE);
    return child;
}

static StudySyntaxNodeId first_function_body(const StudySyntaxTree *tree) {
    StudySyntaxNodeId function_id = child_at(tree, tree->root, 0U);
    StudySyntaxNodeId child = node(tree, function_id)->first_child;

    while (node(tree, child)->kind == STUDY_SYNTAX_PARAMETER) {
        child = node(tree, child)->next_sibling;
    }

    assert(node(tree, child)->kind == STUDY_SYNTAX_BLOCK);
    return child;
}

static void test_small_program_tree(void) {
    ParsedFixture fixture;
    StudySyntaxNodeId function_id;
    StudySyntaxNodeId block_id;
    StudySyntaxNodeId return_id;
    StudySyntaxNodeId integer_id;

    parse_text(
        &fixture,
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n"
    );

    assert(node(&fixture.tree, fixture.tree.root)->kind ==
           STUDY_SYNTAX_PROGRAM);
    function_id = child_at(&fixture.tree, fixture.tree.root, 0U);
    assert(node(&fixture.tree, function_id)->kind == STUDY_SYNTAX_FUNCTION);
    assert(node(&fixture.tree, function_id)->as.function.result_type ==
           STUDY_SYNTAX_TYPE_I32);

    block_id = first_function_body(&fixture.tree);
    return_id = child_at(&fixture.tree, block_id, 0U);
    integer_id = child_at(&fixture.tree, return_id, 0U);
    assert(node(&fixture.tree, return_id)->kind ==
           STUDY_SYNTAX_RETURN_STATEMENT);
    assert(node(&fixture.tree, integer_id)->kind ==
           STUDY_SYNTAX_INTEGER_EXPRESSION);
    assert(node(&fixture.tree, integer_id)->as.integer.value == 42U);
    assert(node(&fixture.tree, return_id)->range.start.line == 2U);
    assert(node(&fixture.tree, return_id)->range.start.column == 5U);
    assert(node(&fixture.tree, return_id)->range.end.column == 15U);

    parsed_fixture_free(&fixture);
}

static void test_expression_precedence(void) {
    ParsedFixture fixture;
    StudySyntaxNodeId block_id;
    StudySyntaxNodeId return_id;
    StudySyntaxNodeId equality_id;
    StudySyntaxNodeId addition_id;
    StudySyntaxNodeId product_id;

    parse_text(
        &fixture,
        "fn main() -> bool { return 1 + 2 * 3 == 7; }"
    );

    block_id = first_function_body(&fixture.tree);
    return_id = child_at(&fixture.tree, block_id, 0U);
    equality_id = child_at(&fixture.tree, return_id, 0U);
    assert(node(&fixture.tree, equality_id)->kind ==
           STUDY_SYNTAX_BINARY_EXPRESSION);
    assert(node(&fixture.tree, equality_id)->as.operation.operator_kind ==
           STUDY_TOKEN_EQUAL_EQUAL);

    addition_id = child_at(&fixture.tree, equality_id, 0U);
    assert(node(&fixture.tree, addition_id)->as.operation.operator_kind ==
           STUDY_TOKEN_PLUS);
    product_id = child_at(&fixture.tree, addition_id, 1U);
    assert(node(&fixture.tree, product_id)->as.operation.operator_kind ==
           STUDY_TOKEN_STAR);

    parsed_fixture_free(&fixture);
}

static void test_unary_grouping_and_left_associativity(void) {
    ParsedFixture fixture;
    StudySyntaxNodeId block_id;
    StudySyntaxNodeId return_id;
    StudySyntaxNodeId outer_minus;
    StudySyntaxNodeId left_minus;
    StudySyntaxNodeId group_id;
    StudySyntaxNodeId bang_id;

    parse_text(
        &fixture,
        "fn main() -> i32 { return 9 - 3 - (-(!false)); }"
    );

    block_id = first_function_body(&fixture.tree);
    return_id = child_at(&fixture.tree, block_id, 0U);
    outer_minus = child_at(&fixture.tree, return_id, 0U);
    assert(node(&fixture.tree, outer_minus)->kind ==
           STUDY_SYNTAX_BINARY_EXPRESSION);
    assert(node(&fixture.tree, outer_minus)->as.operation.operator_kind ==
           STUDY_TOKEN_MINUS);

    left_minus = child_at(&fixture.tree, outer_minus, 0U);
    assert(node(&fixture.tree, left_minus)->kind ==
           STUDY_SYNTAX_BINARY_EXPRESSION);
    assert(node(&fixture.tree, left_minus)->as.operation.operator_kind ==
           STUDY_TOKEN_MINUS);

    group_id = child_at(&fixture.tree, outer_minus, 1U);
    assert(node(&fixture.tree, group_id)->kind ==
           STUDY_SYNTAX_GROUP_EXPRESSION);
    group_id = child_at(&fixture.tree, group_id, 0U);
    assert(node(&fixture.tree, group_id)->kind ==
           STUDY_SYNTAX_UNARY_EXPRESSION);
    assert(node(&fixture.tree, group_id)->as.operation.operator_kind ==
           STUDY_TOKEN_MINUS);
    group_id = child_at(&fixture.tree, group_id, 0U);
    assert(node(&fixture.tree, group_id)->kind ==
           STUDY_SYNTAX_GROUP_EXPRESSION);
    bang_id = child_at(&fixture.tree, group_id, 0U);
    assert(node(&fixture.tree, bang_id)->kind ==
           STUDY_SYNTAX_UNARY_EXPRESSION);
    assert(node(&fixture.tree, bang_id)->as.operation.operator_kind ==
           STUDY_TOKEN_BANG);
    assert(node(
               &fixture.tree,
               child_at(&fixture.tree, bang_id, 0U)
           )->kind == STUDY_SYNTAX_BOOLEAN_EXPRESSION);

    parsed_fixture_free(&fixture);
}

static void test_complete_statement_and_call_shapes(void) {
    static const char text[] =
        "fn choose(value: i32, flag: bool) -> i32 {\n"
        "    var answer: i32 = value;\n"
        "    if flag { answer = answer + 1; }\n"
        "    else { answer = answer - 1; }\n"
        "    while answer < 10 { answer = answer + 1; }\n"
        "    print(answer);\n"
        "    return answer;\n"
        "}\n"
        "fn main() -> i32 { return choose(1, true); }\n";
    ParsedFixture fixture;
    StudySyntaxNodeId first_function;
    StudySyntaxNodeId second_function;
    StudySyntaxNodeId body;
    StudySyntaxNodeId statement;

    parse_text(&fixture, text);

    first_function = child_at(&fixture.tree, fixture.tree.root, 0U);
    second_function = child_at(&fixture.tree, fixture.tree.root, 1U);
    assert(node(&fixture.tree, first_function)->kind == STUDY_SYNTAX_FUNCTION);
    assert(node(&fixture.tree, second_function)->kind == STUDY_SYNTAX_FUNCTION);
    assert(node(&fixture.tree, first_function)->first_child !=
           STUDY_SYNTAX_NODE_NONE);
    assert(node(
               &fixture.tree,
               node(&fixture.tree, first_function)->first_child
           )->kind == STUDY_SYNTAX_PARAMETER);

    body = first_function_body(&fixture.tree);
    statement = child_at(&fixture.tree, body, 0U);
    assert(node(&fixture.tree, statement)->kind ==
           STUDY_SYNTAX_VAR_STATEMENT);
    statement = child_at(&fixture.tree, body, 1U);
    assert(node(&fixture.tree, statement)->kind ==
           STUDY_SYNTAX_IF_STATEMENT);
    assert(child_at(&fixture.tree, statement, 2U) !=
           STUDY_SYNTAX_NODE_NONE);
    statement = child_at(&fixture.tree, body, 2U);
    assert(node(&fixture.tree, statement)->kind ==
           STUDY_SYNTAX_WHILE_STATEMENT);
    statement = child_at(&fixture.tree, body, 3U);
    assert(node(&fixture.tree, statement)->kind ==
           STUDY_SYNTAX_PRINT_STATEMENT);
    statement = child_at(&fixture.tree, body, 4U);
    assert(node(&fixture.tree, statement)->kind ==
           STUDY_SYNTAX_RETURN_STATEMENT);

    body = node(&fixture.tree, second_function)->last_child;
    statement = child_at(&fixture.tree, body, 0U);
    statement = child_at(&fixture.tree, statement, 0U);
    assert(node(&fixture.tree, statement)->kind ==
           STUDY_SYNTAX_CALL_EXPRESSION);
    assert(child_at(&fixture.tree, statement, 0U) !=
           STUDY_SYNTAX_NODE_NONE);
    assert(child_at(&fixture.tree, statement, 1U) !=
           STUDY_SYNTAX_NODE_NONE);

    parsed_fixture_free(&fixture);
}

static void test_parse_error_leaves_empty_output(void) {
    static const char text[] =
        "fn main() -> i32 {\n"
        "    return 42\n"
        "}\n";
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_diagnostic_clear(&diagnostic);
    assert(study_source_copy(
        &source,
        "missing-semicolon.study",
        (const unsigned char *)text,
        strlen(text),
        &diagnostic
    ));
    assert(study_lex(&source, &tokens, &diagnostic));

    assert(!study_parse(&source, &tokens, &tree, &diagnostic));
    assert(tree.nodes == NULL);
    assert(tree.count == 0U);
    assert(tree.capacity == 0U);
    assert(tree.root == STUDY_SYNTAX_NODE_NONE);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_SOURCE);
    assert(diagnostic.has_location);
    assert(diagnostic.line == 3U);
    assert(diagnostic.column == 1U);
    assert(strcmp(
        diagnostic.message,
        "expected ';' after return value, found }"
    ) == 0);

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_nonprogram_token_sequence_is_rejected(void) {
    static const char text[] = "1 true }";
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_diagnostic_clear(&diagnostic);
    assert(study_source_copy(
        &source,
        "tokens.study",
        (const unsigned char *)text,
        strlen(text),
        &diagnostic
    ));
    assert(study_lex(&source, &tokens, &diagnostic));
    assert(!study_parse(&source, &tokens, &tree, &diagnostic));
    assert(diagnostic.line == 1U);
    assert(diagnostic.column == 1U);
    assert(strcmp(
        diagnostic.message,
        "expected 'fn' to begin a function, found integer"
    ) == 0);

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_trailing_parameter_comma_is_rejected(void) {
    static const char text[] =
        "fn broken(value: i32,) -> i32 { return value; }";
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_diagnostic_clear(&diagnostic);
    assert(study_source_copy(
        &source,
        "trailing-comma.study",
        (const unsigned char *)text,
        strlen(text),
        &diagnostic
    ));
    assert(study_lex(&source, &tokens, &diagnostic));
    assert(!study_parse(&source, &tokens, &tree, &diagnostic));
    assert(diagnostic.line == 1U);
    assert(diagnostic.column == 22U);
    assert(strcmp(
        diagnostic.message,
        "expected parameter name, found )"
    ) == 0);
    assert(tree.nodes == NULL);

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_nesting_limit_is_reported(void) {
    char text[1024];
    const char *prefix = "fn main() -> i32 { return ";
    const char *suffix = "0; }";
    size_t length = 0U;
    size_t index;
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyDiagnostic diagnostic;

    memcpy(text + length, prefix, strlen(prefix));
    length += strlen(prefix);
    for (index = 0U; index < 300U; index++) {
        text[length++] = '(';
    }
    text[length++] = '0';
    for (index = 0U; index < 300U; index++) {
        text[length++] = ')';
    }
    memcpy(text + length, suffix, strlen(suffix));
    length += strlen(suffix);

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_diagnostic_clear(&diagnostic);
    assert(study_source_copy(
        &source,
        "deep.study",
        (const unsigned char *)text,
        length,
        &diagnostic
    ));
    assert(study_lex(&source, &tokens, &diagnostic));
    assert(!study_parse(&source, &tokens, &tree, &diagnostic));
    assert(strcmp(
        diagnostic.message,
        "syntax nesting exceeds 256 levels"
    ) == 0);
    assert(tree.nodes == NULL);

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_nonempty_output_is_rejected(void) {
    ParsedFixture fixture;
    StudyDiagnostic diagnostic;
    size_t original_count;

    parse_text(&fixture, "fn main() -> i32 { return 0; }");
    original_count = fixture.tree.count;
    study_diagnostic_clear(&diagnostic);

    assert(!study_parse(
        &fixture.source,
        &fixture.tokens,
        &fixture.tree,
        &diagnostic
    ));
    assert(fixture.tree.count == original_count);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);

    parsed_fixture_free(&fixture);
}

int main(void) {
    test_small_program_tree();
    test_expression_precedence();
    test_unary_grouping_and_left_associativity();
    test_complete_statement_and_call_shapes();
    test_parse_error_leaves_empty_output();
    test_nonprogram_token_sequence_is_rejected();
    test_trailing_parameter_comma_is_rejected();
    test_nesting_limit_is_reported();
    test_nonempty_output_is_rejected();
    return 0;
}
