#include "study/diagnostic.h"
#include "study/source.h"

#include <stdio.h>
#include <string.h>

enum {
    STUDY_EXIT_SUCCESS = 0,
    STUDY_EXIT_INPUT_ERROR = 1,
    STUDY_EXIT_USAGE = 2
};

static void study_print_usage(FILE *stream) {
    (void)fprintf(
        stream,
        "Usage:\n"
        "  studycc dump source INPUT.study\n"
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

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        study_print_usage(stdout);
        return STUDY_EXIT_SUCCESS;
    }

    if (argc == 4 && strcmp(argv[1], "dump") == 0 &&
        strcmp(argv[2], "source") == 0) {
        return study_dump_source(argv[3]);
    }

    study_print_usage(stderr);
    return STUDY_EXIT_USAGE;
}
