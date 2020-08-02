#include <errno.h>
#include <stddef.h>

#include "log2fix.h"

#define INV_LOG2_E_Q1DOT31  UINT64_C(0x58b90bfc) // Inverse log base 2 of e
#define INV_LOG2_10_Q1DOT31 UINT64_C(0x268826a1) // Inverse log base 2 of 10

int32_t log2fix (uint32_t x, size_t precision)
{

    int32_t b = 1U << (precision - 1);
    int32_t y = 0;

    if (precision < 1 || precision > 31) {
        errno = EINVAL;
        return INT32_MAX; // indicates an error
    }

    if (x == 0) {
        return INT32_MIN; // represents negative infinity
    }

    while (x < 1U << precision) {
        x <<= 1;
        y -= 1U << precision;
    }

    while (x >= 2U << precision) {
        x >>= 1;
        y += 1U << precision;
    }

    uint64_t z = x;

    for (size_t i = 0; i < precision; i++) {
        z = z * z >> precision;
        if (z >= 2U << precision) {
            z >>= 1;
            y += b;
        }
        b >>= 1;
    }

    return y;
}

int32_t logfix (uint32_t x, size_t precision)
{
    uint64_t t;

    t = log2fix(x, precision) * INV_LOG2_E_Q1DOT31;

    return t >> 31;
}

int32_t log10fix (uint32_t x, size_t precision)
{
    uint64_t t;

    t = log2fix(x, precision) * INV_LOG2_10_Q1DOT31;

    return t >> 31;
}
