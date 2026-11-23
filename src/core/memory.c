#include "psxe/memory.h"

#include <stdint.h>

#define PSXE_HEAP_ALIGNMENT 8u
#define PSXE_HEAP_MASK (PSXE_HEAP_ALIGNMENT - 1u)
#define PSXE_HEAP_HEADER ((sizeof(psxe_heap_block) + PSXE_HEAP_MASK) & ~(size_t)PSXE_HEAP_MASK)

struct psxe_heap_block {
    size_t size;
    psxe_heap_block *next;
    bool used;
};

static uint8_t *psxe_heap_payload(psxe_heap_block *block) {
    return (uint8_t *)block + PSXE_HEAP_HEADER;
}

bool psxe_heap_init(psxe_heap *heap, void *memory, size_t size) {
    size_t padding = (size_t)(-(uintptr_t)memory & PSXE_HEAP_MASK);
    if (heap == NULL) {
        return false;
    }
    heap->first = NULL;
    if ((memory == NULL) || (size < padding + PSXE_HEAP_HEADER + PSXE_HEAP_ALIGNMENT)) {
        return false;
    }

    heap->first = (psxe_heap_block *)((uint8_t *)memory + padding);
    *heap->first = (psxe_heap_block){
        .size = ((size - padding) & ~(size_t)PSXE_HEAP_MASK) - PSXE_HEAP_HEADER
    };
    return true;
}

void *psxe_heap_alloc(psxe_heap *heap, size_t size) {
    if ((heap == NULL) || !size || (size > SIZE_MAX - PSXE_HEAP_MASK)) {
        return NULL;
    }
    size = (size + PSXE_HEAP_MASK) & ~(size_t)PSXE_HEAP_MASK;
    for (psxe_heap_block *block = heap->first; block != NULL; block = block->next) {
        if (block->used || (block->size < size)) {
            continue;
        }
        if (block->size - size >= PSXE_HEAP_HEADER + PSXE_HEAP_ALIGNMENT) {
            psxe_heap_block *split = (psxe_heap_block *)(psxe_heap_payload(block) + size);
            *split = (psxe_heap_block){ .size = block->size - size - PSXE_HEAP_HEADER, .next = block->next };
            block->next = split;
            block->size = size;
        }
        block->used = true;
        return psxe_heap_payload(block);
    }
    return NULL;
}

static void psxe_heap_merge_next(psxe_heap_block *block) {
    psxe_heap_block *next = block->next;
    if ((next != NULL) && !next->used) {
        block->size += PSXE_HEAP_HEADER + next->size;
        block->next = next->next;
    }
}

bool psxe_heap_free(psxe_heap *heap, void *memory) {
    psxe_heap_block *previous = NULL;
    if ((heap == NULL) || (memory == NULL)) {
        return false;
    }
    for (psxe_heap_block *block = heap->first; block != NULL; block = block->next) {
        if (psxe_heap_payload(block) == memory) {
            if (!block->used) {
                return false;
            }
            block->used = false;
            psxe_heap_merge_next(block);
            if ((previous != NULL) && !previous->used) {
                psxe_heap_merge_next(previous);
            }
            return true;
        }
        previous = block;
    }
    return false;
}

psxe_heap_stats psxe_heap_get_stats(const psxe_heap *heap) {
    psxe_heap_stats stats = { 0 };
    if (heap == NULL) {
        return stats;
    }
    for (const psxe_heap_block *block = heap->first; block != NULL; block = block->next) {
        if (block->used) {
            stats.used_bytes += block->size;
        } else {
            stats.free_bytes += block->size;
            if (stats.largest_free < block->size) {
                stats.largest_free = block->size;
            }
        }
    }
    return stats;
}

psxe_mesh_storage *psxe_mesh_alloc(
    psxe_heap *heap, uint16_t vertex_count, uint16_t normal_count, uint16_t face_count
) {
    size_t vertices_size = sizeof(SVECTOR) * vertex_count;
    size_t normals_size = sizeof(SVECTOR) * normal_count;
    size_t faces_size = sizeof(psxe_face) * face_count;
    psxe_mesh_storage *storage;

    if (!vertex_count || !face_count) {
        return NULL;
    }
    storage = psxe_heap_alloc(heap, sizeof(*storage) + vertices_size + normals_size + faces_size);
    if (storage == NULL) {
        return NULL;
    }
    storage->vertices = (SVECTOR *)(storage + 1);
    storage->normals = normal_count ? (SVECTOR *)((uint8_t *)storage->vertices + vertices_size) : NULL;
    storage->faces = (psxe_face *)((uint8_t *)storage->vertices + vertices_size + normals_size);
    storage->mesh = (psxe_mesh){
        .vertices = storage->vertices,
        .normals = storage->normals,
        .faces = storage->faces,
        .vertex_count = vertex_count,
        .normal_count = normal_count,
        .face_count = face_count
    };
    return storage;
}

bool psxe_mesh_free(psxe_heap *heap, psxe_mesh_storage *storage) {
    return psxe_heap_free(heap, storage);
}
