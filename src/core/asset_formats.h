#ifndef PSXE_ASSET_FORMATS_H
#define PSXE_ASSET_FORMATS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct psxe_vag_view {
    const uint8_t *payload;
    uint32_t size;
    uint32_t sample_rate;
} psxe_vag_view;

typedef struct psxe_tim_block {
    const uint32_t *data;
    uint16_t width;
    uint16_t height;
} psxe_tim_block;

typedef struct psxe_tim_view {
    psxe_tim_block pixels;
    psxe_tim_block palettes;
    unsigned int mode;
    unsigned int texels_per_word;
} psxe_tim_view;

#define PSXE_VAG_HEADER_SIZE 48u
#define PSXE_SPU_RAM_END 0x80000u
#define PSXE_VRAM_WIDTH 1024
#define PSXE_VRAM_HEIGHT 512

/* Views borrow the buffer. A failed parse leaves the output untouched. */
bool psxe_vag_parse(const void *data, size_t size, psxe_vag_view *out);
bool psxe_tim_parse(const void *data, size_t size, psxe_tim_view *out);

#endif
