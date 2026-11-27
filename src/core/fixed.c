#include "fixed.h"

#include <stddef.h>

/* Direction precision target: scaled components land in 0x2000..0x3fff so
 * squared sums fit in 32 bits and unit vectors keep ~13 bits. */
#define PSXE_NORMALIZE_HIGH 0x3fffu
#define PSXE_NORMALIZE_LOW 0x2000u

static uint32_t psxe_isqrt(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1u << 30;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (value >= (result + bit)) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint32_t psxe_abs32(int32_t value) {
    return (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

int32_t psxe_vector_normalize(int32_t x, int32_t y, int32_t z, SVECTOR *out) {
    uint32_t largest = psxe_abs32(x);
    int32_t sx;
    int32_t sy;
    int32_t sz;
    int shift = 0; /* > 0: scaled down, < 0: scaled up */
    uint32_t length;
    int64_t real_length;

    if (psxe_abs32(y) > largest) {
        largest = psxe_abs32(y);
    }
    if (psxe_abs32(z) > largest) {
        largest = psxe_abs32(z);
    }
    if (largest == 0u) {
        return 0;
    }
    while (largest > PSXE_NORMALIZE_HIGH) {
        largest >>= 1;
        ++shift;
    }
    while (largest < PSXE_NORMALIZE_LOW) {
        largest <<= 1;
        --shift;
    }
    if (shift >= 0) {
        sx = (int32_t)((int64_t)x >> shift);
        sy = (int32_t)((int64_t)y >> shift);
        sz = (int32_t)((int64_t)z >> shift);
    } else {
        sx = x * (1 << -shift);
        sy = y * (1 << -shift);
        sz = z * (1 << -shift);
    }

    length = psxe_isqrt((uint32_t)(sx * sx) + (uint32_t)(sy * sy) + (uint32_t)(sz * sz));
    if (length == 0u) {
        return 0;
    }
    if (out != NULL) {
        out->vx = (int16_t)((sx * ONE) / (int32_t)length);
        out->vy = (int16_t)((sy * ONE) / (int32_t)length);
        out->vz = (int16_t)((sz * ONE) / (int32_t)length);
        out->pad = 0;
    }

    real_length = (shift >= 0) ? ((int64_t)length << shift) : ((int64_t)length >> -shift);
    if (real_length < 1) {
        real_length = 1;
    }
    return (real_length > INT32_MAX) ? INT32_MAX : (int32_t)real_length;
}

void psxe_matrix_apply(const MATRIX *m, int32_t x, int32_t y, int32_t z, VECTOR *out) {
    int row;
    int32_t *result[3] = { &out->vx, &out->vy, &out->vz };

    for (row = 0; row < 3; ++row) {
        int64_t sum = ((int64_t)m->m[row][0] * x) +
                      ((int64_t)m->m[row][1] * y) +
                      ((int64_t)m->m[row][2] * z);
        *result[row] = (int32_t)(sum >> 12);
    }
}
