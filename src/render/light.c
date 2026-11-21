#include "psxe/render.h"

#include <stdint.h>
#include <string.h>

#include "render_internal.h"

/* Direction precision target: scaled components land in 0x2000..0x3fff so
 * squared sums fit in 32 bits and unit vectors keep ~13 bits. */
#define PSXE_NORMALIZE_HIGH 0x3fffu
#define PSXE_NORMALIZE_LOW 0x2000u

typedef struct psxe_light_candidate {
    int32_t score;
    SVECTOR direction;
    int32_t rgb[3];
} psxe_light_candidate;

static uint32_t psxe_isqrt(uint32_t value) {
    uint32_t result = 0;
    uint32_t bit = 1u << 30;

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (value >= (result + bit)) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint32_t psxe_abs32(int32_t value) {
    return (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

int32_t psxe_vector_normalize(int32_t x, int32_t y, int32_t z, SVECTOR *out) {
    uint32_t largest = psxe_abs32(x);
    int32_t sx;
    int32_t sy;
    int32_t sz;
    int shift = 0; /* > 0: scaled down, < 0: scaled up */
    uint32_t length;
    int64_t real_length;

    if (psxe_abs32(y) > largest) {
        largest = psxe_abs32(y);
    }
    if (psxe_abs32(z) > largest) {
        largest = psxe_abs32(z);
    }
    if (largest == 0u) {
        return 0;
    }
    while (largest > PSXE_NORMALIZE_HIGH) {
        largest >>= 1;
        ++shift;
    }
    while (largest < PSXE_NORMALIZE_LOW) {
        largest <<= 1;
        --shift;
    }
    if (shift >= 0) {
        sx = (int32_t)((int64_t)x >> shift);
        sy = (int32_t)((int64_t)y >> shift);
        sz = (int32_t)((int64_t)z >> shift);
    } else {
        sx = x * (1 << -shift);
        sy = y * (1 << -shift);
        sz = z * (1 << -shift);
    }

    length = psxe_isqrt((uint32_t)(sx * sx) + (uint32_t)(sy * sy) + (uint32_t)(sz * sz));
    if (length == 0u) {
        return 0;
    }
    if (out != NULL) {
        out->vx = (int16_t)((sx * ONE) / (int32_t)length);
        out->vy = (int16_t)((sy * ONE) / (int32_t)length);
        out->vz = (int16_t)((sz * ONE) / (int32_t)length);
        out->pad = 0;
    }

    real_length = (shift >= 0) ? ((int64_t)length << shift) : ((int64_t)length >> -shift);
    if (real_length < 1) {
        real_length = 1;
    }
    return (real_length > INT32_MAX) ? INT32_MAX : (int32_t)real_length;
}

void psxe_matrix_apply(const MATRIX *m, int32_t x, int32_t y, int32_t z, VECTOR *out) {
    int row;
    int32_t *result[3] = { &out->vx, &out->vy, &out->vz };

    for (row = 0; row < 3; ++row) {
        int64_t sum = ((int64_t)m->m[row][0] * x) +
                      ((int64_t)m->m[row][1] * y) +
                      ((int64_t)m->m[row][2] * z);
        *result[row] = (int32_t)(sum >> 12);
    }
}

/* ---- Light value helpers (never touch GTE state) ------------------------ */

void psxe_light_init(psxe_light *light, psxe_light_type type) {
    if (light == NULL) {
        return;
    }
    memset(light, 0, sizeof(*light));
    light->direction.vy = -ONE; /* from below toward above (Y points down) */
    light->color = (psxe_color){ 255, 255, 255 };
    light->type = (uint8_t)type;
    light->falloff = PSXE_FALLOFF_LINEAR;
    light->layers = PSXE_LIGHT_LAYER_DEFAULT;
    light->enabled = true;
    light->intensity = ONE;
    light->radius = 1024;
}

void psxe_light_set_color(psxe_light *light, psxe_color color, int32_t intensity) {
    if (light != NULL) {
        light->color = color;
        light->intensity = (intensity < 0) ? 0 : intensity;
    }
}

void psxe_light_set_direction(psxe_light *light, int32_t x, int32_t y, int32_t z) {
    SVECTOR direction;
    if ((light != NULL) && (psxe_vector_normalize(x, y, z, &direction) > 0)) {
        light->direction = direction;
    }
}

void psxe_light_set_position(psxe_light *light, int32_t x, int32_t y, int32_t z) {
    if (light != NULL) {
        light->position.vx = x;
        light->position.vy = y;
        light->position.vz = z;
    }
}

void psxe_light_set_range(psxe_light *light, int32_t radius, psxe_light_falloff falloff) {
    if (light != NULL) {
        light->radius = (radius < 0) ? 0 : radius;
        light->falloff = ((unsigned int)falloff <= PSXE_FALLOFF_NONE) ? (uint8_t)falloff
                                                                     : (uint8_t)PSXE_FALLOFF_LINEAR;
    }
}

void psxe_light_set_layers(psxe_light *light, uint8_t layers) {
    if (light != NULL) {
        light->layers = layers;
    }
}

void psxe_light_set_enabled(psxe_light *light, bool enabled) {
    if (light != NULL) {
        light->enabled = enabled;
    }
}

/* ---- Renderer light set ------------------------------------------------- */

void psxe_renderer_clear_lights(psxe_renderer *renderer) {
    if (renderer != NULL) {
        renderer->lights.count = 0;
    }
}

bool psxe_renderer_add_light(psxe_renderer *renderer, const psxe_light *light) {
    if ((renderer == NULL) || (light == NULL)) {
        return false;
    }
    if (renderer->lights.count >= PSXE_MAX_LIGHTS) {
        ++renderer->lights_dropped;
        return false;
    }
    renderer->lights.lights[renderer->lights.count++] = *light;
    return true;
}

uint16_t psxe_renderer_set_lights(psxe_renderer *renderer, const psxe_light *lights, uint16_t count) {
    uint16_t index;
    if (renderer == NULL) {
        return 0;
    }
    renderer->lights.count = 0;
    if (lights == NULL) {
        return 0;
    }
    for (index = 0; index < count; ++index) {
        psxe_renderer_add_light(renderer, &lights[index]);
    }
    return renderer->lights.count;
}

psxe_light *psxe_renderer_light(psxe_renderer *renderer, uint16_t index) {
    if ((renderer == NULL) || (index >= renderer->lights.count)) {
        return NULL;
    }
    return &renderer->lights.lights[index];
}

void psxe_renderer_set_ambient(psxe_renderer *renderer, psxe_color ambient) {
    if (renderer != NULL) {
        renderer->lights.ambient = ambient;
    }
}

void psxe_renderer_set_directional_light(
    psxe_renderer *renderer,
    const SVECTOR *direction,
    psxe_color color,
    psxe_color ambient
) {
    psxe_light light;

    if ((renderer == NULL) || (direction == NULL)) {
        return;
    }
    psxe_light_init(&light, PSXE_LIGHT_DIRECTIONAL);
    psxe_light_set_direction(&light, direction->vx, direction->vy, direction->vz);
    psxe_light_set_color(&light, color, ONE);
    psxe_renderer_clear_lights(renderer);
    psxe_renderer_add_light(renderer, &light);
    psxe_renderer_set_ambient(renderer, ambient);
}

/* ---- Per-mesh selection ------------------------------------------------- */

static void psxe_scale_color(psxe_color color, int32_t intensity, int32_t rgb[3]) {
    rgb[0] = (int32_t)(((int64_t)color.r * intensity) / 255);
    rgb[1] = (int32_t)(((int64_t)color.g * intensity) / 255);
    rgb[2] = (int32_t)(((int64_t)color.b * intensity) / 255);
}

/* Lights without a GTE row are spread over the back color. A quarter is the
 * mean of max(0, cos) over all normal directions. */
static void psxe_fold_light(psxe_renderer *renderer, psxe_light_state *state, const int32_t rgb[3]) {
    state->back[0] += rgb[0] >> 2;
    state->back[1] += rgb[1] >> 2;
    state->back[2] += rgb[2] >> 2;
    ++renderer->lights_folded;
}

static void psxe_consider_light(
    psxe_renderer *renderer,
    const psxe_light *light,
    const VECTOR *origin,
    uint8_t exclude_mask,
    psxe_light_candidate best[3],
    unsigned int *best_count,
    psxe_light_state *state
) {
    psxe_light_candidate candidate;
    int32_t intensity;
    unsigned int index;
    unsigned int weakest;

    if (!light->enabled || ((light->layers & exclude_mask) != 0u)) {
        return;
    }
    ++renderer->lights_considered;
    intensity = light->intensity;
    if (intensity <= 0) {
        return;
    }

    switch (light->type) {
    case PSXE_LIGHT_AMBIENT:
        psxe_scale_color(light->color, intensity, candidate.rgb);
        state->back[0] += candidate.rgb[0];
        state->back[1] += candidate.rgb[1];
        state->back[2] += candidate.rgb[2];
        ++renderer->lights_applied;
        return;
    case PSXE_LIGHT_DIRECTIONAL:
        candidate.direction = light->direction;
        break;
    case PSXE_LIGHT_POINT: {
        int32_t distance = psxe_vector_normalize(
            light->position.vx - origin->vx,
            light->position.vy - origin->vy,
            light->position.vz - origin->vz,
            &candidate.direction
        );
        if (light->falloff != PSXE_FALLOFF_NONE) {
            int64_t t;
            if ((light->radius <= 0) || (distance >= light->radius)) {
                return;
            }
            t = ((int64_t)(light->radius - distance) << 12) / light->radius;
            if (light->falloff == PSXE_FALLOFF_SMOOTH) {
                t = (t * t) >> 12;
            }
            intensity = (int32_t)(((int64_t)intensity * t) >> 12);
            if (intensity <= 0) {
                return;
            }
        }
        if (distance == 0) {
            /* Light at the origin has no direction: it surrounds the mesh. */
            psxe_scale_color(light->color, intensity, candidate.rgb);
            state->back[0] += candidate.rgb[0];
            state->back[1] += candidate.rgb[1];
            state->back[2] += candidate.rgb[2];
            ++renderer->lights_applied;
            return;
        }
        break;
    }
    default:
        return;
    }

    psxe_scale_color(light->color, intensity, candidate.rgb);
    candidate.score = candidate.rgb[0] + candidate.rgb[1] + candidate.rgb[2];
    if (candidate.score <= 0) {
        return;
    }

    if (*best_count < 3u) {
        best[(*best_count)++] = candidate;
        return;
    }
    weakest = 0;
    for (index = 1; index < 3u; ++index) {
        if (best[index].score < best[weakest].score) {
            weakest = index;
        }
    }
    if (candidate.score > best[weakest].score) {
        psxe_fold_light(renderer, state, best[weakest].rgb);
        best[weakest] = candidate;
    } else {
        psxe_fold_light(renderer, state, candidate.rgb);
    }
}

static int16_t psxe_clamp_lcm(int32_t value) {
    if (value < 0) {
        return 0;
    }
    return (int16_t)((value > 0x7fff) ? 0x7fff : value);
}

void psxe_light_select(
    psxe_renderer *renderer,
    const VECTOR *origin,
    uint8_t exclude_mask,
    const psxe_light *extra_lights,
    uint16_t extra_count,
    psxe_light_state *state
) {
    psxe_light_candidate best[3];
    unsigned int best_count = 0;
    unsigned int index;
    int32_t ambient[3];

    memset(state, 0, sizeof(*state));
    for (index = 0; index < renderer->lights.count; ++index) {
        psxe_consider_light(
            renderer, &renderer->lights.lights[index], origin, exclude_mask,
            best, &best_count, state
        );
    }
    if (extra_lights != NULL) {
        for (index = 0; index < extra_count; ++index) {
            psxe_consider_light(
                renderer, &extra_lights[index], origin, exclude_mask,
                best, &best_count, state
            );
        }
    }

    for (index = 0; index < best_count; ++index) {
        state->light.m[index][0] = best[index].direction.vx;
        state->light.m[index][1] = best[index].direction.vy;
        state->light.m[index][2] = best[index].direction.vz;
        state->color.m[0][index] = psxe_clamp_lcm(best[index].rgb[0]);
        state->color.m[1][index] = psxe_clamp_lcm(best[index].rgb[1]);
        state->color.m[2][index] = psxe_clamp_lcm(best[index].rgb[2]);
    }
    renderer->lights_applied += best_count;

    psxe_scale_color(renderer->lights.ambient, ONE, ambient);
    state->back[0] += ambient[0];
    state->back[1] += ambient[1];
    state->back[2] += ambient[2];
}

/* ---- Debug draw --------------------------------------------------------- */

void psxe_draw_light_marker(psxe_renderer *renderer, const psxe_light *light, int16_t size) {
    SVECTOR inverse_camera_rotation;
    MATRIX view;
    VECTOR view_position;
    int32_t center_x;
    int32_t center_y;
    int32_t screen_x;
    int32_t screen_y;
    TILE *tile;

    if ((renderer == NULL) || (light == NULL) || !light->enabled ||
        (light->type != PSXE_LIGHT_POINT) || (size <= 0)) {
        return;
    }

    inverse_camera_rotation.vx = (int16_t)-renderer->camera.rotation.vx;
    inverse_camera_rotation.vy = (int16_t)-renderer->camera.rotation.vy;
    inverse_camera_rotation.vz = (int16_t)-renderer->camera.rotation.vz;
    inverse_camera_rotation.pad = 0;
    RotMatrix(&inverse_camera_rotation, &view);
    psxe_matrix_apply(
        &view,
        light->position.vx - renderer->camera.position.vx,
        light->position.vy - renderer->camera.position.vy,
        light->position.vz - renderer->camera.position.vz,
        &view_position
    );
    if (view_position.vz <= 0) {
        return;
    }

    if (renderer->active_target != NULL) {
        center_x = renderer->active_target->width / 2;
        center_y = renderer->active_target->height / 2;
    } else {
        center_x = renderer->width / 2;
        center_y = renderer->height / 2;
    }
    screen_x = center_x + (int32_t)(((int64_t)view_position.vx * renderer->camera.projection) / view_position.vz);
    screen_y = center_y + (int32_t)(((int64_t)view_position.vy * renderer->camera.projection) / view_position.vz);
    if ((screen_x < -512) || (screen_x > 1023) || (screen_y < -512) || (screen_y > 1023)) {
        return;
    }

    tile = psxe_renderer_allocate(renderer, sizeof(*tile));
    if (tile == NULL) {
        return;
    }
    setTile(tile);
    setXY0(tile, screen_x - (size / 2), screen_y - (size / 2));
    setWH(tile, size, size);
    setRGB0(tile, light->color.r, light->color.g, light->color.b);
    addPrim(psxe_renderer_bucket(renderer, 1), tile);
    ++renderer->submitted_primitives;
}
