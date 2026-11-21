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

#define PSXE_SCENE_NO_LIGHT 0xffu

typedef struct psxe_entity {
    psxe_transform transform;
    const psxe_mesh *mesh;
    psxe_material material; /* per-entity texture/color/effect/light-mask override */
    uint16_t generation;
    uint8_t light_slot;     /* internal: index into psxe_scene.lights or PSXE_SCENE_NO_LIGHT */
    bool active;
    bool visible;
} psxe_entity;

/* A light owned by one entity. position/direction are entity-local: they
 * follow the entity's position and rotation (not its scale). */
typedef struct psxe_scene_light_slot {
    psxe_light light;
    uint16_t owner; /* entity index */
    bool used;
} psxe_scene_light_slot;

typedef struct psxe_scene {
    psxe_entity entities[PSXE_MAX_ENTITIES];
    psxe_scene_light_slot lights[PSXE_MAX_SCENE_LIGHTS];
    uint16_t active_count;
    uint16_t light_count;
    uint16_t lights_dropped; /* psxe_scene_set_light calls rejected for capacity */
} psxe_scene;

void psxe_scene_init(psxe_scene *scene);
psxe_entity_id psxe_scene_create(psxe_scene *scene);
bool psxe_scene_destroy(psxe_scene *scene, psxe_entity_id id);
psxe_entity *psxe_scene_get(psxe_scene *scene, psxe_entity_id id);
const psxe_entity *psxe_scene_get_const(const psxe_scene *scene, psxe_entity_id id);
void psxe_scene_render(const psxe_scene *scene, psxe_renderer *renderer);

/* Entity lights */
bool psxe_scene_set_light(psxe_scene *scene, psxe_entity_id id, const psxe_light *light);
bool psxe_scene_clear_light(psxe_scene *scene, psxe_entity_id id);
psxe_light *psxe_scene_light(psxe_scene *scene, psxe_entity_id id);
bool psxe_scene_light_world(const psxe_scene *scene, psxe_entity_id id, psxe_light *out);
uint16_t psxe_scene_gather_lights(const psxe_scene *scene, psxe_light *out, uint16_t capacity);

#endif
