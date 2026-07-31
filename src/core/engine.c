#include "psxe/engine.h"

#include <string.h>

void psxe_engine_init(psxe_engine *engine, const psxe_engine_config *config) {
    memset(engine, 0, sizeof(*engine));
    engine->memory_card = config->memory_card;
    engine->ticks_per_second = (config->render.video_mode == PSXE_VIDEO_PAL) ? 50 : 60;
    engine->running = true;

    psxe_renderer_init(&engine->renderer, &config->render);
    psxe_input_init(&engine->input);
    psxe_audio_init(&engine->audio);
    psxe_memory_card_init();
}

void psxe_engine_run(psxe_engine *engine, const psxe_game *game) {
    if ((engine == NULL) || (game == NULL)) {
        return;
    }

    if (game->init != NULL) {
        game->init(engine, game->user_data);
    }

    while (engine->running) {
        psxe_input_poll(&engine->input);
        if (game->update != NULL) {
            game->update(engine, game->user_data);
        }

        psxe_renderer_begin_frame(&engine->renderer);
        if (game->render != NULL) {
            game->render(engine, game->user_data);
        }
        psxe_renderer_end_frame(&engine->renderer);
        ++engine->frame;
    }

    psxe_audio_stop_all();
    if (game->shutdown != NULL) {
        game->shutdown(engine, game->user_data);
    }
}

void psxe_engine_stop(psxe_engine *engine) {
    if (engine != NULL) {
        engine->running = false;
    }
}
