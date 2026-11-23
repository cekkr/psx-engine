#ifndef PSXE_MEMORY_H
#define PSXE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#include "psxe/render.h"

typedef struct psxe_heap_block psxe_heap_block;

/* A bounded heap over caller-supplied RAM. Allocations are 8-byte aligned. */
typedef struct psxe_heap {
    psxe_heap_block *first;
} psxe_heap;

typedef struct psxe_heap_stats {
    size_t used_bytes;
    size_t free_bytes;
    size_t largest_free;
} psxe_heap_stats;

bool psxe_heap_init(psxe_heap *heap, void *memory, size_t size);
void *psxe_heap_alloc(psxe_heap *heap, size_t size);
bool psxe_heap_free(psxe_heap *heap, void *memory);
psxe_heap_stats psxe_heap_get_stats(const psxe_heap *heap);

/* One allocation for the draw view and its writable arrays. Destroy once and
 * detach borrowers before freeing. texture is borrowed independently. */
typedef struct psxe_mesh_storage {
    psxe_mesh mesh;
    SVECTOR *vertices;
    SVECTOR *normals;
    psxe_face *faces;
} psxe_mesh_storage;

psxe_mesh_storage *psxe_mesh_alloc(
    psxe_heap *heap, uint16_t vertex_count, uint16_t normal_count, uint16_t face_count
);
bool psxe_mesh_free(psxe_heap *heap, psxe_mesh_storage *storage);

#endif
