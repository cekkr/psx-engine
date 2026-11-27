#include "render_internal.h"

void *psxe_renderer_allocate(psxe_renderer *renderer, size_t size) {
    uintptr_t current = (uintptr_t)renderer->next_packet;
    uintptr_t end = (uintptr_t)renderer->packet_end;
    size_t padding = (size_t)(-current & 3u);

    if ((current > end) || (padding > end - current) || (size > end - current - padding)) {
        ++renderer->dropped_primitives;
        return NULL;
    }

    uint8_t *result = renderer->next_packet + padding;
    renderer->next_packet = result + size;
    return result;
}

uint32_t *psxe_renderer_bucket(psxe_renderer *renderer, unsigned int layer) {
    if (layer >= renderer->ot_length) {
        layer = (unsigned int)renderer->ot_length - 1u;
    }
    return &renderer->ordering_table[layer];
}
