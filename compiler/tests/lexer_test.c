#include "study/diagnostic.h"
#include "study/lexer.h"
#include "study/source.h"
#include "study/token.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void copy_source(
    StudySource *source,
    const unsigned char *bytes,
    size_t length
) {
    StudyDiagnostic diagnostic;

    study_source_init(source);
    study_diagnostic_clear(&diagnostic);
    assert(study_source_copy(
        source,
        "memory.study",
        bytes,
        length,
        &diagnostic
    ));
}

static StudyTokenList lex_source(const StudySource *source) {
    StudyTokenList tokens;
    StudyDiagnostic diagnostic;

    study_token_list_init(&tokens);
    study_diagnostic_clear(&diagnostic);
    assert(study_lex(source, &tokens, &diagnostic));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);
    return tokens;
}

static void test_program_tokens_and_source_ranges(void) {
    static const unsigned char bytes[] =
        "fn main() -> i32 {\r\n"
        "\treturn 42; // value\r"
        "}\n";
    static const StudyTokenKind expected[] = {
        STUDY_TOKEN_FN,
        STUDY_TOKEN_IDENTIFIER,
        STUDY_TOKEN_LEFT_PAREN,
        STUDY_TOKEN_RIGHT_PAREN,
        STUDY_TOKEN_ARROW,
        STUDY_TOKEN_I32,
        STUDY_TOKEN_LEFT_BRACE,
        STUDY_TOKEN_RETURN,
        STUDY_TOKEN_INTEGER,
        STUDY_TOKEN_SEMICOLON,
        STUDY_TOKEN_RIGHT_BRACE,
        STUDY_TOKEN_EOF
    };
    StudySource source;
    StudyTokenList tokens;
    size_t index;

    copy_source(&source, bytes, sizeof(bytes) - 1U);
    tokens = lex_source(&source);

    assert(tokens.count == sizeof(expected) / sizeof(expected[0]));
    for (index = 0U; index < tokens.count; index++) {
        assert(tokens.items[index].kind == expected[index]);
    }

    assert(tokens.items[0].range.start.byte_offset == 0U);
    assert(tokens.items[0].range.start.line == 1U);
    assert(tokens.items[0].range.start.column == 1U);
    assert(tokens.items[0].range.end.byte_offset == 2U);
    assert(tokens.items[0].range.end.column == 3U);

    assert(tokens.items[7].range.start.byte_offset == 21U);
    assert(tokens.items[7].range.start.line == 2U);
    assert(tokens.items[7].range.start.column == 2U);
    assert(tokens.items[7].range.end.column == 8U);

    assert(tokens.items[8].kind == STUDY_TOKEN_INTEGER);
    assert(tokens.items[8].integer_value == UINT32_C(42));
    assert(tokens.items[8].range.start.byte_offset == 28U);
    assert(tokens.items[8].range.start.line == 2U);
    assert(tokens.items[8].range.start.column == 9U);
    assert(tokens.items[8].range.end.column == 11U);

    assert(tokens.items[10].range.start.byte_offset == 41U);
    assert(tokens.items[10].range.start.line == 3U);
    assert(tokens.items[10].range.start.column == 1U);

    assert(tokens.items[11].range.start.byte_offset == sizeof(bytes) - 1U);
    assert(tokens.items[11].range.start.line == 4U);
    assert(tokens.items[11].range.start.column == 1U);
    assert(tokens.items[11].range.end.byte_offset == sizeof(bytes) - 1U);

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_every_keyword_and_punctuation(void) {
    static const unsigned char bytes[] =
        "fn var if else while return print true false i32 bool "
        "( ) { } , : ; -> = == ! != < <= > >= + - * name fnx";
    static const StudyTokenKind expected[] = {
        STUDY_TOKEN_FN,
        STUDY_TOKEN_VAR,
        STUDY_TOKEN_IF,
        STUDY_TOKEN_ELSE,
        STUDY_TOKEN_WHILE,
        STUDY_TOKEN_RETURN,
        STUDY_TOKEN_PRINT,
        STUDY_TOKEN_TRUE,
        STUDY_TOKEN_FALSE,
        STUDY_TOKEN_I32,
        STUDY_TOKEN_BOOL,
        STUDY_TOKEN_LEFT_PAREN,
        STUDY_TOKEN_RIGHT_PAREN,
        STUDY_TOKEN_LEFT_BRACE,
        STUDY_TOKEN_RIGHT_BRACE,
        STUDY_TOKEN_COMMA,
        STUDY_TOKEN_COLON,
        STUDY_TOKEN_SEMICOLON,
        STUDY_TOKEN_ARROW,
        STUDY_TOKEN_EQUAL,
        STUDY_TOKEN_EQUAL_EQUAL,
        STUDY_TOKEN_BANG,
        STUDY_TOKEN_BANG_EQUAL,
        STUDY_TOKEN_LESS,
        STUDY_TOKEN_LESS_EQUAL,
        STUDY_TOKEN_GREATER,
        STUDY_TOKEN_GREATER_EQUAL,
        STUDY_TOKEN_PLUS,
        STUDY_TOKEN_MINUS,
        STUDY_TOKEN_STAR,
        STUDY_TOKEN_IDENTIFIER,
        STUDY_TOKEN_IDENTIFIER,
        STUDY_TOKEN_EOF
    };
    StudySource source;
    StudyTokenList tokens;
    size_t index;

    copy_source(&source, bytes, sizeof(bytes) - 1U);
    tokens = lex_source(&source);

    assert(tokens.count == sizeof(expected) / sizeof(expected[0]));
    for (index = 0U; index < tokens.count; index++) {
        assert(tokens.items[index].kind == expected[index]);
    }

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_empty_source_contains_only_eof(void) {
    static const unsigned char bytes[] = "";
    StudySource source;
    StudyTokenList tokens;

    copy_source(&source, bytes, 0U);
    tokens = lex_source(&source);

    assert(tokens.count == 1U);
    assert(tokens.items[0].kind == STUDY_TOKEN_EOF);
    assert(tokens.items[0].range.start.byte_offset == 0U);
    assert(tokens.items[0].range.start.line == 1U);
    assert(tokens.items[0].range.start.column == 1U);
    assert(tokens.items[0].range.end.byte_offset == 0U);

    study_token_list_free(&tokens);
    study_source_free(&source);
}

static void test_integer_boundaries(void) {
    static const unsigned char valid[] = "0 2147483647";
    static const unsigned char invalid[] = "2147483648";
    StudySource source;
    StudyTokenList tokens;
    StudyDiagnostic diagnostic;

    copy_source(&source, valid, sizeof(valid) - 1U);
    tokens = lex_source(&source);
    assert(tokens.count == 3U);
    assert(tokens.items[0].integer_value == UINT32_C(0));
    assert(tokens.items[1].integer_value == UINT32_C(2147483647));
    study_token_list_free(&tokens);
    study_source_free(&source);

    copy_source(&source, invalid, sizeof(invalid) - 1U);
    study_token_list_init(&tokens);
    study_diagnostic_clear(&diagnostic);
    assert(!study_lex(&source, &tokens, &diagnostic));
    assert(tokens.items == NULL);
    assert(tokens.count == 0U);
    assert(tokens.capacity == 0U);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_SOURCE);
    assert(diagnostic.has_location);
    assert(diagnostic.byte_offset == 0U);
    assert(diagnostic.line == 1U);
    assert(diagnostic.column == 1U);
    assert(strcmp(
        diagnostic.message,
        "integer literal exceeds 2147483647"
    ) == 0);
    study_source_free(&source);
}

static void test_invalid_byte_reports_its_position(void) {
    static const unsigned char bytes[] = {'a', '\n', 0U, 'b'};
    StudySource source;
    StudyTokenList tokens;
    StudyDiagnostic diagnostic;

    copy_source(&source, bytes, sizeof(bytes));
    study_token_list_init(&tokens);
    study_diagnostic_clear(&diagnostic);

    assert(!study_lex(&source, &tokens, &diagnostic));
    assert(tokens.items == NULL);
    assert(tokens.count == 0U);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_SOURCE);
    assert(diagnostic.has_location);
    assert(diagnostic.byte_offset == 2U);
    assert(diagnostic.line == 2U);
    assert(diagnostic.column == 1U);
    assert(strcmp(diagnostic.message, "unsupported byte 0x00") == 0);

    study_source_free(&source);
}

static void test_nonempty_output_is_rejected(void) {
    static const unsigned char bytes[] = "name";
    StudySource source;
    StudyTokenList tokens;
    StudyDiagnostic diagnostic;

    copy_source(&source, bytes, sizeof(bytes) - 1U);
    tokens = lex_source(&source);
    study_diagnostic_clear(&diagnostic);

    assert(!study_lex(&source, &tokens, &diagnostic));
    assert(tokens.count == 2U);
    assert(tokens.items[0].kind == STUDY_TOKEN_IDENTIFIER);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);

    study_token_list_free(&tokens);
    assert(tokens.items == NULL);
    assert(tokens.count == 0U);
    assert(tokens.capacity == 0U);
    study_token_list_free(&tokens);
    study_source_free(&source);
}

int main(void) {
    test_program_tokens_and_source_ranges();
    test_every_keyword_and_punctuation();
    test_empty_source_contains_only_eof();
    test_integer_boundaries();
    test_invalid_byte_reports_its_position();
    test_nonempty_output_is_rejected();
    return 0;
}
