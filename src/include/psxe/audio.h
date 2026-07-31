#ifndef PSXE_AUDIO_H
#define PSXE_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "psxe/config.h"

typedef struct psxe_audio_sample {
    uint32_t spu_address;
    uint32_t byte_size;
    uint32_t sample_rate;
    bool valid;
} psxe_audio_sample;

typedef struct psxe_audio {
    uint32_t next_spu_address;
    uint8_t next_voice;
} psxe_audio;

void psxe_audio_init(psxe_audio *audio);
bool psxe_audio_upload_vag(
    psxe_audio *audio,
    psxe_audio_sample *sample,
    const void *vag_data,
    size_t vag_size
);
int psxe_audio_play(
    psxe_audio *audio,
    const psxe_audio_sample *sample,
    uint16_t volume,
    int16_t pan
);
void psxe_audio_stop(int voice);
void psxe_audio_stop_all(void);

#endif
