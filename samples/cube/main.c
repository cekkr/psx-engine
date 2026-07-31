#include <stdio.h>
#include <string.h>

#include <psxe/psxe.h>

typedef struct cube_save {
    SVECTOR rotation;
} cube_save;

typedef struct cube_game {
    psxe_scene scene;
    psxe_entity_id cube;
    psxe_camera camera;
    char status[64];
} cube_game;

static const SVECTOR cube_vertices[] = {
    { -100, -100, -100, 0 }, {  100, -100, -100, 0 },
    { -100,  100, -100, 0 }, {  100,  100, -100, 0 },
    {  100, -100,  100, 0 }, { -100, -100,  100, 0 },
    {  100,  100,  100, 0 }, { -100,  100,  100, 0 }
};

static const SVECTOR cube_normals[] = {
    { 0, 0, -ONE, 0 }, { 0, 0, ONE, 0 },
    { 0, -ONE, 0, 0 }, { 0, ONE, 0, 0 },
    { -ONE, 0, 0, 0 }, { ONE, 0, 0, 0 }
};

#define CUBE_FACE(a, b, c, n, red, green, blue) \
    { { (a), (b), (c) }, (n), { { 0, 0 }, { 0, 0 }, { 0, 0 } }, \
      { (red), (green), (blue) }, PSXE_MATERIAL_LIT }

static const psxe_face cube_faces[] = {
    CUBE_FACE(0, 1, 2, 0, 220, 80, 80), CUBE_FACE(1, 3, 2, 0, 220, 80, 80),
    CUBE_FACE(4, 5, 6, 1, 80, 200, 110), CUBE_FACE(5, 7, 6, 1, 80, 200, 110),
    CUBE_FACE(5, 4, 0, 2, 80, 110, 220), CUBE_FACE(4, 1, 0, 2, 80, 110, 220),
    CUBE_FACE(6, 7, 3, 3, 220, 200, 80), CUBE_FACE(7, 2, 3, 3, 220, 200, 80),
    CUBE_FACE(0, 2, 5, 4, 180, 80, 210), CUBE_FACE(2, 7, 5, 4, 180, 80, 210),
    CUBE_FACE(3, 1, 6, 5, 70, 200, 210), CUBE_FACE(1, 4, 6, 5, 70, 200, 210)
};

static const psxe_mesh cube_mesh = {
    .vertices = cube_vertices,
    .normals = cube_normals,
    .faces = cube_faces,
    .vertex_count = PSXE_ARRAY_COUNT(cube_vertices),
    .normal_count = PSXE_ARRAY_COUNT(cube_normals),
    .face_count = PSXE_ARRAY_COUNT(cube_faces),
    .texture = NULL
};

static void game_init(psxe_engine *engine, void *user_data) {
    cube_game *game = user_data;
    psxe_entity *entity;

    psxe_scene_init(&game->scene);
    game->cube = psxe_scene_create(&game->scene);
    entity = psxe_scene_get(&game->scene, game->cube);
    entity->mesh = &cube_mesh;
    entity->transform.position.vz = 900;

    game->camera = (psxe_camera){
        .position = { 0, 0, 0 },
        .rotation = { 0, 0, 0, 0 },
        .projection = 180
    };
    psxe_renderer_set_camera(&engine->renderer, &game->camera);
    snprintf(game->status, sizeof(game->status), "START: SAVE  SELECT: LOAD");
}

static void game_update(psxe_engine *engine, void *user_data) {
    cube_game *game = user_data;
    psxe_entity *entity = psxe_scene_get(&game->scene, game->cube);
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);

    entity->transform.rotation.vx += 10;
    entity->transform.rotation.vy += 14;

    if (psxe_button_held(pad, PAD_LEFT)) {
        entity->transform.position.vx -= 5;
    }
    if (psxe_button_held(pad, PAD_RIGHT)) {
        entity->transform.position.vx += 5;
    }
    if (psxe_button_held(pad, PAD_UP)) {
        entity->transform.position.vz -= 5;
    }
    if (psxe_button_held(pad, PAD_DOWN)) {
        entity->transform.position.vz += 5;
    }

    if (psxe_button_pressed(pad, PAD_START)) {
        cube_save save = { .rotation = entity->transform.rotation };
        psxe_memory_card_result result = psxe_memory_card_save(
            &engine->memory_card, 0, &save, sizeof(save)
        );
        snprintf(game->status, sizeof(game->status), "SAVE: %s", psxe_memory_card_result_string(result));
    }
    if (psxe_button_pressed(pad, PAD_SELECT)) {
        cube_save save;
        size_t loaded_size = 0;
        psxe_memory_card_result result = psxe_memory_card_load(
            &engine->memory_card, 0, &save, sizeof(save), &loaded_size
        );
        if ((result == PSXE_MEMORY_CARD_OK) && (loaded_size == sizeof(save))) {
            entity->transform.rotation = save.rotation;
        }
        snprintf(game->status, sizeof(game->status), "LOAD: %s", psxe_memory_card_result_string(result));
    }
}

static void game_render(psxe_engine *engine, void *user_data) {
    cube_game *game = user_data;
    char diagnostics[64];

    psxe_scene_render(&game->scene, &engine->renderer);
    psxe_ui_panel(&engine->renderer, 4, 4, 312, 34, (psxe_color){ 16, 16, 28 }, true);
    psxe_ui_text(&engine->renderer, 10, 10, "PSX-ENGINE / 3D FOUNDATION");
    psxe_ui_text(&engine->renderer, 10, 20, game->status);
    snprintf(
        diagnostics,
        sizeof(diagnostics),
        "FRAME %lu  PRIMS %lu  DROP %lu",
        (unsigned long)engine->frame,
        (unsigned long)engine->renderer.submitted_primitives,
        (unsigned long)engine->renderer.dropped_primitives
    );
    psxe_ui_text(&engine->renderer, 10, 228, diagnostics);
}

int main(void) {
    static psxe_engine engine;
    static cube_game state;
    const psxe_engine_config config = {
        .render = {
            .width = 320,
            .height = 240,
            .video_mode = PSXE_VIDEO_NTSC,
            .clear_color = { 22, 28, 48 },
            .dither = true
        },
        .memory_card = {
            .product_code = "BAPXE-00001",
            .identifier = "CUBE",
            .title = "PSX-ENGINE CUBE SAMPLE",
            .format_version = 1
        }
    };
    const psxe_game game = {
        .init = game_init,
        .update = game_update,
        .render = game_render,
        .shutdown = NULL,
        .user_data = &state
    };

    memset(&state, 0, sizeof(state));
    psxe_engine_init(&engine, &config);
    psxe_engine_run(&engine, &game);
    return 0;
}
