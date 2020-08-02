#ifndef LOG2FIX_H
#define LOG2FIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fixed-point base-2 logarithm.
 * 
 * This implementation is based on Clay. S. Turner's fast binary logarithm
 * algorithm[1].
 * 
 * [1] C. S. Turner,  "A Fast Binary Logarithm Algorithm", IEEE Signal
 *     Processing Mag., pp. 124,140, Sep. 2010.
**/
int32_t log2fix (uint32_t x, size_t precision);


/**
 * Fixed-point base-e ("natural") logarithm.
 * 
 * This implementation is based on Clay. S. Turner's fast binary logarithm
 * algorithm[1].
 * 
 * [1] C. S. Turner,  "A Fast Binary Logarithm Algorithm", IEEE Signal
 *     Processing Mag., pp. 124,140, Sep. 2010.
**/
int32_t logfix (uint32_t x, size_t precision);


/**
 * Fixed-point base-10 logarithm.
 * 
 * This implementation is based on Clay. S. Turner's fast binary logarithm
 * algorithm[1].
 * 
 * [1] C. S. Turner,  "A Fast Binary Logarithm Algorithm", IEEE Signal
 *     Processing Mag., pp. 124,140, Sep. 2010.
**/
int32_t log10fix (uint32_t x, size_t precision);

#ifdef __cplusplus
}
#endif

#endif // LOG2FIX_H
