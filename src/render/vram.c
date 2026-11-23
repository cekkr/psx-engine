#include "psxe/vram.h"

#include <string.h>

#include <hwregs_c.h>
#include <psxapi.h>
#include <psxetc.h>

enum {
    PSXE_VRAM_WIDTH = 1024,
    PSXE_VRAM_HEIGHT = 512,
    PSXE_VRAM_TILE_W = 16,
    PSXE_VRAM_TILE_H = 8
};

static uint64_t psxe_vram_mask(const RECT *rect) {
    unsigned int first = (unsigned int)rect->x / PSXE_VRAM_TILE_W;
    unsigned int last = (unsigned int)(rect->x + rect->w - 1) / PSXE_VRAM_TILE_W;
    return (UINT64_MAX << first) & (UINT64_MAX >> (63u - last));
}

static bool psxe_vram_is_free(const psxe_vram *vram, const RECT *rect) {
    uint64_t mask = psxe_vram_mask(rect);
    for (int row = rect->y / PSXE_VRAM_TILE_H; row <= (rect->y + rect->h - 1) / PSXE_VRAM_TILE_H; ++row) {
        if (vram->used_rows[row] & mask) {
            return false;
        }
    }
    return true;
}

static void psxe_vram_mark(psxe_vram *vram, const RECT *rect, bool used) {
    uint64_t mask = psxe_vram_mask(rect);
    for (int row = rect->y / PSXE_VRAM_TILE_H; row <= (rect->y + rect->h - 1) / PSXE_VRAM_TILE_H; ++row) {
        if (used) {
            vram->used_rows[row] |= mask;
        } else {
            vram->used_rows[row] &= ~mask;
        }
    }
}

bool psxe_vram_reserve(psxe_vram *vram, int x, int y, int width, int height) {
    RECT area;
    if ((vram == NULL) || (vram->renderer == NULL) ||
        (x < 0) || (x >= PSXE_VRAM_WIDTH) || (y < 0) || (y >= PSXE_VRAM_HEIGHT) ||
        (width <= 0) || (width > PSXE_VRAM_WIDTH - x) ||
        (height <= 0) || (height > PSXE_VRAM_HEIGHT - y)) {
        return false;
    }
    area = (RECT){ (int16_t)x, (int16_t)y, (int16_t)width, (int16_t)height };
    if (!psxe_vram_is_free(vram, &area)) {
        return false;
    }
    psxe_vram_mark(vram, &area, true);
    return true;
}

void psxe_vram_init(psxe_vram *vram, const psxe_renderer *renderer) {
    if (vram == NULL) {
        return;
    }
    memset(vram, 0, sizeof(*vram));
    if (renderer == NULL) {
        return;
    }
    vram->renderer = renderer;
    RECT framebuffers = { 0, 0, renderer->width, (int16_t)(renderer->height * 2) };
    RECT font = { 960, 0, 32, 33 };
    if ((framebuffers.w > 0) && (framebuffers.w <= PSXE_VRAM_WIDTH) &&
        (framebuffers.h > 0) && (framebuffers.h <= PSXE_VRAM_HEIGHT)) {
        psxe_vram_mark(vram, &framebuffers, true);
        psxe_vram_mark(vram, &font, true);
    } else {
        vram->renderer = NULL;
    }
}

static bool psxe_vram_find(const psxe_vram *vram, RECT *area, unsigned int texels_per_word) {
    for (int y = 0; y <= PSXE_VRAM_HEIGHT - area->h; y += PSXE_VRAM_TILE_H) {
        if (texels_per_word && (((y & 255) + area->h) > 256)) {
            continue;
        }
        area->y = (int16_t)y;
        for (int x = 0; x <= PSXE_VRAM_WIDTH - area->w; x += PSXE_VRAM_TILE_W) {
            if (texels_per_word && ((unsigned int)((x & 63) + area->w) * texels_per_word > 256u)) {
                continue;
            }
            area->x = (int16_t)x;
            if (psxe_vram_is_free(vram, area)) {
                return true;
            }
        }
    }
    return false;
}

static uint32_t psxe_tim_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static size_t psxe_tim_block(const uint8_t *data, size_t length, RECT *area) {
    uint32_t block_size;
    unsigned int width;
    unsigned int height;
    if ((length < 12u) || ((uintptr_t)data & 3u)) {
        return 0;
    }
    block_size = psxe_tim_le32(data);
    width = (unsigned int)data[8] | ((unsigned int)data[9] << 8);
    height = (unsigned int)data[10] | ((unsigned int)data[11] << 8);
    if ((block_size < 12u) || (block_size > length) || !width || !height ||
        (width > PSXE_VRAM_WIDTH) || (height > PSXE_VRAM_HEIGHT) ||
        (width * height > (block_size - 12u) / 2u)) {
        return 0;
    }
    *area = (RECT){ 0, 0, (int16_t)width, (int16_t)height };
    return block_size;
}

static bool psxe_vram_wait_upload(void) {
    int restore_interrupts;
    if (DrawSync(0) == 0) {
        return true;
    }
    /* The transfer started with an idle GPU. Discard the failed upload
     * before releasing its borrowed buffer or reusing its tiles. */
    restore_interrupts = EnterCriticalSection();
    DMA_CHCR(DMA_GPU) = 0x00000201;
    ResetGraph(1);
    if (restore_interrupts) {
        ExitCriticalSection();
    }
    return false;
}

static bool psxe_vram_upload(RECT area, const uint32_t *data) {
    if (((unsigned int)area.w * (unsigned int)area.h) & 1u) {
        /* The SDK DMA needs pairs of halfwords. For an odd final row, pad
         * one texel into the unused part of its allocated tile. */
        uint32_t row[128] = { 0 };
        size_t preceding = (size_t)area.w * (size_t)(area.h - 1);
        RECT rows = area;
        --rows.h;
        if ((rows.h > 0) && (LoadImage(&rows, data) < 0)) {
            return false;
        }
        memcpy(row, data + preceding / 2u, (size_t)area.w * sizeof(uint16_t));
        area.y = (int16_t)(area.y + rows.h);
        area.h = 1;
        ++area.w;
        int queued = LoadImage(&area, row);
        bool completed = psxe_vram_wait_upload();
        return (queued >= 0) && completed;
    }
    int queued = LoadImage(&area, data);
    return (queued >= 0) && psxe_vram_wait_upload();
}

static bool psxe_vram_can_edit(const psxe_vram *vram) {
    const psxe_renderer *renderer = (vram != NULL) ? vram->renderer : NULL;
    return (renderer != NULL) && !renderer->frame_open && (renderer->active_target == NULL) &&
           (renderer->next_packet == renderer->buffers[renderer->active_buffer].packets);
}

bool psxe_vram_load_tim(psxe_vram *vram, const void *data, size_t size, psxe_vram_id *id) {
    const uint8_t *bytes = data;
    const uint32_t *palette_data = NULL;
    unsigned int mode;
    unsigned int texels_per_word;
    unsigned int index;
    psxe_vram_slot allocation = { 0 };

    if (!psxe_vram_can_edit(vram) || (data == NULL) || (id == NULL) || (size < 8u) ||
        ((uintptr_t)data & 3u) || (psxe_tim_le32(bytes) != 0x10u)) {
        return false;
    }
    uint32_t flags = psxe_tim_le32(bytes + 4);
    bytes += 8;
    size -= 8;
    mode = flags & 3u;
    if ((flags != 2u) && (flags != 8u) && (flags != 9u)) {
        return false;
    }
    texels_per_word = 4u >> mode;
    if (flags & 8u) {
        size_t block_size = psxe_tim_block(bytes, size, &allocation.palettes);
        if (!block_size || allocation.palettes.w != (mode ? 256 : 16)) {
            return false;
        }
        allocation.palette_count = (uint16_t)allocation.palettes.h;
        palette_data = (const uint32_t *)(bytes + 12);
        bytes += block_size;
        size -= block_size;
    }
    if (!psxe_tim_block(bytes, size, &allocation.pixels) ||
        ((unsigned int)allocation.pixels.w * texels_per_word > 256u) || (allocation.pixels.h > 256)) {
        return false;
    }
    for (index = 0; index < PSXE_VRAM_MAX_TEXTURES; ++index) {
        if (!vram->slots[index].texture.valid) {
            break;
        }
    }
    if ((index == PSXE_VRAM_MAX_TEXTURES) || (DrawSync(0) != 0) ||
        !psxe_vram_find(vram, &allocation.pixels, texels_per_word)) {
        return false;
    }

    psxe_vram_mark(vram, &allocation.pixels, true);
    if (palette_data != NULL) {
        if (!psxe_vram_find(vram, &allocation.palettes, 0)) {
            psxe_vram_mark(vram, &allocation.pixels, false);
            return false;
        }
        psxe_vram_mark(vram, &allocation.palettes, true);
        allocation.texture.clut = (uint16_t)getClut(allocation.palettes.x, allocation.palettes.y);
    }
    if ((palette_data != NULL && !psxe_vram_upload(allocation.palettes, palette_data)) ||
        !psxe_vram_upload(allocation.pixels, (const uint32_t *)(bytes + 12))) {
        psxe_vram_mark(vram, &allocation.pixels, false);
        if (palette_data != NULL) {
            psxe_vram_mark(vram, &allocation.palettes, false);
        }
        return false;
    }

    allocation.texture.tpage = (uint16_t)getTPage(mode, 0, allocation.pixels.x, allocation.pixels.y);
    allocation.texture.width = (uint16_t)((unsigned int)allocation.pixels.w * texels_per_word);
    allocation.texture.height = (uint16_t)allocation.pixels.h;
    allocation.texture.u = (uint8_t)((unsigned int)(allocation.pixels.x & 63) * texels_per_word);
    allocation.texture.v = (uint8_t)(allocation.pixels.y & 255);
    allocation.texture.valid = true;
    allocation.generation = vram->slots[index].generation + 1u;
    if (!allocation.generation) {
        allocation.generation = 1;
    }
    vram->slots[index] = allocation;
    *id = (psxe_vram_id){ allocation.generation, (uint16_t)index };
    return true;
}

const psxe_texture *psxe_vram_get(const psxe_vram *vram, psxe_vram_id id) {
    if ((vram == NULL) || (id.index >= PSXE_VRAM_MAX_TEXTURES) ||
        (vram->slots[id.index].generation != id.generation) || !vram->slots[id.index].texture.valid) {
        return NULL;
    }
    return &vram->slots[id.index].texture;
}

bool psxe_vram_palette(const psxe_vram *vram, psxe_vram_id id, unsigned int palette, psxe_texture *view) {
    const psxe_texture *texture = psxe_vram_get(vram, id);
    if ((texture == NULL) || (view == NULL) || (palette >= vram->slots[id.index].palette_count)) {
        return false;
    }
    const RECT *palettes = &vram->slots[id.index].palettes;
    *view = *texture;
    view->clut = (uint16_t)getClut(palettes->x, (unsigned int)palettes->y + palette);
    return true;
}

bool psxe_vram_free(psxe_vram *vram, psxe_vram_id id) {
    if (!psxe_vram_can_edit(vram) || (psxe_vram_get(vram, id) == NULL) || (DrawSync(0) != 0)) {
        return false;
    }
    psxe_vram_slot *slot = &vram->slots[id.index];
    psxe_vram_mark(vram, &slot->pixels, false);
    if (slot->palette_count) {
        psxe_vram_mark(vram, &slot->palettes, false);
    }
    slot->texture.valid = false;
    return true;
}
