#include "add.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define EXPECTED_ARGUMENTS 3

static bool parse_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

int main(int argc, char **argv)
{
    if (argc != EXPECTED_ARGUMENTS) {
        fprintf(stderr, "Usage: %s LEFT RIGHT\n", argv[0]);
        return 2;
    }

    int left;
    int right;
    if (!parse_int(argv[1], &left) || !parse_int(argv[2], &right)) {
        fprintf(stderr, "Both operands must be integers representable by int.\n");
        return 2;
    }

    int result;
    if (!add(left, right, &result)) {
        fprintf(stderr, "The result is outside the range of int.\n");
        return 1;
    }

    printf("%d + %d = %d\n", left, right, result);
    return 0;
}
