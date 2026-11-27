#include "psxe/audio.h"

#include <string.h>

#include <hwregs_c.h>
#include <psxetc.h>
#include <psxspu.h>

#include "../core/asset_formats.h"

#define PSXE_SPU_ALLOC_START 0x1010u

static bool psxe_audio_transfer(uint32_t address, const uint32_t *words, size_t size) {
    uint32_t timeout = 0x100000u;
    if (!SpuSetTransferStartAddr(address) || (SpuWrite(words, size) != size)) {
        return false;
    }
    while (DMA_CHCR(DMA_SPU) & (1u << 24)) {
        if (--timeout == 0) {
            DMA_CHCR(DMA_SPU) = 0;
            return false;
        }
    }
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    return SpuIsTransferCompleted(SPU_TRANSFER_PEEK) != 0;
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
    psxe_vag_view vag;
    uint32_t address;
    uint32_t tail[16] __attribute__((aligned(4)));

    if ((audio == NULL) || (sample == NULL) || !psxe_vag_parse(vag_data, vag_size, &vag)) {
        return false;
    }

    uint32_t transfer_size = (vag.size + 63u) & ~63u;
    address = (audio->next_spu_address + 63u) & ~63u;
    if ((address < PSXE_SPU_ALLOC_START) || (address > PSXE_SPU_RAM_END) ||
        (transfer_size > PSXE_SPU_RAM_END - address)) {
        return false;
    }

    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    uint32_t bulk_size = ((uintptr_t)vag.payload & 3u) ? 0u : vag.size & ~63u;
    if (bulk_size && !psxe_audio_transfer(address, (const uint32_t *)vag.payload, bulk_size)) {
        return false;
    }
    for (uint32_t offset = bulk_size; offset < vag.size; offset += sizeof(tail)) {
        uint32_t chunk = vag.size - offset;
        if (chunk < sizeof(tail)) {
            memset(tail, 0, sizeof(tail));
        } else {
            chunk = sizeof(tail);
        }
        memcpy(tail, vag.payload + offset, chunk);
        if (!psxe_audio_transfer(address + offset, tail, sizeof(tail))) {
            return false;
        }
    }

    sample->spu_address = address;
    sample->byte_size = vag.size;
    sample->sample_rate = vag.sample_rate;
    sample->valid = true;
    audio->next_spu_address = address + transfer_size;
    return true;
}

psxe_cd_result psxe_audio_load_vag(
    psxe_audio *audio, psxe_audio_sample *sample, const psxe_cd_file *file,
    void *scratch, size_t capacity
) {
    psxe_cd_result result;
    if ((audio == NULL) || (sample == NULL) || (file == NULL) || (scratch == NULL) ||
        (file->size < PSXE_VAG_HEADER_SIZE) || (file->size > capacity)) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    result = psxe_cd_read_file(file, 0, scratch, file->size);
    if (result != PSXE_CD_OK) {
        return result;
    }
    return psxe_audio_upload_vag(audio, sample, scratch, file->size) ? PSXE_CD_OK : PSXE_CD_IO_ERROR;
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
