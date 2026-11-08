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

#endif
