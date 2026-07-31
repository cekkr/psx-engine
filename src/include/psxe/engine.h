#ifndef PSXE_ENGINE_H
#define PSXE_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "psxe/audio.h"
#include "psxe/input.h"
#include "psxe/memory_card.h"
#include "psxe/render.h"

typedef struct psxe_engine psxe_engine;

typedef struct psxe_game {
    void (*init)(psxe_engine *engine, void *user_data);
    void (*update)(psxe_engine *engine, void *user_data);
    void (*render)(psxe_engine *engine, void *user_data);
    void (*shutdown)(psxe_engine *engine, void *user_data);
    void *user_data;
} psxe_game;

typedef struct psxe_engine_config {
    psxe_render_config render;
    psxe_memory_card_config memory_card;
} psxe_engine_config;

struct psxe_engine {
    psxe_renderer renderer;
    psxe_input input;
    psxe_audio audio;
    psxe_memory_card_config memory_card;
    uint32_t frame;
    uint16_t ticks_per_second;
    bool running;
};

void psxe_engine_init(psxe_engine *engine, const psxe_engine_config *config);
void psxe_engine_run(psxe_engine *engine, const psxe_game *game);
void psxe_engine_stop(psxe_engine *engine);

#endif
