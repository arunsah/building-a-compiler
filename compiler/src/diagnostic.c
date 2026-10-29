#include "study/diagnostic.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void study_diagnostic_clear(StudyDiagnostic *diagnostic) {
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->kind = STUDY_DIAGNOSTIC_NONE;
    diagnostic->path = NULL;
    diagnostic->has_location = false;
    diagnostic->byte_offset = 0U;
    diagnostic->line = 0U;
    diagnostic->column = 0U;
    diagnostic->message[0] = '\0';
}

void study_diagnostic_set(
    StudyDiagnostic *diagnostic,
    StudyDiagnosticKind kind,
    const char *path,
    const char *format,
    ...
) {
    va_list arguments;

    if (diagnostic == NULL) {
        return;
    }

    study_diagnostic_clear(diagnostic);
    diagnostic->kind = kind;
    diagnostic->path = path;

    if (format == NULL) {
        return;
    }

    va_start(arguments, format);
    (void)vsnprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        format,
        arguments
    );
    va_end(arguments);
}

void study_diagnostic_set_errno(
    StudyDiagnostic *diagnostic,
    const char *path,
    const char *operation,
    int error_number
) {
    const char *reason = strerror(error_number);

    study_diagnostic_set(
        diagnostic,
        STUDY_DIAGNOSTIC_IO,
        path,
        "%s: %s",
        operation != NULL ? operation : "I/O operation failed",
        reason
    );
}

void study_diagnostic_set_location(
    StudyDiagnostic *diagnostic,
    size_t byte_offset,
    size_t line,
    size_t column
) {
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->has_location = true;
    diagnostic->byte_offset = byte_offset;
    diagnostic->line = line;
    diagnostic->column = column;
}

void study_diagnostic_print(FILE *stream, const StudyDiagnostic *diagnostic) {
    if (stream == NULL || diagnostic == NULL ||
        diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
        return;
    }

    if (diagnostic->path != NULL && diagnostic->has_location) {
        (void)fprintf(
            stream,
            "%s:%zu:%zu: error: %s\n",
            diagnostic->path,
            diagnostic->line,
            diagnostic->column,
            diagnostic->message
        );
    } else if (diagnostic->path != NULL) {
        (void)fprintf(
            stream,
            "%s: error: %s\n",
            diagnostic->path,
            diagnostic->message
        );
    } else {
        (void)fprintf(stream, "studycc: error: %s\n", diagnostic->message);
    }
}
