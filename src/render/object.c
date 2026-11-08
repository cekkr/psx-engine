#include "psxe/render.h"

#include <stddef.h>

psxe_material psxe_material_default(void) {
    psxe_material material;

    material.texture = NULL;
    material.color.r = 128;
    material.color.g = 128;
    material.color.b = 128;
    material.color_mode = PSXE_COLOR_FACE;
    material.effects = 0;
    material.blend = PSXE_BLEND_HALF;
    return material;
}

void psxe_material_set_texture(psxe_material *material, const psxe_texture *texture) {
    if (material != NULL) {
        material->texture = texture;
    }
}

void psxe_material_set_color(psxe_material *material, psxe_color color, psxe_color_mode mode) {
    if (material != NULL) {
        material->color = color;
        material->color_mode = (uint8_t)mode;
    }
}

void psxe_material_set_effects(psxe_material *material, uint8_t effects, psxe_blend_mode blend) {
    if (material != NULL) {
        material->effects = effects;
        material->blend = (uint8_t)(blend & 3);
    }
}

void psxe_object_init(psxe_object *object, const psxe_mesh *mesh) {
    if (object == NULL) {
        return;
    }
    object->mesh = mesh;
    object->transform = psxe_transform_identity();
    object->material = psxe_material_default();
    object->visible = true;
}

void psxe_object_set_mesh(psxe_object *object, const psxe_mesh *mesh) {
    if (object != NULL) {
        object->mesh = mesh;
    }
}

void psxe_object_set_texture(psxe_object *object, const psxe_texture *texture) {
    if (object != NULL) {
        psxe_material_set_texture(&object->material, texture);
    }
}

void psxe_object_set_color(psxe_object *object, psxe_color color, psxe_color_mode mode) {
    if (object != NULL) {
        psxe_material_set_color(&object->material, color, mode);
    }
}

void psxe_object_set_effects(psxe_object *object, uint8_t effects, psxe_blend_mode blend) {
    if (object != NULL) {
        psxe_material_set_effects(&object->material, effects, blend);
    }
}

void psxe_object_set_position(psxe_object *object, int32_t x, int32_t y, int32_t z) {
    if (object != NULL) {
        object->transform.position.vx = x;
        object->transform.position.vy = y;
        object->transform.position.vz = z;
    }
}

void psxe_object_translate(psxe_object *object, int32_t dx, int32_t dy, int32_t dz) {
    if (object != NULL) {
        object->transform.position.vx += dx;
        object->transform.position.vy += dy;
        object->transform.position.vz += dz;
    }
}

void psxe_object_set_rotation(psxe_object *object, int16_t x, int16_t y, int16_t z) {
    if (object != NULL) {
        object->transform.rotation.vx = x;
        object->transform.rotation.vy = y;
        object->transform.rotation.vz = z;
    }
}

static int16_t psxe_wrap_angle(int32_t angle) {
    return (int16_t)(angle & (PSXE_ANGLE_FULL_TURN - 1));
}

void psxe_object_rotate(psxe_object *object, int16_t dx, int16_t dy, int16_t dz) {
    if (object != NULL) {
        object->transform.rotation.vx = psxe_wrap_angle((int32_t)object->transform.rotation.vx + dx);
        object->transform.rotation.vy = psxe_wrap_angle((int32_t)object->transform.rotation.vy + dy);
        object->transform.rotation.vz = psxe_wrap_angle((int32_t)object->transform.rotation.vz + dz);
    }
}

void psxe_object_set_scale(psxe_object *object, int32_t scale) {
    if (object != NULL) {
        object->transform.scale.vx = scale;
        object->transform.scale.vy = scale;
        object->transform.scale.vz = scale;
    }
}

void psxe_object_draw(psxe_renderer *renderer, const psxe_object *object) {
    if ((object == NULL) || !object->visible) {
        return;
    }
    psxe_draw_mesh_material(renderer, object->mesh, &object->transform, &object->material);
}
