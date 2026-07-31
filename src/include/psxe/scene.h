#ifndef PSXE_SCENE_H
#define PSXE_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "psxe/config.h"
#include "psxe/render.h"
#include "psxe/types.h"

typedef struct psxe_entity_id {
    uint16_t index;
    uint16_t generation;
} psxe_entity_id;

typedef struct psxe_entity {
    psxe_transform transform;
    const psxe_mesh *mesh;
    uint16_t generation;
    bool active;
    bool visible;
} psxe_entity;

typedef struct psxe_scene {
    psxe_entity entities[PSXE_MAX_ENTITIES];
    uint16_t active_count;
} psxe_scene;

void psxe_scene_init(psxe_scene *scene);
psxe_entity_id psxe_scene_create(psxe_scene *scene);
bool psxe_scene_destroy(psxe_scene *scene, psxe_entity_id id);
psxe_entity *psxe_scene_get(psxe_scene *scene, psxe_entity_id id);
const psxe_entity *psxe_scene_get_const(const psxe_scene *scene, psxe_entity_id id);
void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer);

#endif
