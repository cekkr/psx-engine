#ifndef PSXE_ANIMATION_H
#define PSXE_ANIMATION_H

#include "psxe/render.h"

/* Equally spaced poses, stored frame-major in the mesh's vertex/normal order.
 * Without a normal stream, samples keep the mesh's original normals. */
typedef struct psxe_anim_clip {
    const psxe_mesh *mesh;
    const SVECTOR *vertices;
    size_t vertex_samples;
    const SVECTOR *normals;
    size_t normal_samples;
    uint16_t frame_count;
    uint16_t frame_ms;
} psxe_anim_clip;

/* Zero-initialize before use. Use the API to change playback state. */
typedef struct psxe_anim_player {
    const psxe_anim_clip *clip;
    uint32_t time_ms;
    bool loop;
} psxe_anim_player;

/* Borrows immutable clip data. A successful call restarts at time zero. */
bool psxe_anim_play(psxe_anim_player *player, const psxe_anim_clip *clip, bool loop);
void psxe_anim_seek(psxe_anim_player *player, uint32_t time_ms);
void psxe_anim_advance(psxe_anim_player *player, uint32_t elapsed_ms);
/* True when a non-looping clip has reached its last pose. */
bool psxe_anim_finished(const psxe_anim_player *player);

/* Writes a drawable mesh view backed by caller-owned pose buffers. Capacities
 * are in SVECTORs. Pass NULL/0 for normals if the clip has no normal stream.
 * Output storage must be separate from clip data and from the other outputs. */
bool psxe_anim_sample(
    const psxe_anim_player *player, psxe_mesh *mesh,
    SVECTOR *vertices, size_t vertex_capacity,
    SVECTOR *normals, size_t normal_capacity
);

#endif
