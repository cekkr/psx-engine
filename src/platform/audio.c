#include "psxe/audio.h"

#include <string.h>

#include <hwregs_c.h>
#include <psxspu.h>

#define PSXE_SPU_ALLOC_START 0x1010u
#define PSXE_SPU_RAM_END 0x80000u
#define PSXE_VAG_HEADER_SIZE 48u
#define PSXE_VAG_MAGIC 0x70474156u

typedef struct psxe_vag_header {
    uint32_t magic;
    uint32_t version;
    uint32_t interleave;
    uint32_t size;
    uint32_t sample_rate;
    uint16_t reserved[5];
    uint16_t channels;
    char name[16];
} psxe_vag_header;

static uint32_t psxe_bswap32(uint32_t value) {
    return __builtin_bswap32(value);
}

void psxe_audio_init(psxe_audio *audio) {
    SpuInit();
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    audio->next_spu_address = PSXE_SPU_ALLOC_START;
    audio->next_voice = 0;
}

bool psxe_audio_upload_vag(
    psxe_audio *audio,
    psxe_audio_sample *sample,
    const void *vag_data,
    size_t vag_size
) {
    const psxe_vag_header *header;
    const uint8_t *payload;
    uint32_t payload_size;
    uint32_t transfer_size;
    uint32_t bulk_size;
    uint32_t address;
    uint32_t tail[16] __attribute__((aligned(4)));

    if ((audio == NULL) || (sample == NULL) || (vag_data == NULL) || (vag_size < PSXE_VAG_HEADER_SIZE)) {
        return false;
    }

    header = (const psxe_vag_header *)vag_data;
    if (header->magic != PSXE_VAG_MAGIC) {
        return false;
    }

    payload_size = psxe_bswap32(header->size);
    if ((payload_size == 0) || ((size_t)payload_size > (vag_size - PSXE_VAG_HEADER_SIZE))) {
        return false;
    }

    transfer_size = (payload_size + 63u) & ~63u;
    address = (audio->next_spu_address + 63u) & ~63u;
    if ((address + transfer_size) > PSXE_SPU_RAM_END) {
        return false;
    }

    payload = (const uint8_t *)vag_data + PSXE_VAG_HEADER_SIZE;
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    bulk_size = payload_size & ~63u;
    if (bulk_size > 0) {
        SpuSetTransferStartAddr(address);
        SpuWrite((const uint32_t *)payload, bulk_size);
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    }
    if (bulk_size != payload_size) {
        memset(tail, 0, sizeof(tail));
        memcpy(tail, payload + bulk_size, payload_size - bulk_size);
        SpuSetTransferStartAddr(address + bulk_size);
        SpuWrite(tail, sizeof(tail));
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    }

    sample->spu_address = address;
    sample->byte_size = payload_size;
    sample->sample_rate = psxe_bswap32(header->sample_rate);
    sample->valid = true;
    audio->next_spu_address = address + transfer_size;
    return true;
}

int psxe_audio_play(
    psxe_audio *audio,
    const psxe_audio_sample *sample,
    uint16_t volume,
    int16_t pan
) {
    int voice;
    int left;
    int right;

    if ((audio == NULL) || (sample == NULL) || !sample->valid) {
        return -1;
    }

    if (volume > 0x3fff) {
        volume = 0x3fff;
    }
    if (pan < -0x1000) {
        pan = -0x1000;
    } else if (pan > 0x1000) {
        pan = 0x1000;
    }

    voice = audio->next_voice;
    audio->next_voice = (uint8_t)((audio->next_voice + 1u) % PSXE_MAX_AUDIO_VOICES);
    left = (pan > 0) ? (((int)volume * (0x1000 - pan)) >> 12) : (int)volume;
    right = (pan < 0) ? (((int)volume * (0x1000 + pan)) >> 12) : (int)volume;

    SpuSetKey(0, 1u << voice);
    SPU_CH_FREQ(voice) = getSPUSampleRate(sample->sample_rate);
    SPU_CH_ADDR(voice) = getSPUAddr(sample->spu_address);
    SPU_CH_VOL_L(voice) = (uint16_t)left;
    SPU_CH_VOL_R(voice) = (uint16_t)right;
    SPU_CH_ADSR1(voice) = 0x00ff;
    SPU_CH_ADSR2(voice) = 0x0000;
    SpuSetKey(1, 1u << voice);
    return voice;
}

void psxe_audio_stop(int voice) {
    if ((voice >= 0) && (voice < PSXE_MAX_AUDIO_VOICES)) {
        SpuSetKey(0, 1u << voice);
    }
}

void psxe_audio_stop_all(void) {
    SpuSetKey(0, 0x00ffffffu);
}
