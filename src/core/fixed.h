#ifndef PSXE_FIXED_H
#define PSXE_FIXED_H

#include "psxe/types.h"

/* Normalizes (x, y, z) to length ONE; returns the input length (0 = degenerate,
 * out untouched). Handles any int32 range. */
int32_t psxe_vector_normalize(int32_t x, int32_t y, int32_t z, SVECTOR *out);

/* out = m * v (rows of m in 4.12), computed on the CPU. */
void psxe_matrix_apply(const MATRIX *m, int32_t x, int32_t y, int32_t z, VECTOR *out);

#endif
