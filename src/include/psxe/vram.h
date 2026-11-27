#ifndef PSXE_VRAM_H
#define PSXE_VRAM_H

#include "psxe/render.h"

#define PSXE_VRAM_MAX_TEXTURES 32

typedef struct psxe_vram_id {
    uint32_t generation;
    uint16_t index;
} psxe_vram_id;

typedef struct psxe_vram_slot {
    psxe_texture texture;
    RECT pixels;
    RECT palettes;
    uint32_t generation;
    uint16_t palette_count;
} psxe_vram_slot;

/* One pool per renderer. Tiles are 16 halfwords by 8 rows. */
typedef struct psxe_vram {
    const psxe_renderer *renderer;
    uint64_t used_rows[64];
    psxe_vram_slot slots[PSXE_VRAM_MAX_TEXTURES];
} psxe_vram;

typedef struct psxe_vram_stats {
    uint16_t used_tiles;
    uint16_t free_tiles;
} psxe_vram_stats;

void psxe_vram_init(psxe_vram *vram, const psxe_renderer *renderer);
/* Permanently exclude caller-managed VRAM, such as fixed render targets.
 * Coordinates and width are in halfwords. Rectangles are rounded to tiles. */
bool psxe_vram_reserve(psxe_vram *vram, int x, int y, int width, int height);
/* Load and relocate a sized 4/8/16-bit TIM. Only between frame builds. */
bool psxe_vram_load_tim(psxe_vram *vram, const void *data, size_t size, psxe_vram_id *id);
/* Borrowed views and palette variants are valid until free. */
const psxe_texture *psxe_vram_get(const psxe_vram *vram, psxe_vram_id id);
bool psxe_vram_palette(const psxe_vram *vram, psxe_vram_id id, unsigned int palette, psxe_texture *view);
bool psxe_vram_free(psxe_vram *vram, psxe_vram_id id);
/* Includes reservations, framebuffers, debug font, pixels and palettes. */
psxe_vram_stats psxe_vram_get_stats(const psxe_vram *vram);

#endif
