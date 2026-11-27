#include <psxe/psxe.h>

#include "assets/mesh.inc"

extern const unsigned char carrot_tim[];
extern const uint32_t carrot_tim_size;
extern const SVECTOR carrot_poses[];
extern const uint32_t carrot_poses_size;

enum {
    IDLE_FRAMES = 4,
    WALK_FRAMES = COOKED_POSE_COUNT - IDLE_FRAMES,
    GRID_TILES = 8,
    GRID_VERTICES = (GRID_TILES + 1) * (GRID_TILES + 1),
    GRID_FACES = GRID_TILES * GRID_TILES * 2,
    TILE_SIZE = 48,
    FLOOR_X = -192,
    FLOOR_Z = -144,
    EDGE_MARGIN = 38,
    WALK_SPEED = 100
};

static psxe_engine engine;
static psxe_vram vram;
static psxe_anim_player player;
static psxe_object carrot;
static psxe_mesh pose;
static SVECTOR pose_vertices[COOKED_VERTEX_COUNT];
static SVECTOR floor_vertices[GRID_VERTICES];
static psxe_face floor_faces[GRID_FACES];
static int32_t x, z; /* Fractional world positions in 20.12 units. */
static uint32_t ms_remainder;
static bool ready;

static const psxe_mesh carrot_mesh = {
    .vertices = carrot_poses,
    .faces = cooked_faces,
    .vertex_count = COOKED_VERTEX_COUNT,
    .face_count = PSXE_ARRAY_COUNT(cooked_faces)
};

static const psxe_anim_clip idle = {
    .mesh = &carrot_mesh,
    .vertices = carrot_poses,
    .vertex_samples = IDLE_FRAMES * COOKED_VERTEX_COUNT,
    .frame_count = IDLE_FRAMES,
    .frame_ms = 250
};
static const psxe_anim_clip walk = {
    .mesh = &carrot_mesh,
    .vertices = &carrot_poses[IDLE_FRAMES * COOKED_VERTEX_COUNT],
    .vertex_samples = WALK_FRAMES * COOKED_VERTEX_COUNT,
    .frame_count = WALK_FRAMES,
    .frame_ms = 72
};

static const psxe_mesh floor_mesh = {
    .vertices = floor_vertices,
    .faces = floor_faces,
    .vertex_count = GRID_VERTICES,
    .face_count = GRID_FACES
};
static const psxe_transform floor_transform = {
    .scale = { ONE, ONE, ONE }
};
static const psxe_camera camera = {
    .position = { 0, -230, -500 },
    .rotation = { -235, 0, 0, 0 },
    .projection = 272
};

static void build_floor(void) {
    for (int row = 0; row <= GRID_TILES; ++row) {
        for (int col = 0; col <= GRID_TILES; ++col) {
            floor_vertices[row * (GRID_TILES + 1) + col] = (SVECTOR){
                (int16_t)(FLOOR_X + col * TILE_SIZE), 1, (int16_t)(FLOOR_Z + row * TILE_SIZE), 0
            };
        }
    }
    for (int row = 0; row < GRID_TILES; ++row) {
        for (int col = 0; col < GRID_TILES; ++col) {
            uint16_t a = (uint16_t)(row * (GRID_TILES + 1) + col);
            uint16_t b = a + 1;
            uint16_t c = a + GRID_TILES + 1;
            uint16_t d = c + 1;
            size_t tile = (size_t)(row * GRID_TILES + col);
            psxe_color color = ((row + col) & 1) ? (psxe_color){ 58, 81, 73 } : (psxe_color){ 69, 94, 81 };
            if (!row || !col || row == GRID_TILES - 1 || col == GRID_TILES - 1) {
                color = (psxe_color){ 45, 63, 62 };
            }
            floor_faces[2 * tile] = (psxe_face){ .vertex = { a, c, b }, .color = color };
            floor_faces[2 * tile + 1] = (psxe_face){ .vertex = { b, c, d }, .color = color };
        }
    }
}

static void game_init(psxe_engine *game, void *user_data) {
    (void)user_data;
    psxe_vram_id texture_id;
    psxe_vram_init(&vram, &game->renderer);
    build_floor();
    psxe_renderer_set_camera(&game->renderer, &camera);

    if (carrot_poses_size != COOKED_POSE_COUNT * sizeof(pose_vertices) ||
        !psxe_vram_load_tim(&vram, carrot_tim, carrot_tim_size, &texture_id) ||
        !psxe_anim_play(&player, &walk, true) ||
        !psxe_anim_play(&player, &idle, true)) {
        return;
    }
    psxe_object_init(&carrot, &pose);
    psxe_object_set_texture(&carrot, psxe_vram_get(&vram, texture_id));
    psxe_object_set_effects(&carrot, PSXE_EFFECT_RAW_TEXTURE | PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);
    ready = psxe_anim_sample(&player, &pose, pose_vertices, COOKED_VERTEX_COUNT, NULL, 0);
}

static int axis(
    const psxe_pad_state *pad, PadButton negative, PadButton positive, uint8_t stick
) {
    if (psxe_button_held(pad, negative) || psxe_button_held(pad, positive)) {
        return (int)psxe_button_held(pad, positive) - (int)psxe_button_held(pad, negative);
    }
    return (stick > 157) - (stick < 98);
}

static int32_t clamp(int32_t position, int32_t low, int32_t high) {
    if (position < low) {
        return low;
    }
    return (position > high) ? high : position;
}

static void game_update(psxe_engine *game, void *user_data) {
    (void)user_data;
    if (!ready) {
        return;
    }

    const psxe_pad_state *pad = psxe_input_pad(&game->input, 0);
    int dx = 0;
    int dz = 0;
    if (pad && pad->connected) {
        dx = axis(pad, PAD_LEFT, PAD_RIGHT, pad->left_x);
        dz = axis(pad, PAD_UP, PAD_DOWN, pad->left_y);
    }
    const psxe_anim_clip *next = &idle;
    if (dx || dz) {
        static const int16_t yaw[3][3] = {
            { 512, 0, -512 }, { 1024, 0, -1024 }, { 1536, 2048, -1536 }
        };
        const int32_t direction_scale = (dx && dz) ? 2896 : ONE;
        const int32_t step = (WALK_SPEED * direction_scale) / game->ticks_per_second;
        int32_t next_x = clamp(x + dx * step, (FLOOR_X + EDGE_MARGIN) * ONE,
                              (FLOOR_X + GRID_TILES * TILE_SIZE - EDGE_MARGIN) * ONE);
        int32_t next_z = clamp(z + dz * step, (FLOOR_Z + EDGE_MARGIN) * ONE,
                              (FLOOR_Z + GRID_TILES * TILE_SIZE - EDGE_MARGIN) * ONE);
        if (next_x != x || next_z != z) {
            next = &walk;
        }
        x = next_x;
        z = next_z;
        psxe_object_set_position(&carrot, x / ONE, 0, z / ONE);
        psxe_object_set_rotation(&carrot, 0, yaw[dz + 1][dx + 1], 0);
    }
    ms_remainder += 1000u;
    uint32_t elapsed = ms_remainder / game->ticks_per_second;
    ms_remainder %= game->ticks_per_second;
    if (player.clip != next) {
        (void)psxe_anim_play(&player, next, true);
    } else {
        psxe_anim_advance(&player, elapsed);
    }
    (void)psxe_anim_sample(&player, &pose, pose_vertices, COOKED_VERTEX_COUNT, NULL, 0);
}

static void game_render(psxe_engine *game, void *user_data) {
    (void)user_data;
    psxe_renderer *renderer = &game->renderer;
    psxe_draw_mesh(renderer, &floor_mesh, &floor_transform);
    if (ready) {
        psxe_object_draw(renderer, &carrot);
    }

    psxe_ui_panel(renderer, 0, 0, 320, 27, (psxe_color){ 19, 27, 34 }, false);
    psxe_ui_text(renderer, 10, 9, "CARROT MAN");
    psxe_ui_text(renderer, 148, 9, "D-PAD / LEFT STICK");
    if (!ready) {
        psxe_ui_text(renderer, 10, 224, "COULD NOT LOAD CARROT ASSETS");
    }
}

int main(void) {
    const psxe_engine_config config = {
        .render = {
            .width = 320, .height = 240, .video_mode = PSXE_VIDEO_NTSC,
            .clear_color = { 27, 40, 51 }, .dither = true
        }
    };
    const psxe_game game = { .init = game_init, .update = game_update, .render = game_render };
    psxe_engine_init(&engine, &config);
    psxe_engine_run(&engine, &game);
    return 0;
}
