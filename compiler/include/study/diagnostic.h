#ifndef STUDY_DIAGNOSTIC_H
#define STUDY_DIAGNOSTIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { STUDY_DIAGNOSTIC_MESSAGE_CAPACITY = 256 };

#if defined(__GNUC__) || defined(__clang__)
#define STUDY_PRINTF_FORMAT(format_index, arguments_index) \
    __attribute__((format(printf, format_index, arguments_index)))
#else
#define STUDY_PRINTF_FORMAT(format_index, arguments_index)
#endif

typedef enum StudyDiagnosticKind {
    STUDY_DIAGNOSTIC_NONE = 0,
    STUDY_DIAGNOSTIC_USAGE,
    STUDY_DIAGNOSTIC_IO,
    STUDY_DIAGNOSTIC_SOURCE,
    STUDY_DIAGNOSTIC_INTERNAL
} StudyDiagnosticKind;

typedef struct StudyDiagnostic {
    StudyDiagnosticKind kind;
    const char *path;
    bool has_location;
    size_t byte_offset;
    size_t line;
    size_t column;
    char message[STUDY_DIAGNOSTIC_MESSAGE_CAPACITY];
} StudyDiagnostic;

/*
 * A diagnostic owns message but borrows path. The path must remain valid until
 * the diagnostic has been printed or otherwise consumed.
 */
void study_diagnostic_clear(StudyDiagnostic *diagnostic);

void study_diagnostic_set(
    StudyDiagnostic *diagnostic,
    StudyDiagnosticKind kind,
    const char *path,
    const char *format,
    ...
) STUDY_PRINTF_FORMAT(4, 5);

void study_diagnostic_set_errno(
    StudyDiagnostic *diagnostic,
    const char *path,
    const char *operation,
    int error_number
);

void study_diagnostic_set_location(
    StudyDiagnostic *diagnostic,
    size_t byte_offset,
    size_t line,
    size_t column
);

void study_diagnostic_print(FILE *stream, const StudyDiagnostic *diagnostic);

#endif
