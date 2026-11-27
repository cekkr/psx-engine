#include <stdio.h>
#include <string.h>

#include <psxe/psxe.h>

typedef struct cube_save {
    SVECTOR rotation;
} cube_save;

typedef struct cube_game {
    psxe_scene scene;
    psxe_entity_id cube;
    psxe_entity_id lamp;       /* small unlit cube that owns a point light */
    psxe_camera camera;
    psxe_render_target targets[2];
    psxe_object mini_cube;     /* drawn only inside the render targets */
    psxe_object screen;        /* quad textured with a render target */
    psxe_sprite preview;       /* scaled 2D view of the same target */
    psxe_heap heap;
    uint32_t asset_ram[512];
    psxe_mesh_storage *painted_mesh;
    psxe_vram vram;
    psxe_vram_id paint;
    psxe_texture palette;
    psxe_audio_sample chime;
    unsigned int shown_target;
    unsigned int effect;
    unsigned int ambient;
    int16_t lamp_angle;
    bool show_markers;
    char status[64];
#ifdef PSXE_CUBE_DIAGNOSTICS
    char diagnostics[4][39];
#endif
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
    "NORMAL", "TINT RED", "UNLIT", "ADD BLEND", "FLAT WHITE", "BAKED WARM", "BAKED COOL"
};

static void apply_effect(cube_game *game, psxe_entity *entity, unsigned int effect) {
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
    case 5:
    case 6:
        if (psxe_vram_palette(&game->vram, game->paint, effect - 5u, &game->palette)) {
            psxe_material_set_texture(material, &game->palette);
            psxe_material_set_effects(material, PSXE_EFFECT_UNLIT | PSXE_EFFECT_RAW_TEXTURE, PSXE_BLEND_HALF);
        }
        break;
    default:
        break;
    }
}

static void load_assets(cube_game *game, psxe_engine *engine, psxe_entity *entity) {
    psxe_cd_file file;
    void *staging;
    bool paint_ready = false;

    psxe_heap_init(&game->heap, game->asset_ram, sizeof(game->asset_ram));
    psxe_vram_init(&game->vram, &engine->renderer);
    for (unsigned int i = 0; i < 2; ++i) {
        const psxe_render_target *target = &game->targets[i];
        if (!psxe_vram_reserve(&game->vram, target->x, target->y, target->width, target->height)) {
            return;
        }
    }
    if (psxe_cd_init() != PSXE_CD_OK) {
        return;
    }
    if (psxe_cd_find("/ASSETS/PAINTED.TIM", &file) == PSXE_CD_OK) {
        staging = psxe_heap_alloc(&game->heap, file.size);
        if (staging != NULL) {
            paint_ready = (psxe_cd_read_file(&file, 0, staging, file.size) == PSXE_CD_OK) &&
                          psxe_vram_load_tim(&game->vram, staging, file.size, &game->paint);
            psxe_heap_free(&game->heap, staging);
        }
    }
    if (psxe_cd_find("/ASSETS/CHIME.VAG", &file) == PSXE_CD_OK) {
        staging = psxe_heap_alloc(&game->heap, file.size);
        if (staging != NULL) {
            psxe_audio_load_vag(&engine->audio, &game->chime, &file, staging, file.size);
            psxe_heap_free(&game->heap, staging);
        }
    }
    if (!paint_ready) {
        return;
    }

    game->painted_mesh = psxe_mesh_alloc(&game->heap, cube_mesh.vertex_count, cube_mesh.normal_count,
                                       cube_mesh.face_count);
    if (game->painted_mesh == NULL) {
        psxe_vram_free(&game->vram, game->paint);
        return;
    }
    memcpy(game->painted_mesh->vertices, cube_vertices, sizeof(cube_vertices));
    memcpy(game->painted_mesh->normals, cube_normals, sizeof(cube_normals));
    memcpy(game->painted_mesh->faces, cube_faces, sizeof(cube_faces));
    for (unsigned int i = 0; i < cube_mesh.face_count; ++i) {
        static const uint8_t uv[2][3][2] = {
            { { 0, 0 }, { 31, 0 }, { 0, 31 } }, { { 31, 0 }, { 31, 31 }, { 0, 31 } }
        };
        psxe_face *face = &game->painted_mesh->faces[i];
        memcpy(face->uv, uv[i & 1u], sizeof(face->uv));
        face->flags |= PSXE_MATERIAL_TEXTURED;
    }
    entity->mesh = &game->painted_mesh->mesh;
    game->effect = 5;
    apply_effect(game, entity, game->effect);
}

static const psxe_color ambient_presets[] = {
    { 40, 40, 52 }, { 8, 8, 8 }, { 96, 80, 64 }
};
static const char *const ambient_names[] = { "COOL", "DARK", "WARM" };

#define LAMP_ORBIT 320
#define LAMP_HEIGHT -140

static void setup_lights(cube_game *game, psxe_renderer *renderer) {
    psxe_light sun;
    psxe_light lamp_light;
    psxe_entity *lamp;

    /* Static world light: dim blue directional from the upper left. */
    psxe_light_init(&sun, PSXE_LIGHT_DIRECTIONAL);
    psxe_light_set_direction(&sun, -ONE, -ONE, -ONE / 2);
    psxe_light_set_color(&sun, (psxe_color){ 110, 130, 190 }, ONE);
    psxe_renderer_clear_lights(renderer);
    psxe_renderer_add_light(renderer, &sun);
    psxe_renderer_set_ambient(renderer, ambient_presets[0]);

    /* Moving point light owned by a scene entity. Light position is an
     * entity-local offset, so it follows the lamp transform. */
    game->lamp = psxe_scene_create(&game->scene);
    lamp = psxe_scene_get(&game->scene, game->lamp);
    if (lamp == NULL) {
        return;
    }
    lamp->mesh = &cube_mesh;
    lamp->transform.scale = (VECTOR){ ONE / 6, ONE / 6, ONE / 6 };
    psxe_material_set_color(&lamp->material, (psxe_color){ 255, 200, 96 }, PSXE_COLOR_REPLACE);
    psxe_material_set_effects(&lamp->material, PSXE_EFFECT_UNLIT, PSXE_BLEND_HALF);

    psxe_light_init(&lamp_light, PSXE_LIGHT_POINT);
    psxe_light_set_color(&lamp_light, (psxe_color){ 255, 180, 80 }, ONE + ONE / 2);
    psxe_light_set_range(&lamp_light, 700, PSXE_FALLOFF_SMOOTH);
    psxe_scene_set_light(&game->scene, game->lamp, &lamp_light);
}

static void update_lamp(cube_game *game) {
    const psxe_entity *cube = psxe_scene_get_const(&game->scene, game->cube);
    psxe_entity *lamp = psxe_scene_get(&game->scene, game->lamp);

    if ((cube == NULL) || (lamp == NULL)) {
        return;
    }
    game->lamp_angle = (int16_t)((game->lamp_angle + 20) & (PSXE_ANGLE_FULL_TURN - 1));
    lamp->transform.position.vx = cube->transform.position.vx + ((isin(game->lamp_angle) * LAMP_ORBIT) >> 12);
    lamp->transform.position.vy = cube->transform.position.vy + LAMP_HEIGHT;
    lamp->transform.position.vz = cube->transform.position.vz + ((icos(game->lamp_angle) * LAMP_ORBIT) >> 12);
    lamp->transform.rotation.vy = game->lamp_angle;
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
    setup_lights(game, &engine->renderer);

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
    load_assets(game, engine, entity);

    snprintf(game->status, sizeof(game->status), "START: SAVE  SELECT: LOAD");
}

#ifdef PSXE_CUBE_DIAGNOSTICS
static void update_diagnostics(psxe_engine *engine, cube_game *game) {
    if ((engine->frame == 0) || ((engine->frame % engine->ticks_per_second) != 0)) {
        return;
    }

    const psxe_renderer *renderer = &engine->renderer;
    const psxe_vram_stats vram = psxe_vram_get_stats(&game->vram);
    const psxe_audio_stats spu = psxe_audio_get_stats(&engine->audio);
    const unsigned long cd = (unsigned long)psxe_cd_sectors_read();
    const unsigned int vram_capacity = (unsigned int)vram.used_tiles + vram.free_tiles;
    const unsigned long spu_capacity = (unsigned long)spu.used_bytes + spu.free_bytes;

    snprintf(game->diagnostics[0], sizeof(game->diagnostics[0]), "CPU %u GPU %u TICKS",
             (unsigned int)engine->cpu_ticks, (unsigned int)renderer->gpu_wait_ticks);
    snprintf(game->diagnostics[1], sizeof(game->diagnostics[1]), "PRIMS %lu DROP %lu REJ %lu RT %u",
             (unsigned long)renderer->submitted_primitives, (unsigned long)renderer->dropped_primitives,
             (unsigned long)renderer->rejected_faces, (unsigned int)renderer->target_passes);
    snprintf(game->diagnostics[2], sizeof(game->diagnostics[2]), "PACK %lu MAX %lu CD %lu",
             (unsigned long)renderer->packet_bytes, (unsigned long)renderer->packet_high_water, cd);
    snprintf(game->diagnostics[3], sizeof(game->diagnostics[3]), "VRAM %u/%u SPU %lu B",
             (unsigned int)vram.used_tiles, vram_capacity, (unsigned long)spu.used_bytes);

    printf("PSXE frame=%lu cpu_ticks=%u gpu_wait_ticks=%u prims=%lu drop=%lu rejected=%lu passes=%u\n",
           (unsigned long)(engine->frame - 1u), (unsigned int)engine->cpu_ticks,
           (unsigned int)renderer->gpu_wait_ticks, (unsigned long)renderer->submitted_primitives,
           (unsigned long)renderer->dropped_primitives, (unsigned long)renderer->rejected_faces,
           (unsigned int)renderer->target_passes);
    printf("PSXE packets=%lu peak=%lu cd_sectors=%lu vram_tiles=%u/%u spu_bytes=%lu/%lu\n",
           (unsigned long)renderer->packet_bytes, (unsigned long)renderer->packet_high_water, cd,
           (unsigned int)vram.used_tiles, vram_capacity, (unsigned long)spu.used_bytes, spu_capacity);
}
#endif

static void game_update(psxe_engine *engine, void *user_data) {
    cube_game *game = user_data;
    psxe_entity *entity = psxe_scene_get(&game->scene, game->cube);
    const psxe_pad_state *pad = psxe_input_pad(&engine->input, 0);

#ifdef PSXE_CUBE_DIAGNOSTICS
    update_diagnostics(engine, game);
#endif
    entity->transform.rotation.vx += 10;
    entity->transform.rotation.vy += 14;
    psxe_object_rotate(&game->mini_cube, 0, 24, 8);
    psxe_object_rotate(&game->screen, 0, 12, 0);
    update_lamp(game);

    if (psxe_button_pressed(pad, PAD_CROSS)) {
        unsigned int effect_count = game->painted_mesh ? PSXE_ARRAY_COUNT(effect_names) : 5u;
        game->effect = (game->effect + 1u) % effect_count;
        apply_effect(game, entity, game->effect);
        psxe_input_rumble(&engine->input, 0, true, 100, 8);
        psxe_audio_play(&engine->audio, &game->chime, 0x2400, 0);
        snprintf(game->status, sizeof(game->status), "EFFECT: %s", effect_names[game->effect]);
    }
    if (psxe_button_pressed(pad, PAD_CIRCLE)) {
        show_target(game, game->shown_target ^ 1u);
        snprintf(game->status, sizeof(game->status), "SCREEN: TARGET %c", game->shown_target ? 'B' : 'A');
    }
    if (psxe_button_pressed(pad, PAD_TRIANGLE)) {
        game->preview.material.effects ^= PSXE_EFFECT_FLIP_X;
    }
    if (psxe_button_pressed(pad, PAD_SQUARE)) {
        game->ambient = (game->ambient + 1u) % (unsigned int)PSXE_ARRAY_COUNT(ambient_presets);
        psxe_renderer_set_ambient(&engine->renderer, ambient_presets[game->ambient]);
        snprintf(game->status, sizeof(game->status), "AMBIENT: %s", ambient_names[game->ambient]);
    }
    if (psxe_button_pressed(pad, PAD_L1)) {
        psxe_light *lamp_light = psxe_scene_light(&game->scene, game->lamp);
        if (lamp_light != NULL) {
            lamp_light->enabled = !lamp_light->enabled;
            snprintf(game->status, sizeof(game->status), "LAMP: %s", lamp_light->enabled ? "ON" : "OFF");
        }
    }
    if (psxe_button_pressed(pad, PAD_R1)) {
        game->show_markers = !game->show_markers;
    }
    if (psxe_button_pressed(pad, PAD_R2)) {
        unsigned int free_blocks;
        psxe_memory_card_result result = psxe_memory_card_list(0, NULL, 0, NULL, &free_blocks);
        if (result == PSXE_MEMORY_CARD_OK) {
            snprintf(game->status, sizeof(game->status), "CARD: %u BLOCKS FREE", free_blocks);
        } else {
            snprintf(game->status, sizeof(game->status), "CARD: %s", psxe_memory_card_result_string(result));
        }
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
            if (index == 1u) {
                /* Per-pass light set; render_target_end restores the main one. */
                static const SVECTOR green_light = { ONE, -ONE, -ONE, 0 };
                psxe_renderer_set_directional_light(
                    renderer, &green_light, (psxe_color){ 120, 255, 140 }, (psxe_color){ 24, 48, 24 }
                );
            }
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
    if (game->show_markers) {
        psxe_light lamp_world;
        if (psxe_scene_light_world(&game->scene, game->lamp, &lamp_world)) {
            psxe_draw_light_marker(renderer, &lamp_world, 6);
        }
    }
    psxe_sprite_draw(renderer, &game->preview);
    psxe_ui_panel(&engine->renderer, 4, 4, 312, 34, (psxe_color){ 16, 16, 28 }, true);
    psxe_ui_text(&engine->renderer, 10, 10, "PSX-ENGINE / 3D FOUNDATION");
    psxe_ui_text(&engine->renderer, 10, 20, game->status);
#ifdef PSXE_CUBE_DIAGNOSTICS
    for (index = 0; index < PSXE_ARRAY_COUNT(game->diagnostics); ++index) {
        if (game->diagnostics[index][0]) {
            psxe_ui_text(renderer, 10, (int16_t)(174u + index * 10u), game->diagnostics[index]);
        }
    }
#else
    snprintf(
        diagnostics,
        sizeof(diagnostics),
        "FRAME %lu  PRIMS %lu  DROP %lu  RT %u",
        (unsigned long)engine->frame,
        (unsigned long)engine->renderer.submitted_primitives,
        (unsigned long)engine->renderer.dropped_primitives,
        (unsigned int)engine->renderer.target_passes
    );
    psxe_ui_text(&engine->renderer, 10, 208, diagnostics);
#endif
    snprintf(
        diagnostics,
        sizeof(diagnostics),
        "LIGHTS CON %lu APP %lu FOLD %lu DROP %lu",
        (unsigned long)engine->renderer.lights_considered,
        (unsigned long)engine->renderer.lights_applied,
        (unsigned long)engine->renderer.lights_folded,
        (unsigned long)engine->renderer.lights_dropped
    );
    psxe_ui_text(&engine->renderer, 10, 218, diagnostics);
}

static void game_shutdown(psxe_engine *engine, void *user_data) {
    cube_game *game = user_data;
    psxe_entity *entity = psxe_scene_get(&game->scene, game->cube);
    (void)engine;
    entity->mesh = &cube_mesh;
    entity->material = psxe_material_default();
    if (game->painted_mesh != NULL) {
        psxe_mesh_free(&game->heap, game->painted_mesh);
        psxe_vram_free(&game->vram, game->paint);
    }
    psxe_cd_stop();
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
            .dither = true,
#ifdef PSXE_CUBE_DIAGNOSTICS
            .profile_timing = true,
#endif
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
        .shutdown = game_shutdown,
        .user_data = &state
    };

    memset(&state, 0, sizeof(state));
    psxe_engine_init(&engine, &config);
    psxe_engine_run(&engine, &game);
    return 0;
}
