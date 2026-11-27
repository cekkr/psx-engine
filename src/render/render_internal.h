#ifndef PSXE_RENDER_INTERNAL_H
#define PSXE_RENDER_INTERNAL_H

/* Private helpers shared by the renderer translation units. Not public API. */

#include "psxe/render.h"

/* The only GPU packet allocator: 4-byte aligned, bounded by packet_end,
 * increments dropped_primitives and returns NULL when the arena is full. */
void *psxe_renderer_allocate(psxe_renderer *renderer, size_t size);

/* Bucket in the current destination ordering table (main buffer or render
 * target). Out-of-range layers clamp to the last bucket. */
uint32_t *psxe_renderer_bucket(psxe_renderer *renderer, unsigned int layer);

/* Replaces the semi-transparency (ABR) bits of a texture-page attribute. */
static inline uint16_t psxe_tpage_with_blend(uint16_t tpage, uint8_t blend) {
    return (uint16_t)((tpage & ~(3u << 5)) | ((blend & 3u) << 5));
}

/* GTE lighting state for one mesh, built on the CPU from the light set. */
typedef struct psxe_light_state {
    MATRIX light;  /* world-space rows: unit directions toward up to 3 lights */
    MATRIX color;  /* columns: per-light RGB in 4.12 */
    int32_t back[3]; /* ambient + folded lights, 4096 = full brightness */
} psxe_light_state;

/* Ranks every enabled light not excluded by exclude_mask by its contribution
 * at origin, keeps the best three for the GTE rows, and folds ambient lights,
 * the set's ambient color, and the remainder into the back color. Updates the
 * renderer's light counters. Touches no GTE state. */
void psxe_light_select(
    psxe_renderer *renderer,
    const VECTOR *origin,
    uint8_t exclude_mask,
    const psxe_light *extra_lights,
    uint16_t extra_count,
    psxe_light_state *state
);

#endif
