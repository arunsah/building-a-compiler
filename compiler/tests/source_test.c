#include "study/diagnostic.h"
#include "study/source.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void test_source_copies_borrowed_input(void) {
    char path[] = "memory.study";
    unsigned char bytes[] = {'a', 0U, 'b', '\n'};
    StudySource source;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_diagnostic_clear(&diagnostic);

    assert(study_source_copy(
        &source,
        path,
        bytes,
        sizeof(bytes),
        &diagnostic
    ));
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_NONE);

    path[0] = 'X';
    bytes[0] = 'z';

    assert(strcmp(source.path, "memory.study") == 0);
    assert(source.length == 4U);
    assert(source.bytes[0] == (unsigned char)'a');
    assert(source.bytes[1] == 0U);
    assert(source.bytes[2] == (unsigned char)'b');
    assert(source.bytes[3] == (unsigned char)'\n');
    assert(source.bytes[4] == 0U);
    assert(study_source_line_count(&source) == 2U);
    assert(study_source_has_final_line_ending(&source));

    study_source_free(&source);
    assert(source.path == NULL);
    assert(source.bytes == NULL);
    assert(source.length == 0U);
    study_source_free(&source);
}

static void test_line_endings(void) {
    static const unsigned char bytes[] = "a\r\nb\rc\n";
    StudySource source;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_diagnostic_clear(&diagnostic);

    assert(study_source_copy(
        &source,
        "lines.study",
        bytes,
        sizeof(bytes) - 1U,
        &diagnostic
    ));
    assert(study_source_line_count(&source) == 4U);
    assert(study_source_has_final_line_ending(&source));
    study_source_free(&source);
}

static void test_invalid_input_leaves_empty_output(void) {
    StudySource source;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_diagnostic_clear(&diagnostic);

    assert(!study_source_copy(
        &source,
        "invalid.study",
        NULL,
        1U,
        &diagnostic
    ));
    assert(source.path == NULL);
    assert(source.bytes == NULL);
    assert(source.length == 0U);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);
}

static void test_nonempty_output_is_rejected(void) {
    static const unsigned char first[] = "first";
    static const unsigned char second[] = "second";
    StudySource source;
    StudyDiagnostic diagnostic;

    study_source_init(&source);
    study_diagnostic_clear(&diagnostic);

    assert(study_source_copy(
        &source,
        "first.study",
        first,
        sizeof(first) - 1U,
        &diagnostic
    ));
    assert(!study_source_copy(
        &source,
        "second.study",
        second,
        sizeof(second) - 1U,
        &diagnostic
    ));
    assert(strcmp(source.path, "first.study") == 0);
    assert(source.length == sizeof(first) - 1U);
    assert(diagnostic.kind == STUDY_DIAGNOSTIC_INTERNAL);
    study_source_free(&source);
}

int main(void) {
    test_source_copies_borrowed_input();
    test_line_endings();
    test_invalid_input_leaves_empty_output();
    test_nonempty_output_is_rejected();
    return 0;
}
