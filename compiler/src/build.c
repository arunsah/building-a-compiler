#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700

#include "study/build.h"

#include "study/elf64.h"
#include "study/elf64_executable.h"
#include "study/system_tools.h"
#include "study/x86_64_assembly.h"
#include "study/x86_64_encode.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool study_build_path(
    char *output,
    size_t output_capacity,
    const char *directory,
    const char *name,
    const StudySource *source,
    StudyDiagnostic *diagnostic
) {
    int length = snprintf(
        output,
        output_capacity,
        "%s/%s",
        directory,
        name
    );

    if (length < 0 || (size_t)length >= output_capacity) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source != NULL ? source->path : NULL,
            "temporary build path is too long"
        );
        return false;
    }
    return true;
}

bool study_build_executable(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
) {
    char *temporary_output = NULL;
    size_t temporary_output_length;
    StudyX86Image image;
    StudyElf64Object object;
    StudyElf64Executable executable;
    FILE *stream = NULL;
    int temporary_fd = -1;
    bool success = false;

    study_x86_64_image_init(&image);
    study_elf64_object_init(&object);
    study_elf64_executable_init(&executable);
    study_diagnostic_clear(diagnostic);
    if (program == NULL || source == NULL || output_path == NULL ||
        output_path[0] == '\0') {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source != NULL ? source->path : NULL,
            "build requires a non-empty output path"
        );
        goto cleanup;
    }
    if (!study_x86_64_encode(program, source, &image, diagnostic) ||
        !study_elf64_object_build(
            program,
            &image,
            source,
            &object,
            diagnostic
        ) ||
        !study_elf64_executable_build(
            &object,
            program,
            &image,
            source,
            &executable,
            diagnostic
        )) {
        goto cleanup;
    }
    if (strlen(output_path) > SIZE_MAX - sizeof(".tmp.XXXXXX")) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source->path,
            "output path is too long"
        );
        goto cleanup;
    }
    temporary_output_length = strlen(output_path) + sizeof(".tmp.XXXXXX");
    temporary_output = malloc(temporary_output_length);
    if (temporary_output == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source->path,
            "not enough memory for the output path"
        );
        goto cleanup;
    }
    (void)snprintf(
        temporary_output,
        temporary_output_length,
        "%s.tmp.XXXXXX",
        output_path
    );
    temporary_fd = mkstemp(temporary_output);
    if (temporary_fd < 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not reserve a temporary output",
            errno
        );
        goto cleanup;
    }
    if (fchmod(
            temporary_fd,
            S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP |
                S_IROTH | S_IXOTH
        ) != 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not make the temporary output executable",
            errno
        );
        goto cleanup;
    }
    stream = fdopen(temporary_fd, "wb");
    if (stream == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not open the temporary executable",
            errno
        );
        goto cleanup;
    }
    temporary_fd = -1;
    if (!study_elf64_executable_write(
            stream,
            &executable,
            &object,
            program,
            &image,
            source,
            diagnostic
        )) {
        goto cleanup;
    }
    if (fclose(stream) != 0) {
        stream = NULL;
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not close the temporary executable",
            errno
        );
        goto cleanup;
    }
    stream = NULL;
    if (rename(temporary_output, output_path) != 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not publish the executable",
            errno
        );
        goto cleanup;
    }
    temporary_output[0] = '\0';
    success = true;

cleanup:
    if (stream != NULL && fclose(stream) != 0 &&
        diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not close the temporary executable",
            errno
        );
    }
    if (temporary_fd >= 0) {
        (void)close(temporary_fd);
    }
    if (temporary_output != NULL && temporary_output[0] != '\0') {
        (void)unlink(temporary_output);
    }
    free(temporary_output);
    study_elf64_executable_free(&executable);
    study_elf64_object_free(&object);
    study_x86_64_image_free(&image);
    return success;
}

bool study_build_with_system_tools(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
) {
    char directory_template[] = "/tmp/studycc-build.XXXXXX";
    char assembly_path[96] = {0};
    char object_path[96] = {0};
    char *temporary_output = NULL;
    size_t temporary_output_length;
    FILE *assembly = NULL;
    int temporary_fd = -1;
    bool success = false;

    study_diagnostic_clear(diagnostic);
    if (program == NULL || source == NULL || output_path == NULL ||
        output_path[0] == '\0') {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source != NULL ? source->path : NULL,
            "build requires a non-empty output path"
        );
        return false;
    }

    if (mkdtemp(directory_template) == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not create a temporary build directory",
            errno
        );
        return false;
    }
    if (!study_build_path(
            assembly_path,
            sizeof(assembly_path),
            directory_template,
            "program.s",
            source,
            diagnostic
        ) ||
        !study_build_path(
            object_path,
            sizeof(object_path),
            directory_template,
            "program.o",
            source,
            diagnostic
        )) {
        goto cleanup;
    }

    if (strlen(output_path) > SIZE_MAX - sizeof(".tmp.XXXXXX")) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source->path,
            "output path is too long"
        );
        goto cleanup;
    }
    temporary_output_length =
        strlen(output_path) + sizeof(".tmp.XXXXXX");
    temporary_output = malloc(temporary_output_length);
    if (temporary_output == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source->path,
            "not enough memory for the output path"
        );
        goto cleanup;
    }
    (void)snprintf(
        temporary_output,
        temporary_output_length,
        "%s.tmp.XXXXXX",
        output_path
    );
    temporary_fd = mkstemp(temporary_output);
    if (temporary_fd < 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not reserve a temporary output",
            errno
        );
        goto cleanup;
    }
    if (close(temporary_fd) != 0) {
        temporary_fd = -1;
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not close a temporary output",
            errno
        );
        goto cleanup;
    }
    temporary_fd = -1;

    assembly = fopen(assembly_path, "wb");
    if (assembly == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not create temporary assembly",
            errno
        );
        goto cleanup;
    }
    if (!study_x86_64_emit_assembly(
            assembly,
            program,
            source,
            diagnostic
        )) {
        goto cleanup;
    }
    if (fclose(assembly) != 0) {
        assembly = NULL;
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not close temporary assembly",
            errno
        );
        goto cleanup;
    }
    assembly = NULL;

    if (!study_system_assemble_x86_64(
            assembly_path,
            object_path,
            source->path,
            diagnostic
        ) ||
        !study_system_link_x86_64(
            object_path,
            temporary_output,
            source->path,
            diagnostic
        )) {
        goto cleanup;
    }

    if (rename(temporary_output, output_path) != 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not publish the executable",
            errno
        );
        goto cleanup;
    }
    temporary_output[0] = '\0';
    success = true;

cleanup:
    if (assembly != NULL && fclose(assembly) != 0 &&
        diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not close temporary assembly",
            errno
        );
    }
    if (temporary_fd >= 0) {
        (void)close(temporary_fd);
    }
    if (temporary_output != NULL && temporary_output[0] != '\0') {
        (void)unlink(temporary_output);
    }
    if (object_path[0] != '\0') {
        (void)unlink(object_path);
    }
    if (assembly_path[0] != '\0') {
        (void)unlink(assembly_path);
    }
    (void)rmdir(directory_template);
    free(temporary_output);
    return success;
}

bool study_build_with_objcopy_and_system_linker(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
) {
    char directory_template[] = "/tmp/studycc-encode.XXXXXX";
    char binary_path[96] = {0};
    char object_path[96] = {0};
    char *temporary_output = NULL;
    size_t temporary_output_length;
    StudyX86Image image;
    FILE *binary = NULL;
    int temporary_fd = -1;
    bool success = false;

    study_x86_64_image_init(&image);
    study_diagnostic_clear(diagnostic);
    if (program == NULL || source == NULL || output_path == NULL ||
        output_path[0] == '\0') {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source != NULL ? source->path : NULL,
            "build requires a non-empty output path"
        );
        return false;
    }
    if (!study_x86_64_encode(program, source, &image, diagnostic)) {
        return false;
    }

    if (mkdtemp(directory_template) == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not create a temporary encoding directory",
            errno
        );
        goto cleanup;
    }
    if (!study_build_path(
            binary_path,
            sizeof(binary_path),
            directory_template,
            "program.bin",
            source,
            diagnostic
        ) ||
        !study_build_path(
            object_path,
            sizeof(object_path),
            directory_template,
            "program.o",
            source,
            diagnostic
        )) {
        goto cleanup;
    }

    if (strlen(output_path) > SIZE_MAX - sizeof(".tmp.XXXXXX")) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source->path,
            "output path is too long"
        );
        goto cleanup;
    }
    temporary_output_length = strlen(output_path) + sizeof(".tmp.XXXXXX");
    temporary_output = malloc(temporary_output_length);
    if (temporary_output == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source->path,
            "not enough memory for the output path"
        );
        goto cleanup;
    }
    (void)snprintf(
        temporary_output,
        temporary_output_length,
        "%s.tmp.XXXXXX",
        output_path
    );
    temporary_fd = mkstemp(temporary_output);
    if (temporary_fd < 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not reserve a temporary output",
            errno
        );
        goto cleanup;
    }
    if (close(temporary_fd) != 0) {
        temporary_fd = -1;
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not close a temporary output",
            errno
        );
        goto cleanup;
    }
    temporary_fd = -1;

    binary = fopen(binary_path, "wb");
    if (binary == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not create the temporary code image",
            errno
        );
        goto cleanup;
    }
    if (!study_x86_64_image_write(binary, &image, source, diagnostic)) {
        goto cleanup;
    }
    if (fclose(binary) != 0) {
        binary = NULL;
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not close the temporary code image",
            errno
        );
        goto cleanup;
    }
    binary = NULL;

    if (!study_system_wrap_x86_64_binary(
            directory_template,
            "program.bin",
            "program.o",
            source->path,
            diagnostic
        ) ||
        !study_system_link_x86_64(
            object_path,
            temporary_output,
            source->path,
            diagnostic
        )) {
        goto cleanup;
    }

    if (rename(temporary_output, output_path) != 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not publish the executable",
            errno
        );
        goto cleanup;
    }
    temporary_output[0] = '\0';
    success = true;

cleanup:
    if (binary != NULL && fclose(binary) != 0 &&
        diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not close the temporary code image",
            errno
        );
    }
    if (temporary_fd >= 0) {
        (void)close(temporary_fd);
    }
    if (temporary_output != NULL && temporary_output[0] != '\0') {
        (void)unlink(temporary_output);
    }
    if (object_path[0] != '\0') {
        (void)unlink(object_path);
    }
    if (binary_path[0] != '\0') {
        (void)unlink(binary_path);
    }
    (void)rmdir(directory_template);
    free(temporary_output);
    study_x86_64_image_free(&image);
    return success;
}

bool study_build_with_elf_and_system_linker(
    const StudyX86Program *program,
    const StudySource *source,
    const char *output_path,
    StudyDiagnostic *diagnostic
) {
    char directory_template[] = "/tmp/studycc-elf.XXXXXX";
    char object_path[96] = {0};
    char *temporary_output = NULL;
    size_t temporary_output_length;
    StudyX86Image image;
    StudyElf64Object object;
    FILE *object_stream = NULL;
    int temporary_fd = -1;
    bool success = false;

    study_x86_64_image_init(&image);
    study_elf64_object_init(&object);
    study_diagnostic_clear(diagnostic);
    if (program == NULL || source == NULL || output_path == NULL ||
        output_path[0] == '\0') {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source != NULL ? source->path : NULL,
            "build requires a non-empty output path"
        );
        return false;
    }
    if (!study_x86_64_encode(program, source, &image, diagnostic) ||
        !study_elf64_object_build(
            program,
            &image,
            source,
            &object,
            diagnostic
        )) {
        goto cleanup;
    }

    if (mkdtemp(directory_template) == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not create a temporary ELF directory",
            errno
        );
        goto cleanup;
    }
    if (!study_build_path(
            object_path,
            sizeof(object_path),
            directory_template,
            "program.o",
            source,
            diagnostic
        )) {
        goto cleanup;
    }

    if (strlen(output_path) > SIZE_MAX - sizeof(".tmp.XXXXXX")) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_USAGE,
            source->path,
            "output path is too long"
        );
        goto cleanup;
    }
    temporary_output_length = strlen(output_path) + sizeof(".tmp.XXXXXX");
    temporary_output = malloc(temporary_output_length);
    if (temporary_output == NULL) {
        study_diagnostic_set(
            diagnostic,
            STUDY_DIAGNOSTIC_INTERNAL,
            source->path,
            "not enough memory for the output path"
        );
        goto cleanup;
    }
    (void)snprintf(
        temporary_output,
        temporary_output_length,
        "%s.tmp.XXXXXX",
        output_path
    );
    temporary_fd = mkstemp(temporary_output);
    if (temporary_fd < 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not reserve a temporary output",
            errno
        );
        goto cleanup;
    }
    if (close(temporary_fd) != 0) {
        temporary_fd = -1;
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not close a temporary output",
            errno
        );
        goto cleanup;
    }
    temporary_fd = -1;

    object_stream = fopen(object_path, "wb");
    if (object_stream == NULL) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not create the temporary ELF object",
            errno
        );
        goto cleanup;
    }
    if (!study_elf64_object_write(
            object_stream,
            &object,
            program,
            &image,
            source,
            diagnostic
        )) {
        goto cleanup;
    }
    if (fclose(object_stream) != 0) {
        object_stream = NULL;
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not close the temporary ELF object",
            errno
        );
        goto cleanup;
    }
    object_stream = NULL;

    if (!study_system_link_x86_64(
            object_path,
            temporary_output,
            source->path,
            diagnostic
        )) {
        goto cleanup;
    }
    if (rename(temporary_output, output_path) != 0) {
        study_diagnostic_set_errno(
            diagnostic,
            output_path,
            "could not publish the executable",
            errno
        );
        goto cleanup;
    }
    temporary_output[0] = '\0';
    success = true;

cleanup:
    if (object_stream != NULL && fclose(object_stream) != 0 &&
        diagnostic->kind == STUDY_DIAGNOSTIC_NONE) {
        study_diagnostic_set_errno(
            diagnostic,
            source->path,
            "could not close the temporary ELF object",
            errno
        );
    }
    if (temporary_fd >= 0) {
        (void)close(temporary_fd);
    }
    if (temporary_output != NULL && temporary_output[0] != '\0') {
        (void)unlink(temporary_output);
    }
    if (object_path[0] != '\0') {
        (void)unlink(object_path);
    }
    (void)rmdir(directory_template);
    free(temporary_output);
    study_elf64_object_free(&object);
    study_x86_64_image_free(&image);
    return success;
}
