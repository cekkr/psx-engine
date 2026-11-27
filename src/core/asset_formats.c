#include "asset_formats.h"

#include <string.h>

#include "byte_order.h"

bool psxe_vag_parse(const void *data, size_t size, psxe_vag_view *out) {
    const uint8_t *bytes = data;
    if ((data == NULL) || (out == NULL) || (size < PSXE_VAG_HEADER_SIZE) ||
        memcmp(bytes, "VAGp", 4) || psxe_read_be32(bytes + 8) || bytes[30] || (bytes[31] > 1u)) {
        return false;
    }

    uint32_t payload_size = psxe_read_be32(bytes + 12);
    uint32_t sample_rate = psxe_read_be32(bytes + 16);
    if (!payload_size || (payload_size & 15u) || (payload_size > PSXE_SPU_RAM_END) ||
        (payload_size > size - PSXE_VAG_HEADER_SIZE) ||
        (sample_rate < 11u) || (sample_rate > (0x3fffu * 44100u) / 4096u)) {
        return false;
    }
    *out = (psxe_vag_view){ bytes + PSXE_VAG_HEADER_SIZE, payload_size, sample_rate };
    return true;
}

static size_t psxe_tim_parse_block(const uint8_t *bytes, size_t size, psxe_tim_block *out) {
    if ((size < 12u) || ((uintptr_t)bytes & 3u)) {
        return 0;
    }

    uint32_t block_size = psxe_read_le32(bytes);
    uint16_t width = psxe_read_le16(bytes + 8);
    uint16_t height = psxe_read_le16(bytes + 10);
    if ((block_size < 12u) || (block_size > size) || !width || !height ||
        (width > PSXE_VRAM_WIDTH) || (height > PSXE_VRAM_HEIGHT) ||
        ((uint32_t)width * height > (block_size - 12u) / 2u)) {
        return 0;
    }
    *out = (psxe_tim_block){ (const uint32_t *)(bytes + 12), width, height };
    return block_size;
}

bool psxe_tim_parse(const void *data, size_t size, psxe_tim_view *out) {
    const uint8_t *bytes = data;
    psxe_tim_view view = { 0 };

    if ((data == NULL) || (out == NULL) || (size < 8u) || ((uintptr_t)data & 3u) ||
        (psxe_read_le32(bytes) != 0x10u)) {
        return false;
    }
    uint32_t flags = psxe_read_le32(bytes + 4);
    if ((flags != 2u) && (flags != 8u) && (flags != 9u)) {
        return false;
    }

    bytes += 8;
    size -= 8;
    view.mode = flags & 3u;
    view.texels_per_word = 4u >> view.mode;
    if (flags & 8u) {
        size_t palette_size = psxe_tim_parse_block(bytes, size, &view.palettes);
        if (!palette_size || (view.palettes.width != (view.mode ? 256 : 16))) {
            return false;
        }
        bytes += palette_size;
        size -= palette_size;
    }
    if (!psxe_tim_parse_block(bytes, size, &view.pixels) ||
        (view.pixels.width * view.texels_per_word > 256u) || (view.pixels.height > 256u)) {
        return false;
    }
    *out = view;
    return true;
}
