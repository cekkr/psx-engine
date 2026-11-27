#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "core/asset_formats.h"
#include "core/fixed.h"
#include "core/save_format.h"
#include "psxe/animation.h"
#include "psxe/memory.h"
#include "psxe/scene.h"
#include "render/render_internal.h"

static void test_animation(void) {
    static const SVECTOR frames[] = {
        { INT16_MIN, -200, 80, 0 }, { 40, 40, 40, 0 },
        { INT16_MAX, -60, -80, 0 }, { 20, 20, 20, 0 }
    };
    static const SVECTOR normal_frames[] = { { ONE, 0, 0, 0 }, { 0, ONE, 0, 0 } };
    static const psxe_face face = { 0 };
    const psxe_mesh mesh = {
        .vertices = frames, .normals = normal_frames, .faces = &face,
        .vertex_count = 2, .normal_count = 1, .face_count = 1
    };
    const psxe_anim_clip clip = {
        .mesh = &mesh, .vertices = frames, .vertex_samples = PSXE_ARRAY_COUNT(frames),
        .normals = normal_frames, .normal_samples = PSXE_ARRAY_COUNT(normal_frames),
        .frame_count = 2, .frame_ms = 100
    };
    psxe_anim_player player = { 0 };
    psxe_mesh pose = { 0 };
    SVECTOR vertices[] = { { 777, 0, 0, 0 }, { 0 } };
    SVECTOR normal[1];
    psxe_anim_advance(&player, UINT32_MAX);
    assert(!psxe_anim_finished(&player));
    assert(!psxe_anim_sample(&player, &pose, vertices, 2, normal, 1));
    assert(psxe_anim_play(&player, &clip, true));
    psxe_anim_advance(&player, 50);
    assert(!psxe_anim_sample(&player, &pose, vertices, 1, normal, 1));
    assert(!psxe_anim_sample(&player, &pose, vertices, 2, normal, 0));
    assert(pose.vertices == NULL && vertices[0].vx == 777);
    assert(psxe_anim_sample(&player, &pose, vertices, 2, normal, 1));
    assert(pose.faces == &face && pose.vertices == vertices && pose.normals == normal);
    assert(vertices[0].vx == 0 && vertices[0].vy == -130 && vertices[0].vz == 0);
    assert(vertices[1].vx == 30 && normal[0].vx == ONE / 2 && normal[0].vy == ONE / 2);
    psxe_anim_seek(&player, 375);
    assert(psxe_anim_sample(&player, &pose, vertices, 2, normal, 1));
    assert(player.time_ms == 175 && vertices[0].vy == -165 && vertices[1].vx == 35);
    psxe_anim_advance(&player, UINT32_MAX);
    assert(player.time_ms == (175u + UINT32_MAX % 200u) % 200u);
    assert(!psxe_anim_finished(&player));

    psxe_anim_clip bad = clip;
    bad.vertex_samples = 3;
    assert(!psxe_anim_play(&player, &bad, false) && player.clip == &clip && player.loop);
    bad = clip;
    bad.normal_samples = 1;
    assert(!psxe_anim_play(&player, &bad, false));
    bad = clip;
    bad.frame_ms = 0;
    assert(!psxe_anim_play(&player, &bad, false));
    assert(psxe_anim_play(&player, &clip, false) && !psxe_anim_finished(&player));
    psxe_anim_advance(&player, UINT32_MAX);
    assert(psxe_anim_finished(&player) && player.time_ms == 100);
    assert(psxe_anim_sample(&player, &pose, vertices, 2, normal, 1) && vertices[0].vx == INT16_MAX);
    psxe_anim_seek(&player, 0);
    assert(!psxe_anim_finished(&player));

    psxe_anim_clip still = clip;
    still.frame_count = 1;
    still.normals = NULL;
    still.normal_samples = 0;
    assert(psxe_anim_play(&player, &still, false) && psxe_anim_finished(&player));
    assert(psxe_anim_play(&player, &still, true) && !psxe_anim_finished(&player));
    psxe_anim_advance(&player, 50);
    assert(psxe_anim_sample(&player, &pose, vertices, 2, NULL, 0));
    assert(vertices[0].vx == INT16_MIN && pose.normals == mesh.normals);
}

static void test_scene(void) {
    psxe_scene scene;
    psxe_entity_id ids[PSXE_MAX_ENTITIES];
    psxe_scene_init(&scene);
    for (uint16_t i = 0; i < PSXE_MAX_ENTITIES; ++i) {
        ids[i] = psxe_scene_create(&scene);
        assert(ids[i].index == i);
        assert(psxe_scene_get_const(&scene, ids[i])->transform.scale.vx == ONE);
    }
    assert(psxe_scene_create(&scene).index == UINT16_MAX);
    assert(scene.active_count == PSXE_MAX_ENTITIES);

    const psxe_light light = { .enabled = true, .type = PSXE_LIGHT_POINT };
    for (uint16_t i = 0; i < PSXE_MAX_SCENE_LIGHTS; ++i) {
        assert(psxe_scene_set_light(&scene, ids[i], &light));
    }
    assert(!psxe_scene_set_light(&scene, ids[PSXE_MAX_SCENE_LIGHTS], &light));
    assert(scene.light_count == PSXE_MAX_SCENE_LIGHTS && scene.lights_dropped == 1);
    assert(psxe_scene_set_light(&scene, ids[0], &light));
    assert(scene.light_count == PSXE_MAX_SCENE_LIGHTS);

    assert(psxe_scene_destroy(&scene, ids[0]));
    assert(scene.light_count == PSXE_MAX_SCENE_LIGHTS - 1);
    assert(!psxe_scene_destroy(&scene, ids[0]));
    psxe_entity_id reused = psxe_scene_create(&scene);
    assert(reused.index == ids[0].index && reused.generation != ids[0].generation);
    assert(psxe_scene_get(&scene, ids[0]) == NULL);
    assert(psxe_scene_light(&scene, reused) == NULL);
    assert(!psxe_scene_set_light(&scene, ids[0], &light));
    assert(psxe_scene_set_light(&scene, reused, &light));
}

static void test_heap(void) {
    uint64_t memory[64];
    psxe_heap heap;
    assert(!psxe_heap_init(&heap, memory, 1));
    assert(psxe_heap_init(&heap, (uint8_t *)memory + 1, sizeof(memory) - 1));
    psxe_heap_stats initial = psxe_heap_get_stats(&heap);
    assert(initial.free_bytes && initial.free_bytes == initial.largest_free);

    void *first = psxe_heap_alloc(&heap, 17);
    void *middle = psxe_heap_alloc(&heap, 24);
    void *last = psxe_heap_alloc(&heap, 40);
    assert(first != NULL && ((uintptr_t)first & 7u) == 0);
    assert(middle != NULL && last != NULL);
    assert(!psxe_heap_alloc(&heap, 0) && !psxe_heap_alloc(&heap, SIZE_MAX));
    assert(!psxe_heap_free(&heap, (uint8_t *)middle + 1));
    assert(psxe_heap_free(&heap, middle));
    assert(!psxe_heap_free(&heap, middle));
    psxe_heap_stats fragmented = psxe_heap_get_stats(&heap);
    assert(fragmented.free_bytes > fragmented.largest_free);
    assert(!psxe_heap_alloc(&heap, fragmented.free_bytes));
    assert(psxe_heap_free(&heap, last) && psxe_heap_free(&heap, first));
    psxe_heap_stats restored = psxe_heap_get_stats(&heap);
    assert(!restored.used_bytes && restored.largest_free == initial.largest_free);

    assert(!psxe_mesh_alloc(&heap, 0, 0, 1));
    assert(!psxe_mesh_alloc(&heap, UINT16_MAX, UINT16_MAX, UINT16_MAX));
    psxe_mesh_storage *mesh = psxe_mesh_alloc(&heap, 3, 0, 1);
    assert(mesh != NULL && mesh->mesh.vertices == mesh->vertices);
    assert(mesh->mesh.faces == mesh->faces && mesh->mesh.normals == NULL);
    assert(psxe_mesh_free(&heap, mesh));
    assert(psxe_heap_get_stats(&heap).free_bytes == initial.free_bytes);
}

static void test_packets(void) {
    static psxe_renderer renderer;
    uint32_t arena[4] = { 0, 0, 0, 0xdeadbeefu };
    uint8_t *bytes = (uint8_t *)arena;
    renderer.next_packet = bytes + 1;
    renderer.packet_end = bytes + 12;
    assert(psxe_renderer_allocate(&renderer, 4) == bytes + 4);
    assert(psxe_renderer_allocate(&renderer, SIZE_MAX) == NULL);
    assert(renderer.next_packet == bytes + 8 && renderer.dropped_primitives == 1);
    assert(psxe_renderer_allocate(&renderer, 4) == bytes + 8);
    assert(psxe_renderer_allocate(&renderer, 1) == NULL);
    renderer.next_packet = bytes + 9;
    renderer.packet_end = bytes + 11;
    assert(psxe_renderer_allocate(&renderer, 1) == NULL);
    assert(renderer.next_packet == bytes + 9 && renderer.dropped_primitives == 3);
    assert(arena[3] == 0xdeadbeefu);

    renderer.ordering_table = arena;
    renderer.ot_length = 3;
    assert(psxe_renderer_bucket(&renderer, 0) == &arena[0]);
    assert(psxe_renderer_bucket(&renderer, UINT_MAX) == &arena[2]);
}

static void test_fixed(void) {
    SVECTOR direction = { 1, 2, 3, 4 };
    assert(psxe_vector_normalize(0, 0, 0, &direction) == 0);
    assert(direction.vx == 1 && direction.vy == 2 && direction.vz == 3 && direction.pad == 4);
    assert(psxe_vector_normalize(3, -4, 0, &direction) == 5);
    assert(direction.vx == 2457 && direction.vy == -3276 && direction.vz == 0 && direction.pad == 0);
    assert(psxe_vector_normalize(INT32_MIN, 0, 0, &direction) == INT32_MAX);
    assert(direction.vx == -ONE);
    assert(psxe_vector_normalize(0, 0, 1, NULL) == 1);

    MATRIX rotation = { .m = { { 0, -ONE, 0 }, { ONE, 0, 0 }, { 0, 0, ONE / 2 } } };
    VECTOR result;
    psxe_matrix_apply(&rotation, 7, -3, -5, &result);
    assert(result.vx == 3 && result.vy == 7 && result.vz == -3);
}

static void test_saves(void) {
    uint8_t block[PSXE_MEMORY_CARD_BLOCK_SIZE] = { 'S', 'C' };
    const char payload[] = "123456789";
    const uint8_t header[] = {
        'P', 'X', 'S', 'E', 1, 0, 0x34, 0x12, 9, 0, 0, 0, 0x26, 0x39, 0xf4, 0xcb
    };
    char output[sizeof(payload)] = { 0 };
    size_t written = 0;

    assert(!psxe_save_encode(block, sizeof(block) - 1, 0x1234, payload, 9));
    assert(!psxe_save_encode(block, sizeof(block), 0x1234, payload, PSXE_MAX_SAVE_PAYLOAD + 1));
    assert(psxe_save_encode(block, sizeof(block), 0x1234, payload, 9));
    assert(block[0] == 'S' && block[1] == 'C');
    assert(memcmp(block + 512, header, sizeof(header)) == 0);
    assert(psxe_save_decode(block, sizeof(block), 0x1234, output, 9, &written) == PSXE_MEMORY_CARD_OK);
    assert(written == 9 && memcmp(output, payload, 9) == 0);

    memset(output, 0, sizeof(output));
    written = 0;
    assert(psxe_save_decode(block, sizeof(block) - 1, 0x1234, output, 9, &written) == PSXE_MEMORY_CARD_CORRUPT);
    assert(psxe_save_decode(block, sizeof(block), 1, output, 9, &written) == PSXE_MEMORY_CARD_VERSION_MISMATCH);
    assert(psxe_save_decode(block, sizeof(block), 0x1234, output, 8, &written) == PSXE_MEMORY_CARD_INVALID_ARGUMENT);
    assert(psxe_save_decode(block, sizeof(block), 0x1234, output, 9, NULL) == PSXE_MEMORY_CARD_OK);
    memset(output, 0, sizeof(output));
    const unsigned int offsets[] = { 512, 516, 522, 524, 528 };
    for (size_t i = 0; i < PSXE_ARRAY_COUNT(offsets); ++i) {
        block[offsets[i]] ^= 1;
        assert(psxe_save_decode(block, sizeof(block), 0x1234, output, 9, &written) == PSXE_MEMORY_CARD_CORRUPT);
        assert(written == 0 && memcmp(output, (char[sizeof(output)]){ 0 }, sizeof(output)) == 0);
        block[offsets[i]] ^= 1;
    }
}

static void test_tim(void) {
    union {
        uint32_t align;
        uint8_t bytes[74];
    } tim = { .bytes = {
        [0] = 0x10, [4] = 8,
        [8] = 44, [16] = 16, [18] = 1,
        [52] = 14, [60] = 1, [62] = 1
    } };
    psxe_tim_view view;
    assert(psxe_tim_parse(tim.bytes, 66, &view));
    assert(view.mode == 0 && view.texels_per_word == 4 && view.palettes.height == 1);
    assert((const uint8_t *)view.palettes.data == tim.bytes + 20);
    assert((const uint8_t *)view.pixels.data == tim.bytes + 64 && view.pixels.width == 1);
    for (size_t size = 0; size < 66; ++size) {
        assert(!psxe_tim_parse(tim.bytes, size, &view));
    }
    assert(!psxe_tim_parse(tim.bytes + 1, 66, &view));
    tim.bytes[4] = 9;
    assert(!psxe_tim_parse(tim.bytes, 66, &view));
    tim.bytes[4] = 8;
    tim.bytes[8] = 45;
    assert(!psxe_tim_parse(tim.bytes, sizeof(tim.bytes), &view));
    tim.bytes[8] = 44;
    tim.bytes[60] = 2;
    assert(!psxe_tim_parse(tim.bytes, 66, &view));
    assert(view.pixels.width == 1 && view.texels_per_word == 4);

    tim.bytes[4] = 2;
    tim.bytes[8] = 14;
    tim.bytes[16] = 1;
    assert(psxe_tim_parse(tim.bytes, 22, &view));
    assert(view.mode == 2 && view.texels_per_word == 1 && view.palettes.data == NULL);

    union {
        uint32_t align;
        uint8_t bytes[546];
    } indexed = { .bytes = {
        [0] = 0x10, [4] = 9,
        [8] = 12, [9] = 2, [17] = 1, [18] = 1,
        [532] = 14, [540] = 1, [542] = 1
    } };
    assert(psxe_tim_parse(indexed.bytes, sizeof(indexed.bytes), &view));
    assert(view.mode == 1 && view.texels_per_word == 2 && view.palettes.width == 256);
}

static void test_vag(void) {
    uint8_t vag[64] = { 'V', 'A', 'G', 'p', [15] = 16, [18] = 0xac, [19] = 0x44 };
    psxe_vag_view view;
    assert(psxe_vag_parse(vag, sizeof(vag), &view));
    assert(view.payload == vag + 48 && view.size == 16 && view.sample_rate == 44100);
    for (size_t size = 0; size < sizeof(vag); ++size) {
        assert(!psxe_vag_parse(vag, size, &view));
    }
    vag[12] = 0xff;
    assert(!psxe_vag_parse(vag, sizeof(vag), &view));
    vag[12] = 0;
    vag[15] = 15;
    assert(!psxe_vag_parse(vag, sizeof(vag), &view));
    vag[15] = 16;
    vag[31] = 2;
    assert(!psxe_vag_parse(vag, sizeof(vag), &view));
    vag[31] = 0;
    vag[18] = vag[19] = 0;
    assert(!psxe_vag_parse(vag, sizeof(vag), &view));
    assert(view.size == 16 && view.sample_rate == 44100);
}

int main(void) {
    test_animation();
    test_scene();
    test_heap();
    test_packets();
    test_fixed();
    test_saves();
    test_tim();
    test_vag();
    puts("PSXE host tests passed (animation, scene, heap, packets, fixed-point, saves, TIM, VAG).");
    return 0;
}
