#ifndef PSXE_TYPES_H
#define PSXE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include <psxgte.h>

#define PSXE_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define PSXE_ANGLE_FULL_TURN 4096
#define PSXE_FIXED_ONE ONE

typedef struct psxe_color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} psxe_color;

typedef struct psxe_transform {
    VECTOR position;
    SVECTOR rotation;
    VECTOR scale;
} psxe_transform;

static inline psxe_transform psxe_transform_identity(void) {
    psxe_transform value = {
        .position = { 0, 0, 0 },
        .rotation = { 0, 0, 0, 0 },
        .scale = { ONE, ONE, ONE }
    };
    return value;
}

#endif
