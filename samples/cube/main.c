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
    psxe_render_target targets[2];
    psxe_object mini_cube;     /* drawn only inside the render targets */
    psxe_object screen;        /* quad textured with a render target */
    psxe_sprite preview;       /* scaled 2D view of the same target */
    unsigned int shown_target;
    unsigned int effect;
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

static const SVECTOR quad_vertices[] = {
    { -100, -100, 0, 0 }, { 100, -100, 0, 0 },
    { -100,  100, 0, 0 }, { 100,  100, 0, 0 }
};

#define QUAD_FLAGS (PSXE_MATERIAL_TEXTURED | PSXE_MATERIAL_DOUBLE_SIDED)

/* UVs are offsets from the texture base; the render targets are 128x128. */
static const psxe_face quad_faces[] = {
    { { 0, 1, 2 }, 0, { { 0, 0 }, { 127, 0 }, { 0, 127 } }, { 128, 128, 128 }, QUAD_FLAGS },
    { { 1, 3, 2 }, 0, { { 127, 0 }, { 127, 127 }, { 0, 127 } }, { 128, 128, 128 }, QUAD_FLAGS }
};

static const psxe_mesh quad_mesh = {
    .vertices = quad_vertices,
    .normals = NULL,
    .faces = quad_faces,
    .vertex_count = PSXE_ARRAY_COUNT(quad_vertices),
    .normal_count = 0,
    .face_count = PSXE_ARRAY_COUNT(quad_faces),
    .texture = NULL
};

static const char *const effect_names[] = {
    "NORMAL", "TINT RED", "UNLIT", "ADD BLEND", "FLAT WHITE"
};

static void apply_effect(psxe_entity *entity, unsigned int effect) {
    psxe_material *material = &entity->material;

    *material = psxe_material_default();
    switch (effect) {
    case 1:
        psxe_material_set_color(material, (psxe_color){ 255, 96, 96 }, PSXE_COLOR_TINT);
        break;
    case 2:
        psxe_material_set_effects(material, PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);
        break;
    case 3:
        psxe_material_set_effects(material, PSXE_EFFECT_SEMITRANSPARENT, PSXE_BLEND_ADD);
        break;
    case 4:
        psxe_material_set_color(material, (psxe_color){ 255, 255, 255 }, PSXE_COLOR_REPLACE);
        psxe_material_set_effects(material, PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);
        break;
    default:
        break;
    }
}

static void show_target(cube_game *game, unsigned int index) {
    const psxe_texture *texture = psxe_render_target_texture(&game->targets[index]);
    uint8_t flips = (uint8_t)(game->preview.material.effects & (PSXE_EFFECT_FLIP_X | PSXE_EFFECT_FLIP_Y));

    game->shown_target = index;
    psxe_object_set_texture(&game->screen, texture);
    /* Keeps position and the 64x64 on-screen size; region resets to 128x128. */
    psxe_sprite_set_texture(&game->preview, texture);
    psxe_sprite_set_effects(&game->preview, flips, PSXE_BLEND_HALF);
}

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

    {
        psxe_render_target_config target_config = {
            .x = 320, .y = 0, .width = 128, .height = 128,
            .clear_color = { 40, 16, 64 }, .clear = true,
            .ot_length = 256, .depth_shift = 2
        };
        /* Clear colors avoid pure black: texel 0x0000 is transparent. */
        psxe_render_target_init(&game->targets[0], &engine->renderer, &target_config);
        target_config.x = 448;
        target_config.clear_color = (psxe_color){ 16, 64, 40 };
        psxe_render_target_init(&game->targets[1], &engine->renderer, &target_config);
    }

    psxe_object_init(&game->mini_cube, &cube_mesh);
    psxe_object_set_position(&game->mini_cube, 0, 0, 400);

    psxe_object_init(&game->screen, &quad_mesh);
    psxe_object_set_position(&game->screen, -230, 0, 700);

    psxe_sprite_init(&game->preview, NULL);
    psxe_sprite_set_position(&game->preview, 248, 44);
    psxe_sprite_set_size(&game->preview, 64, 64);
    psxe_sprite_set_color(&game->preview, (psxe_color){ 128, 128, 128 });
    show_target(game, 0);

    snprintf(game->status, sizeof(game->status), "START: SAVE  SELECT: LOAD");
}

static void game_update(psxe_engine *engine, void *user_data) {
    cube_game *game = user_data;
    psxe_entity *entity = psxe_scene_get(&game->scene, game->cube);
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);

    entity->transform.rotation.vx += 10;
    entity->transform.rotation.vy += 14;
    psxe_object_rotate(&game->mini_cube, 0, 24, 8);
    psxe_object_rotate(&game->screen, 0, 12, 0);

    if (psxe_button_pressed(pad, PAD_CROSS)) {
        game->effect = (game->effect + 1u) % (unsigned int)PSXE_ARRAY_COUNT(effect_names);
        apply_effect(entity, game->effect);
        snprintf(game->status, sizeof(game->status), "EFFECT: %s", effect_names[game->effect]);
    }
    if (psxe_button_pressed(pad, PAD_CIRCLE)) {
        show_target(game, game->shown_target ^ 1u);
        snprintf(game->status, sizeof(game->status), "SCREEN: TARGET %c", game->shown_target ? 'B' : 'A');
    }
    if (psxe_button_pressed(pad, PAD_TRIANGLE)) {
        game->preview.material.effects ^= PSXE_EFFECT_FLIP_X;
    }

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
    psxe_renderer *renderer = &engine->renderer;
    static const psxe_camera target_camera = {
        .position = { 0, 0, 0 }, .rotation = { 0, 0, 0, 0 }, .projection = 120
    };
    static const char *const target_labels[] = { "TARGET A", "TARGET B" };
    char diagnostics[64];
    unsigned int index;

    /* Offscreen passes: both targets are rendered every frame, before the
     * main pass samples them. Camera and GTE offset are restored by end. */
    for (index = 0; index < 2u; ++index) {
        if (psxe_render_target_begin(renderer, &game->targets[index])) {
            psxe_renderer_set_camera(renderer, &target_camera);
            psxe_object_set_color(
                &game->mini_cube,
                (index == 0u) ? (psxe_color){ 128, 128, 128 } : (psxe_color){ 80, 255, 160 },
                (index == 0u) ? PSXE_COLOR_FACE : PSXE_COLOR_TINT
            );
            psxe_object_draw(renderer, &game->mini_cube);
            psxe_ui_text(renderer, 4, 4, target_labels[index]);
            psxe_render_target_end(renderer);
        }
    }

    psxe_scene_render(&game->scene, renderer);
    psxe_object_draw(renderer, &game->screen);
    psxe_sprite_draw(renderer, &game->preview);
    psxe_ui_panel(&engine->renderer, 4, 4, 312, 34, (psxe_color){ 16, 16, 28 }, true);
    psxe_ui_text(&engine->renderer, 10, 10, "PSX-ENGINE / 3D FOUNDATION");
    psxe_ui_text(&engine->renderer, 10, 20, game->status);
    snprintf(
        diagnostics,
        sizeof(diagnostics),
        "FRAME %lu  PRIMS %lu  DROP %lu  RT %u",
        (unsigned long)engine->frame,
        (unsigned long)engine->renderer.submitted_primitives,
        (unsigned long)engine->renderer.dropped_primitives,
        (unsigned int)engine->renderer.target_passes
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
