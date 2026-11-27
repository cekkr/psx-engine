#ifndef PSXE_PROFILE_INTERNAL_H
#define PSXE_PROFILE_INTERNAL_H

#include <stdint.h>

#include <hwregs_c.h>

/* Timer 2 free-runs at CPU clock / 8, with IRQs disabled. Each mode write
 * starts a new interval. Saturate if the 16-bit timer reached its limit. */
static inline void psxe_profile_begin(void) {
    TIMER_CTRL(2) = 1u << 9;
    (void)TIMER_CTRL(2);
}

static inline uint16_t psxe_profile_end(void) {
    uint16_t elapsed = TIMER_VALUE(2);
    return (TIMER_CTRL(2) & (1u << 12)) ? UINT16_MAX : elapsed;
}

#endif
