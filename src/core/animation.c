#include "psxe/animation.h"

#include <string.h>

bool psxe_anim_play(psxe_anim_player *player, const psxe_anim_clip *clip, bool loop) {
    if (!player || !clip || !clip->mesh || !clip->vertices ||
        !clip->frame_count || !clip->frame_ms || !clip->mesh->vertex_count ||
        clip->vertex_samples > SIZE_MAX / sizeof(SVECTOR) ||
        clip->mesh->vertex_count > clip->vertex_samples / clip->frame_count ||
        (clip->mesh->face_count && !clip->mesh->faces)) {
        return false;
    }
    if (clip->normals) {
        if (!clip->mesh->normal_count ||
            clip->normal_samples > SIZE_MAX / sizeof(SVECTOR) ||
            clip->mesh->normal_count > clip->normal_samples / clip->frame_count) {
            return false;
        }
    } else if (clip->normal_samples || (clip->mesh->normal_count && !clip->mesh->normals)) {
        return false;
    }
    *player = (psxe_anim_player){ .clip = clip, .loop = loop };
    return true;
}

static uint32_t psxe_anim_last_pose(const psxe_anim_clip *clip) {
    return (uint32_t)(clip->frame_count - 1u) * clip->frame_ms;
}

void psxe_anim_seek(psxe_anim_player *player, uint32_t time_ms) {
    if (!player || !player->clip) {
        return;
    }
    const psxe_anim_clip *clip = player->clip;
    uint32_t last = psxe_anim_last_pose(clip);
    if (player->loop) {
        player->time_ms = time_ms % (last + clip->frame_ms);
    } else {
        player->time_ms = (time_ms < last) ? time_ms : last;
    }
}

void psxe_anim_advance(psxe_anim_player *player, uint32_t elapsed_ms) {
    if (!player || !player->clip) {
        return;
    }
    const psxe_anim_clip *clip = player->clip;
    uint32_t last = psxe_anim_last_pose(clip);
    if (player->loop) {
        uint32_t duration = last + clip->frame_ms;
        uint32_t step = elapsed_ms % duration;
        uint32_t until_wrap = duration - player->time_ms;
        player->time_ms = (step < until_wrap) ? player->time_ms + step : step - until_wrap;
    } else {
        uint32_t remaining = last - player->time_ms;
        player->time_ms += (elapsed_ms < remaining) ? elapsed_ms : remaining;
    }
}

bool psxe_anim_finished(const psxe_anim_player *player) {
    return player && player->clip && !player->loop &&
           player->time_ms == psxe_anim_last_pose(player->clip);
}

static int16_t psxe_anim_mix(int16_t a, int16_t b, int32_t fraction) {
    return (int16_t)(((int32_t)a * (ONE - fraction) + (int32_t)b * fraction) / ONE);
}

static void psxe_anim_fill(
    SVECTOR *output, const SVECTOR *samples, size_t count,
    uint32_t first, uint32_t second, int32_t fraction
) {
    const SVECTOR *a = samples + first * count;
    if (!fraction || first == second) {
        memcpy(output, a, count * sizeof(*output));
        return;
    }
    const SVECTOR *b = samples + second * count;
    for (size_t i = 0; i < count; ++i) {
        output[i] = (SVECTOR){
            psxe_anim_mix(a[i].vx, b[i].vx, fraction),
            psxe_anim_mix(a[i].vy, b[i].vy, fraction),
            psxe_anim_mix(a[i].vz, b[i].vz, fraction),
            0
        };
    }
}

bool psxe_anim_sample(
    const psxe_anim_player *player, psxe_mesh *mesh,
    SVECTOR *vertices, size_t vertex_capacity,
    SVECTOR *normals, size_t normal_capacity
) {
    if (!player || !player->clip || !mesh || !vertices) {
        return false;
    }
    const psxe_anim_clip *clip = player->clip;
    size_t vertex_count = clip->mesh->vertex_count;
    size_t normal_count = clip->mesh->normal_count;
    if (vertex_capacity < vertex_count ||
        (clip->normals && (!normals || normal_capacity < normal_count))) {
        return false;
    }
    uint32_t first = player->time_ms / clip->frame_ms;
    uint32_t second = first + 1u;
    if (second == clip->frame_count) {
        second = player->loop ? 0 : first;
    }
    int32_t fraction = (int32_t)((player->time_ms % clip->frame_ms) * ONE / clip->frame_ms);
    psxe_anim_fill(vertices, clip->vertices, vertex_count, first, second, fraction);
    *mesh = *clip->mesh;
    mesh->vertices = vertices;
    if (clip->normals) {
        psxe_anim_fill(normals, clip->normals, normal_count, first, second, fraction);
        mesh->normals = normals;
    }
    return true;
}
