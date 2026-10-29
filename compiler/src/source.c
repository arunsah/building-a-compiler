#include "study/source.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { STUDY_INITIAL_SOURCE_CAPACITY = 4096 };

static bool study_source_is_empty(const StudySource *source) {
    return source != NULL && source->path == NULL && source->bytes == NULL &&
           source->length == 0U;
}

static char *study_copy_c_string(const char *text) {
    size_t length;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text);
    if (length == SIZE_MAX) {
        return NULL;
    }

    copy = malloc(length + 1U);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, text, length + 1U);
    return copy;
}

static bool study_source_output_is_ready(
    const StudySource *output,
    const char *path,
    StudyDiagnostic *diagnostic
) {
    if (output == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            path,
            "source output is null"
        );
        return false;
    }

    if (!study_source_is_empty(output)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            path,
            "source output must be initialized and empty"
        );
        return false;
    }

    return true;
}

void study_source_init(StudySource *source) {
    if (source == NULL) {
        return;
    }

    source->path = NULL;
    source->bytes = NULL;
    source->length = 0U;
}

void study_source_free(StudySource *source) {
    if (source == NULL) {
        return;
    }

    free(source->path);
    free(source->bytes);
    study_source_init(source);
}

bool study_source_copy(
    StudySource *output,
    const char *path,
    const unsigned char *bytes,
    size_t length,
    StudyDiagnostic *diagnostic
) {
    StudySource result = {0};

    study_diagnostic_clear(diagnostic);

    if (!study_source_output_is_ready(output, path, diagnostic)) {
        return false;
    }

    if (path == NULL || (bytes == NULL && length != 0U)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            path,
            "source input is invalid"
        );
        return false;
    }

    if (length == SIZE_MAX) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            path,
            "source is too large to store"
        );
        return false;
    }

    result.path = study_copy_c_string(path);
    result.bytes = malloc(length + 1U);
    if (result.path == NULL || result.bytes == NULL) {
        study_source_free(&result);
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            path,
            "not enough memory to copy source"
        );
        return false;
    }

    if (length != 0U) {
        memcpy(result.bytes, bytes, length);
    }
    result.bytes[length] = 0U;
    result.length = length;
    *output = result;
    return true;
}

bool study_source_read(
    StudySource *output,
    const char *path,
    StudyDiagnostic *diagnostic
) {
    StudySource result = {0};
    FILE *input = NULL;
    size_t capacity = STUDY_INITIAL_SOURCE_CAPACITY;
    int saved_error = 0;

    study_diagnostic_clear(diagnostic);

    if (!study_source_output_is_ready(output, path, diagnostic)) {
        return false;
    }

    if (path == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            NULL,
            "source path is null"
        );
        return false;
    }

    input = fopen(path, "rb");
    if (input == NULL) {
        saved_error = errno;
        study_diagnostic_set_errno(
            diagnostic,
            path,
            "could not open source",
            saved_error
        );
        return false;
    }

    result.path = study_copy_c_string(path);
    result.bytes = malloc(capacity + 1U);
    if (result.path == NULL || result.bytes == NULL) {
        study_source_free(&result);
        (void)fclose(input);
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            path,
            "not enough memory to read source"
        );
        return false;
    }

    for (;;) {
        size_t available = capacity - result.length;
        size_t amount;

        if (available == 0U) {
            unsigned char *grown;
            size_t next_capacity;

            if (capacity > (SIZE_MAX - 1U) / 2U) {
                study_source_free(&result);
                (void)fclose(input);
                study_diagnostic_set(
                    diagnostic,
                    STUDY_DIAGNOSTIC_IO,
                    path,
                    "source is too large to store"
                );
                return false;
            }

            next_capacity = capacity * 2U;
            grown = realloc(result.bytes, next_capacity + 1U);
            if (grown == NULL) {
                study_source_free(&result);
                (void)fclose(input);
                study_diagnostic_set(
                    diagnostic,
                    STUDY_DIAGNOSTIC_IO,
                    path,
                    "not enough memory to grow source buffer"
                );
                return false;
            }

            result.bytes = grown;
            capacity = next_capacity;
            available = capacity - result.length;
        }

        errno = 0;
        amount = fread(result.bytes + result.length, 1U, available, input);
        result.length += amount;

        if (amount != 0U) {
            continue;
        }

        if (ferror(input) != 0) {
            saved_error = errno != 0 ? errno : EIO;
            study_source_free(&result);
            (void)fclose(input);
            study_diagnostic_set_errno(
                diagnostic,
                path,
                "could not read source",
                saved_error
            );
            return false;
        }

        break;
    }

    if (fclose(input) != 0) {
        saved_error = errno;
        study_source_free(&result);
        study_diagnostic_set_errno(
            diagnostic,
            path,
            "could not close source after reading",
            saved_error
        );
        return false;
    }

    result.bytes[result.length] = 0U;
    *output = result;
    return true;
}

size_t study_source_line_count(const StudySource *source) {
    size_t lines = 1U;
    size_t index = 0U;

    if (source == NULL) {
        return 0U;
    }

    while (index < source->length) {
        if (source->bytes[index] == (unsigned char)'\r') {
            lines += 1U;
            if (index + 1U < source->length &&
                source->bytes[index + 1U] == (unsigned char)'\n') {
                index += 1U;
            }
        } else if (source->bytes[index] == (unsigned char)'\n') {
            lines += 1U;
        }
        index += 1U;
    }

    return lines;
}

bool study_source_has_final_line_ending(const StudySource *source) {
    unsigned char final_byte;

    if (source == NULL || source->length == 0U) {
        return false;
    }

    final_byte = source->bytes[source->length - 1U];
    return final_byte == (unsigned char)'\n' ||
           final_byte == (unsigned char)'\r';
}
