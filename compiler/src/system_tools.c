#include "study/system_tools.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool study_run_system_tool(
    const char *tool_name,
    char *const arguments[],
    const char *working_directory,
    const char *source_path,
    StudyDiagnostic *diagnostic
) {
    pid_t child;
    int status;

    child = fork();
    if (child < 0) {
        study_diagnostic_set_errno(
            diagnostic,
            source_path,
            "could not start a system tool",
            errno
        );
        return false;
    }
    if (child == 0) {
        if (working_directory != NULL && chdir(working_directory) != 0) {
            _exit(125);
        }
        execvp(tool_name, arguments);
        _exit(errno == ENOENT ? 127 : 126);
    }

    for (;;) {
        if (waitpid(child, &status, 0) >= 0) {
            break;
        }
        if (errno != EINTR) {
            study_diagnostic_set_errno(
                diagnostic,
                source_path,
                "could not wait for a system tool",
                errno
            );
            return false;
        }
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return true;
    }
    if (WIFEXITED(status)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            source_path,
            "system %s adapter exited with status %d",
            tool_name,
            WEXITSTATUS(status)
        );
    } else if (WIFSIGNALED(status)) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            source_path,
            "system %s adapter was terminated by signal %d",
            tool_name,
            WTERMSIG(status)
        );
    } else {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_IO,
            source_path,
            "system %s adapter did not finish normally",
            tool_name
        );
    }
    return false;
}

bool study_system_assemble_x86_64(
    const char *assembly_path,
    const char *object_path,
    const char *source_path,
    StudyDiagnostic *diagnostic
) {
    char *arguments[] = {
        (char *)"as",
        (char *)"--64",
        (char *)"-o",
        (char *)object_path,
        (char *)assembly_path,
        NULL
    };

    return study_run_system_tool(
        "as",
        arguments,
        NULL,
        source_path,
        diagnostic
    );
}

bool study_system_wrap_x86_64_binary(
    const char *directory,
    const char *binary_name,
    const char *object_name,
    const char *source_path,
    StudyDiagnostic *diagnostic
) {
    char *arguments[] = {
        (char *)"objcopy",
        (char *)"-I",
        (char *)"binary",
        (char *)"-O",
        (char *)"elf64-x86-64",
        (char *)"-B",
        (char *)"i386:x86-64",
        (char *)"--rename-section",
        (char *)".data=.text,alloc,load,readonly,code,contents",
        (char *)"--redefine-sym",
        (char *)"_binary_program_bin_start=_start",
        (char *)"--redefine-sym",
        (char *)"_binary_program_bin_end=_study_image_end",
        (char *)"--redefine-sym",
        (char *)"_binary_program_bin_size=_study_image_size",
        (char *)binary_name,
        (char *)object_name,
        NULL
    };

    return study_run_system_tool(
        "objcopy",
        arguments,
        directory,
        source_path,
        diagnostic
    );
}

bool study_system_link_x86_64(
    const char *object_path,
    const char *executable_path,
    const char *source_path,
    StudyDiagnostic *diagnostic
) {
    char *arguments[] = {
        (char *)"ld",
        (char *)"-m",
        (char *)"elf_x86_64",
        (char *)"-static",
        (char *)"-o",
        (char *)executable_path,
        (char *)object_path,
        NULL
    };

    return study_run_system_tool(
        "ld",
        arguments,
        NULL,
        source_path,
        diagnostic
    );
}
