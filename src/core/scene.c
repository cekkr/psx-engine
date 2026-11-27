#include "psxe/scene.h"

#include <string.h>

#include "../render/render_internal.h"
#include "fixed.h"

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
            entity->light_slot = PSXE_SCENE_NO_LIGHT;
            entity->material = psxe_material_default();
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

    psxe_scene_clear_light(scene, id);
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

static void psxe_scene_world_light(const psxe_entity *entity, const psxe_light *local, psxe_light *out) {
    MATRIX rotation;
    VECTOR offset;
    VECTOR direction;

    *out = *local;
    RotMatrix((SVECTOR *)&entity->transform.rotation, &rotation);
    psxe_matrix_apply(&rotation, local->position.vx, local->position.vy, local->position.vz, &offset);
    out->position.vx = entity->transform.position.vx + offset.vx;
    out->position.vy = entity->transform.position.vy + offset.vy;
    out->position.vz = entity->transform.position.vz + offset.vz;
    psxe_matrix_apply(&rotation, local->direction.vx, local->direction.vy, local->direction.vz, &direction);
    out->direction.vx = (int16_t)direction.vx;
    out->direction.vy = (int16_t)direction.vy;
    out->direction.vz = (int16_t)direction.vz;
}

bool psxe_scene_set_light(psxe_scene *scene, psxe_entity_id id, const psxe_light *light) {
    psxe_entity *entity;
    uint8_t slot;

    if ((light == NULL) || !psxe_scene_id_valid(scene, id)) {
        return false;
    }
    entity = &scene->entities[id.index];
    if (entity->light_slot == PSXE_SCENE_NO_LIGHT) {
        for (slot = 0; slot < PSXE_MAX_SCENE_LIGHTS; ++slot) {
            if (!scene->lights[slot].used) {
                break;
            }
        }
        if (slot >= PSXE_MAX_SCENE_LIGHTS) {
            ++scene->lights_dropped;
            return false;
        }
        scene->lights[slot].used = true;
        scene->lights[slot].owner = id.index;
        entity->light_slot = slot;
        ++scene->light_count;
    }
    scene->lights[entity->light_slot].light = *light;
    return true;
}

bool psxe_scene_clear_light(psxe_scene *scene, psxe_entity_id id) {
    psxe_entity *entity;

    if (!psxe_scene_id_valid(scene, id)) {
        return false;
    }
    entity = &scene->entities[id.index];
    if (entity->light_slot == PSXE_SCENE_NO_LIGHT) {
        return false;
    }
    scene->lights[entity->light_slot].used = false;
    entity->light_slot = PSXE_SCENE_NO_LIGHT;
    --scene->light_count;
    return true;
}

psxe_light *psxe_scene_light(psxe_scene *scene, psxe_entity_id id) {
    if (!psxe_scene_id_valid(scene, id) ||
        (scene->entities[id.index].light_slot == PSXE_SCENE_NO_LIGHT)) {
        return NULL;
    }
    return &scene->lights[scene->entities[id.index].light_slot].light;
}

bool psxe_scene_light_world(const psxe_scene *scene, psxe_entity_id id, psxe_light *out) {
    const psxe_entity *entity;

    if ((out == NULL) || !psxe_scene_id_valid(scene, id)) {
        return false;
    }
    entity = &scene->entities[id.index];
    if (entity->light_slot == PSXE_SCENE_NO_LIGHT) {
        return false;
    }
    psxe_scene_world_light(entity, &scene->lights[entity->light_slot].light, out);
    return true;
}

uint16_t psxe_scene_gather_lights(const psxe_scene *scene, psxe_light *out, uint16_t capacity) {
    uint16_t count = 0;
    uint16_t slot;

    if ((scene == NULL) || (out == NULL)) {
        return 0;
    }
    for (slot = 0; (slot < PSXE_MAX_SCENE_LIGHTS) && (count < capacity); ++slot) {
        const psxe_scene_light_slot *entry = &scene->lights[slot];
        const psxe_entity *entity;

        if (!entry->used || !entry->light.enabled || (entry->owner >= PSXE_MAX_ENTITIES)) {
            continue;
        }
        entity = &scene->entities[entry->owner];
        /* Owner must still be active and point back at this slot. */
        if (!entity->active || (entity->light_slot != slot)) {
            continue;
        }
        psxe_scene_world_light(entity, &entry->light, &out[count++]);
    }
    return count;
}

void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer) {
    psxe_light lights[PSXE_MAX_SCENE_LIGHTS];
    uint16_t light_count;
    uint16_t index;
    if ((scene == NULL) || (renderer == NULL)) {
        return;
    }

    /* Entity lights are gathered once per call, in world space, and offered
     * to every mesh alongside the renderer's own light set. */
    light_count = psxe_scene_gather_lights(scene, lights, PSXE_MAX_SCENE_LIGHTS);
    for (index = 0; index < PSXE_MAX_ENTITIES; ++index) {
        const psxe_entity *entity = &scene->entities[index];
        if (entity->active && entity->visible && (entity->mesh != NULL)) {
            psxe_draw_mesh_lights(
                renderer, entity->mesh, &entity->transform, &entity->material,
                lights, light_count
            );
        }
    }
}
