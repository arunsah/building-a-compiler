#include "add.h"

#include <limits.h>

bool add(int left, int right, int *result)
{
    if ((right > 0 && left > INT_MAX - right) ||
        (right < 0 && left < INT_MIN - right)) {
        return false;
    }

    *result = left + right;
    return true;
}
