#include "psxe/scene.h"

#include <string.h>

#define PSXE_INVALID_ENTITY_INDEX UINT16_MAX

static bool psxe_scene_id_valid(const psxe_scene *scene, psxe_entity_id id) {
    return (scene != NULL) &&
           (id.index < PSXE_MAX_ENTITIES) &&
           scene->entities[id.index].active &&
           (scene->entities[id.index].generation == id.generation);
}

void psxe_scene_init(psxe_scene *scene) {
    memset(scene, 0, sizeof(*scene));
}

psxe_entity_id psxe_scene_create(psxe_scene *scene) {
    psxe_entity_id result = { PSXE_INVALID_ENTITY_INDEX, 0 };
    uint16_t index;

    if ((scene == NULL) || (scene->active_count >= PSXE_MAX_ENTITIES)) {
        return result;
    }

    for (index = 0; index < PSXE_MAX_ENTITIES; ++index) {
        psxe_entity *entity = &scene->entities[index];
        if (!entity->active) {
            entity->active = true;
            entity->visible = true;
            entity->mesh = NULL;
            entity->transform = psxe_transform_identity();
            ++scene->active_count;
            result.index = index;
            result.generation = entity->generation;
            return result;
        }
    }
    return result;
}

bool psxe_scene_destroy(psxe_scene *scene, psxe_entity_id id) {
    psxe_entity *entity;
    if (!psxe_scene_id_valid(scene, id)) {
        return false;
    }

    entity = &scene->entities[id.index];
    entity->active = false;
    entity->visible = false;
    entity->mesh = NULL;
    ++entity->generation;
    --scene->active_count;
    return true;
}

psxe_entity *psxe_scene_get(psxe_scene *scene, psxe_entity_id id) {
    if (!psxe_scene_id_valid(scene, id)) {
        return NULL;
    }
    return &scene->entities[id.index];
}

const psxe_entity *psxe_scene_get_const(const psxe_scene *scene, psxe_entity_id id) {
    if (!psxe_scene_id_valid(scene, id)) {
        return NULL;
    }
    return &scene->entities[id.index];
}

void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer) {
    uint16_t index;
    if ((scene == NULL) || (renderer == NULL)) {
        return;
    }

    for (index = 0; index < PSXE_MAX_ENTITIES; ++index) {
        const psxe_entity *entity = &scene->entities[index];
        if (entity->active && entity->visible && (entity->mesh != NULL)) {
            psxe_draw_mesh(renderer, entity->mesh, &entity->transform);
        }
    }
}
