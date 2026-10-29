#include "study/build.h"
#include "study/checked.h"
#include "study/checker.h"
#include "study/diagnostic.h"
#include "study/interpreter.h"
#include "study/ir.h"
#include "study/lexer.h"
#include "study/lower.h"
#include "study/parser.h"
#include "study/source.h"
#include "study/syntax.h"
#include "study/token.h"
#include "study/x86_64.h"
#include "study/x86_64_assembly.h"
#include "study/x86_64_lower.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum {
    STUDY_EXIT_SUCCESS = 0,
    STUDY_EXIT_INPUT_ERROR = 1,
    STUDY_EXIT_USAGE = 2
};

typedef enum StudyIrAction {
    STUDY_IR_ACTION_DUMP_IR,
    STUDY_IR_ACTION_DUMP_X86_64,
    STUDY_IR_ACTION_EMIT_ASSEMBLY,
    STUDY_IR_ACTION_INTERPRET,
    STUDY_IR_ACTION_BUILD
} StudyIrAction;

static void study_print_usage(FILE *stream) {
    (void)fprintf(
        stream,
        "Usage:\n"
        "  studycc dump source INPUT.study\n"
        "  studycc dump tokens INPUT.study\n"
        "  studycc dump ast INPUT.study\n"
        "  studycc dump checked INPUT.study\n"
        "  studycc dump ir INPUT.study\n"
        "  studycc dump x86-64 INPUT.study\n"
        "  studycc emit asm INPUT.study\n"
        "  studycc build INPUT.study -o OUTPUT\n"
        "  studycc check INPUT.study\n"
        "  studycc interpret INPUT.study\n"
        "  studycc --help\n"
    );
}

static int study_dump_source(const char *path) {
    StudySource source;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_diagnostic_clear(&diagnostic);

    if (!study_source_read(&source, path, &diagnostic)) {
        study_diagnostic_print(stderr, &diagnostic);
        return STUDY_EXIT_INPUT_ERROR;
    }

    (void)printf("source: %s\n", source.path);
    (void)printf("bytes: %zu\n", source.length);
    (void)printf("lines: %zu\n", study_source_line_count(&source));
    (void)printf(
        "final line ending: %s\n",
        study_source_has_final_line_ending(&source) ? "yes" : "no"
    );

    study_source_free(&source);
    return STUDY_EXIT_SUCCESS;
}

static void study_print_token(const StudySource *source, const StudyToken *token) {
    size_t spelling_length =
        token->range.end.byte_offset - token->range.start.byte_offset;

    (void)printf(
        "%zu..%zu  %zu:%zu..%zu:%zu  %s",
        token->range.start.byte_offset,
        token->range.end.byte_offset,
        token->range.start.line,
        token->range.start.column,
        token->range.end.line,
        token->range.end.column,
        study_token_kind_name(token->kind)
    );

    if (token->kind != STUDY_TOKEN_EOF) {
        (void)fputs("  `", stdout);
        (void)fwrite(
            source->bytes + token->range.start.byte_offset,
            1U,
            spelling_length,
            stdout
        );
        (void)fputc('`', stdout);
    }

    if (token->kind == STUDY_TOKEN_INTEGER) {
        (void)printf("  value=%" PRIu32, token->integer_value);
    }

    (void)fputc('\n', stdout);
}

static int study_dump_tokens(const char *path) {
    StudySource source;
    StudyTokenList tokens;
    StudyDiagnostic diagnostic;
    size_t index;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_diagnostic_clear(&diagnostic);

    if (!study_source_read(&source, path, &diagnostic)) {
        study_diagnostic_print(stderr, &diagnostic);
        return STUDY_EXIT_INPUT_ERROR;
    }

    if (!study_lex(&source, &tokens, &diagnostic)) {
        study_diagnostic_print(stderr, &diagnostic);
        study_source_free(&source);
        return STUDY_EXIT_INPUT_ERROR;
    }

    for (index = 0U; index < tokens.count; index++) {
        study_print_token(&source, &tokens.items[index]);
    }

    study_token_list_free(&tokens);
    study_source_free(&source);
    return STUDY_EXIT_SUCCESS;
}

static int study_dump_ast(const char *path) {
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyDiagnostic diagnostic;
    int status = STUDY_EXIT_SUCCESS;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_diagnostic_clear(&diagnostic);

    if (!study_source_read(&source, path, &diagnostic) ||
        !study_lex(&source, &tokens, &diagnostic) ||
        !study_parse(&source, &tokens, &tree, &diagnostic)) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (!study_syntax_tree_dump(stdout, &tree, &source, &tokens)) {
        study_diagnostic_set(
            &diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source.path,
            "could not write syntax tree dump"
        );
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    }

    study_syntax_tree_free(&tree);
    study_token_list_free(&tokens);
    study_source_free(&source);
    return status;
}

static int study_check_path(const char *path, bool dump_checked) {
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyCheckedProgram checked;
    StudyDiagnostic diagnostic;
    int status = STUDY_EXIT_SUCCESS;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_checked_program_init(&checked);
    study_diagnostic_clear(&diagnostic);

    if (!study_source_read(&source, path, &diagnostic) ||
        !study_lex(&source, &tokens, &diagnostic) ||
        !study_parse(&source, &tokens, &tree, &diagnostic) ||
        !study_check_program(
            &source,
            &tokens,
            &tree,
            &checked,
            &diagnostic
        )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (dump_checked &&
               !study_checked_program_dump(
                   stdout,
                   &checked,
                   &tree,
                   &source,
                   &tokens
               )) {
        study_diagnostic_set(
            &diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source.path,
            "could not write checked program dump"
        );
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    }

    study_checked_program_free(&checked);
    study_syntax_tree_free(&tree);
    study_token_list_free(&tokens);
    study_source_free(&source);
    return status;
}

static int study_ir_path(
    const char *path,
    StudyIrAction action,
    const char *build_output
) {
    StudySource source;
    StudyTokenList tokens;
    StudySyntaxTree tree;
    StudyCheckedProgram checked;
    StudyIrProgram ir;
    StudyX86Program x86;
    StudyDiagnostic diagnostic;
    uint32_t main_result = 0U;
    int status = STUDY_EXIT_SUCCESS;

    study_source_init(&source);
    study_token_list_init(&tokens);
    study_syntax_tree_init(&tree);
    study_checked_program_init(&checked);
    study_ir_program_init(&ir);
    study_x86_64_program_init(&x86);
    study_diagnostic_clear(&diagnostic);

    if (!study_source_read(&source, path, &diagnostic) ||
        !study_lex(&source, &tokens, &diagnostic) ||
        !study_parse(&source, &tokens, &tree, &diagnostic) ||
        !study_check_program(
            &source,
            &tokens,
            &tree,
            &checked,
            &diagnostic
        ) ||
        !study_lower_to_ir(
            &source,
            &tokens,
            &tree,
            &checked,
            &ir,
            &diagnostic
        )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if ((action == STUDY_IR_ACTION_DUMP_X86_64 ||
                action == STUDY_IR_ACTION_EMIT_ASSEMBLY ||
                action == STUDY_IR_ACTION_BUILD) &&
               !study_lower_to_x86_64(
                   &ir,
                   &source,
                   &x86,
                   &diagnostic
               )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (action == STUDY_IR_ACTION_DUMP_IR &&
               !study_ir_program_dump(
                   stdout,
                   &ir,
                   &source,
                   &tokens
               )) {
        study_diagnostic_set(
            &diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source.path,
            "could not write typed IR dump"
        );
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (action == STUDY_IR_ACTION_DUMP_X86_64 &&
               !study_x86_64_program_dump(
                   stdout,
                   &x86,
                   &source,
                   &diagnostic
               )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (action == STUDY_IR_ACTION_EMIT_ASSEMBLY &&
               !study_x86_64_emit_assembly(
                   stdout,
                   &x86,
                   &source,
                   &diagnostic
               )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (action == STUDY_IR_ACTION_INTERPRET &&
               !study_ir_interpret(
                   &ir,
                   &source,
                   stdout,
                   &main_result,
                   &diagnostic
               )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    } else if (action == STUDY_IR_ACTION_BUILD &&
               !study_build_with_system_tools(
                   &x86,
                   &source,
                   build_output,
                   &diagnostic
               )) {
        study_diagnostic_print(stderr, &diagnostic);
        status = STUDY_EXIT_INPUT_ERROR;
    }

    (void)main_result;
    study_x86_64_program_free(&x86);
    study_ir_program_free(&ir);
    study_checked_program_free(&checked);
    study_syntax_tree_free(&tree);
    study_token_list_free(&tokens);
    study_source_free(&source);
    return status;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        study_print_usage(stdout);
        return STUDY_EXIT_SUCCESS;
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "source") == 0) {
        return study_dump_source(argv[3]);
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "tokens") == 0) {
        return study_dump_tokens(argv[3]);
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "ast") == 0) {
        return study_dump_ast(argv[3]);
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "checked") == 0) {
        return study_check_path(argv[3], true);
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "ir") == 0) {
        return study_ir_path(argv[3], STUDY_IR_ACTION_DUMP_IR, NULL);
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "x86-64") == 0) {
        return study_ir_path(
            argv[3],
            STUDY_IR_ACTION_DUMP_X86_64,
            NULL
        );
    }

    if (argc == 4 && strcmp(argv[1], "emit") == 0 &&
        strcmp(argv[2], "asm") == 0) {
        return study_ir_path(
            argv[3],
            STUDY_IR_ACTION_EMIT_ASSEMBLY,
            NULL
        );
    }

    if (argc == 3 && strcmp(argv[1], "check") == 0) {
        return study_check_path(argv[2], false);
    }

    if (argc == 3 && strcmp(argv[1], "interpret") == 0) {
        return study_ir_path(argv[2], STUDY_IR_ACTION_INTERPRET, NULL);
    }

    if (argc == 5 && strcmp(argv[1], "build") == 0 &&
        strcmp(argv[3], "-o") == 0) {
        return study_ir_path(argv[2], STUDY_IR_ACTION_BUILD, argv[4]);
    }

    study_print_usage(stderr);
    return STUDY_EXIT_USAGE;
}
