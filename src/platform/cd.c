#include "psxe/cd.h"

#include <string.h>

#include <hwregs_c.h>
#include <psxcd.h>
#include <psxetc.h>
#include <psxgpu.h>

#define PSXE_CD_LBA_LIMIT (100u * 60u * 75u - 150u)
#define PSXE_CD_READ_TIMEOUT 180u /* VBlanks without a sector. */
#define PSXE_CD_COMMAND_TIMEOUT 180u

typedef enum psxe_cd_operation {
    PSXE_CD_IDLE,
    PSXE_CD_TRANSFER,
    PSXE_CD_XA,
    PSXE_CD_CDDA,
    PSXE_CD_RECOVERY
} psxe_cd_operation;

static bool psxe_cd_ready;
static psxe_cd_operation psxe_cd_operation_active;
static psxe_cd_result psxe_cd_read_result = PSXE_CD_INVALID_ARGUMENT;
static volatile int32_t psxe_cd_pending_sectors;
static volatile uint32_t psxe_cd_last_sector_tick;
static uint32_t *volatile psxe_cd_dma_address;
static volatile int psxe_cd_sector_words;
static uint32_t psxe_cd_scratch[PSXE_CD_SECTOR_SIZE / sizeof(uint32_t)];
/* The SDK retains this pointer if command acknowledgement times out. */
static uint8_t psxe_cd_reply[32];

static void psxe_cd_sector_interrupt(CdlIntrResult interrupt, uint8_t *result) {
    if (psxe_cd_pending_sectors <= 0) {
        return;
    }
    if (interrupt != CdlDataReady) {
        psxe_cd_pending_sectors = -1;
        return;
    }
    if (!CdGetSector2(psxe_cd_dma_address, psxe_cd_sector_words) || (CdDataSync(0) != 0)) {
        DMA_CHCR(DMA_CD) = 0;
        psxe_cd_pending_sectors = -1;
        return;
    }
    psxe_cd_dma_address += psxe_cd_sector_words;
    psxe_cd_last_sector_tick = (uint32_t)VSync(-1);
    --psxe_cd_pending_sectors;
}

static bool psxe_cd_command(CdlCommand command, const void *args, int length, bool reply) {
    CdlIntrResult result;
    if (CdCommand(command, args, length, reply ? psxe_cd_reply : NULL) != 1) {
        return false;
    }
    uint32_t start = (uint32_t)VSync(-1);
    while ((result = CdSync(1, NULL)) == CdlNoIntr) {
        if ((uint32_t)VSync(-1) - start > PSXE_CD_COMMAND_TIMEOUT) {
            return false;
        }
        VSync(0);
    }
    return (result == CdlComplete) || (result == CdlDataReady && !reply);
}

static bool psxe_cd_set_location(uint32_t lba) {
    CdlLOC location;
    CdIntToPos((int)lba, &location);
    return psxe_cd_command(CdlSetloc, &location, 3, false);
}

static psxe_cd_result psxe_cd_require_idle(void) {
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    return (psxe_cd_operation_active == PSXE_CD_IDLE) ? PSXE_CD_OK : PSXE_CD_BUSY;
}

psxe_cd_result psxe_cd_init(void) {
    if (psxe_cd_ready) {
        return PSXE_CD_OK;
    }
    if (CdInit() != 1) {
        return PSXE_CD_NOT_READY;
    }
    CdReadyCallback(psxe_cd_sector_interrupt);
    CdAutoPauseCallback(psxe_cd_sector_interrupt);
    psxe_cd_ready = true;
    return PSXE_CD_OK;
}

psxe_cd_result psxe_cd_status(psxe_cd_drive_status *status) {
    if (status == NULL) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    if (psxe_cd_operation_active == PSXE_CD_TRANSFER) {
        return PSXE_CD_BUSY;
    }
    if (!psxe_cd_command(CdlNop, NULL, 0, true)) {
        return PSXE_CD_IO_ERROR;
    }
    *status = (psxe_cd_drive_status){
        .lid_open = (psxe_cd_reply[0] & CdlStatShellOpen) != 0,
        .motor_on = (psxe_cd_reply[0] & CdlStatStandby) != 0,
        .seeking = (psxe_cd_reply[0] & CdlStatSeek) != 0,
        .reading = (psxe_cd_reply[0] & CdlStatRead) != 0,
        .playing = (psxe_cd_reply[0] & CdlStatPlay) != 0
    };
    if (!status->seeking && !status->reading && !status->playing &&
        ((psxe_cd_operation_active == PSXE_CD_CDDA) || (psxe_cd_operation_active == PSXE_CD_XA))) {
        psxe_cd_operation_active = PSXE_CD_IDLE;
    }
    return PSXE_CD_OK;
}

psxe_cd_result psxe_cd_seek(uint32_t lba, bool audio_track) {
    psxe_cd_result status = psxe_cd_require_idle();
    if (status != PSXE_CD_OK) {
        return status;
    }
    if (lba >= PSXE_CD_LBA_LIMIT) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    return (psxe_cd_set_location(lba) &&
            psxe_cd_command(audio_track ? CdlSeekP : CdlSeekL, NULL, 0, false)) ?
           PSXE_CD_OK : PSXE_CD_IO_ERROR;
}

static bool psxe_cd_valid_bcd(uint8_t value) {
    return ((value >> 4) < 10) && ((value & 15u) < 10);
}

psxe_cd_result psxe_cd_position(uint32_t *lba) {
    CdlLOC location;
    int position;
    if (lba == NULL) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    if (psxe_cd_operation_active == PSXE_CD_TRANSFER) {
        return PSXE_CD_BUSY;
    }
    if (!psxe_cd_command(CdlGetlocP, NULL, 0, true)) {
        return PSXE_CD_IO_ERROR;
    }
    location = (CdlLOC){ psxe_cd_reply[5], psxe_cd_reply[6], psxe_cd_reply[7], psxe_cd_reply[0] };
    if (!psxe_cd_valid_bcd(location.minute) || !psxe_cd_valid_bcd(location.second) ||
        !psxe_cd_valid_bcd(location.sector) || (location.second >= 0x60u) || (location.sector >= 0x75u)) {
        return PSXE_CD_IO_ERROR;
    }
    position = CdPosToInt(&location);
    if (position < 0) {
        return PSXE_CD_IO_ERROR;
    }
    *lba = (uint32_t)position;
    return PSXE_CD_OK;
}

psxe_cd_result psxe_cd_motor_on(void) {
    psxe_cd_drive_status status;
    psxe_cd_result result = psxe_cd_require_idle();
    if (result == PSXE_CD_OK) {
        result = psxe_cd_status(&status);
    }
    if (result != PSXE_CD_OK) {
        return result;
    }
    return (status.motor_on || psxe_cd_command(CdlStandby, NULL, 0, false)) ?
           PSXE_CD_OK : PSXE_CD_IO_ERROR;
}

static psxe_cd_result psxe_cd_halt(CdlCommand command) {
    bool stopped = psxe_cd_command(command, NULL, 0, false);
    psxe_cd_operation_active = stopped ? PSXE_CD_IDLE : PSXE_CD_RECOVERY;
    return stopped ? PSXE_CD_OK : PSXE_CD_IO_ERROR;
}

static psxe_cd_result psxe_cd_halt_transport(CdlCommand command) {
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    if (psxe_cd_operation_active == PSXE_CD_TRANSFER) {
        return PSXE_CD_BUSY;
    }
    return psxe_cd_halt(command);
}

psxe_cd_result psxe_cd_pause(void) {
    if ((psxe_cd_operation_active == PSXE_CD_XA) || (psxe_cd_operation_active == PSXE_CD_CDDA)) {
        uint32_t start = (uint32_t)VSync(-1);
        psxe_cd_drive_status status;
        psxe_cd_result result = psxe_cd_status(&status);
        while ((result == PSXE_CD_OK) && status.seeking) {
            if ((uint32_t)VSync(-1) - start > PSXE_CD_COMMAND_TIMEOUT) {
                return PSXE_CD_IO_ERROR;
            }
            VSync(0);
            result = psxe_cd_status(&status);
        }
        if (result != PSXE_CD_OK) {
            return result;
        }
    }
    return psxe_cd_halt_transport(CdlPause);
}

psxe_cd_result psxe_cd_stop(void) {
    return psxe_cd_halt_transport(CdlStop);
}

static psxe_cd_result psxe_cd_finish_read(psxe_cd_result result) {
    /* Disarm first. An IRQ already running on this CPU has finished before
     * this store, and later IRQs won't touch the caller's destination. */
    psxe_cd_pending_sectors = 0;
    psxe_cd_dma_address = NULL;
    /* Pause is rejected by some drives while a ReadN seek is still running. */
    if ((psxe_cd_halt(CdlPause) != PSXE_CD_OK) && (psxe_cd_halt(CdlStop) != PSXE_CD_OK)) {
        result = PSXE_CD_IO_ERROR;
    }
    psxe_cd_read_result = result;
    return result;
}

psxe_cd_result psxe_cd_read_start(
    uint32_t lba, uint32_t sectors, psxe_cd_sector_mode mode,
    void *destination, size_t capacity
) {
    size_t sector_size = (mode == PSXE_CD_RAW) ? PSXE_CD_RAW_SECTOR_SIZE : PSXE_CD_SECTOR_SIZE;
    uint8_t settings = (mode == PSXE_CD_RAW) ? CdlModeSpeed | CdlModeSize : CdlModeSpeed;
    psxe_cd_result result = psxe_cd_require_idle();
    if (result != PSXE_CD_OK) {
        return result;
    }
    if (((mode != PSXE_CD_DATA) && (mode != PSXE_CD_RAW)) || (lba >= PSXE_CD_LBA_LIMIT) ||
        !sectors || (sectors > PSXE_CD_LBA_LIMIT - lba) ||
        (destination == NULL) || ((uintptr_t)destination & 3u) || (sectors > capacity / sector_size)) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    if (!psxe_cd_command(CdlSetmode, &settings, 1, false) || !psxe_cd_set_location(lba)) {
        return PSXE_CD_IO_ERROR;
    }
    psxe_cd_operation_active = PSXE_CD_TRANSFER;
    psxe_cd_dma_address = destination;
    psxe_cd_sector_words = (int)(sector_size / sizeof(uint32_t));
    psxe_cd_last_sector_tick = (uint32_t)VSync(-1);
    psxe_cd_pending_sectors = (int32_t)sectors;
    if (!psxe_cd_command(CdlReadN, NULL, 0, false)) {
        return psxe_cd_finish_read(PSXE_CD_IO_ERROR);
    }
    return PSXE_CD_PENDING;
}

psxe_cd_result psxe_cd_read_poll(void) {
    int32_t pending;
    if (psxe_cd_operation_active != PSXE_CD_TRANSFER) {
        return psxe_cd_read_result;
    }
    pending = psxe_cd_pending_sectors;
    if (pending == 0) {
        return psxe_cd_finish_read(PSXE_CD_OK);
    }
    if ((pending < 0) || (((uint32_t)VSync(-1) - psxe_cd_last_sector_tick) > PSXE_CD_READ_TIMEOUT)) {
        return psxe_cd_finish_read(PSXE_CD_IO_ERROR);
    }
    return PSXE_CD_PENDING;
}

psxe_cd_result psxe_cd_read_wait(void) {
    psxe_cd_result result;
    do {
        result = psxe_cd_read_poll();
    } while (result == PSXE_CD_PENDING);
    return result;
}

psxe_cd_result psxe_cd_read_cancel(void) {
    if (psxe_cd_operation_active != PSXE_CD_TRANSFER) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    psxe_cd_result result = psxe_cd_finish_read(PSXE_CD_OK);
    psxe_cd_read_result = PSXE_CD_INVALID_ARGUMENT;
    return result;
}

static psxe_cd_result psxe_cd_read_data(uint32_t lba, uint32_t sectors, void *destination, size_t size) {
    psxe_cd_result result = psxe_cd_read_start(lba, sectors, PSXE_CD_DATA, destination, size);
    return (result == PSXE_CD_PENDING) ? psxe_cd_read_wait() : result;
}

static uint32_t psxe_cd_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool psxe_cd_extent_valid(const psxe_cd_file *file, uint32_t limit) {
    uint32_t sectors = file->size / PSXE_CD_SECTOR_SIZE + (file->size % PSXE_CD_SECTOR_SIZE != 0);
    return (file->lba < limit) && (sectors <= limit - file->lba);
}

static bool psxe_cd_iso_extent(const uint8_t *record, uint32_t limit, psxe_cd_file *file) {
    uint32_t lba = psxe_cd_le32(record + 2);
    if ((lba >= limit) || (record[1] >= limit - lba) ||
        (record[25] & 0x80u) || record[26] || record[27]) {
        return false;
    }
    *file = (psxe_cd_file){ lba + record[1], psxe_cd_le32(record + 10) };
    return psxe_cd_extent_valid(file, limit);
}

static bool psxe_cd_iso_matches(const uint8_t *name, unsigned int length, const char *part, size_t size) {
    if (length == size + 2u && name[size] == ';' && name[size + 1u] == '1') {
        length -= 2u;
    }
    if (length != size) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        char c = part[index];
        if ((c >= 'a') && (c <= 'z')) {
            c = (char)(c - 'a' + 'A');
        }
        if (name[index] != (uint8_t)c) {
            return false;
        }
    }
    return true;
}

static psxe_cd_result psxe_cd_iso_search(
    const psxe_cd_file *directory, uint32_t limit, const char *part, size_t size,
    bool expect_directory, psxe_cd_file *found
) {
    const uint8_t *sector = (const uint8_t *)psxe_cd_scratch;
    for (uint32_t offset = 0; offset < directory->size; offset += PSXE_CD_SECTOR_SIZE) {
        uint32_t length = directory->size - offset;
        psxe_cd_result result = psxe_cd_read_data(directory->lba + offset / PSXE_CD_SECTOR_SIZE,
                                                  1, psxe_cd_scratch, sizeof(psxe_cd_scratch));
        if (result != PSXE_CD_OK) {
            return result;
        }
        if (length > PSXE_CD_SECTOR_SIZE) {
            length = PSXE_CD_SECTOR_SIZE;
        }
        for (uint32_t index = 0; (index < length) && sector[index]; index += sector[index]) {
            const uint8_t *record = sector + index;
            if ((record[0] < 34u) || (record[0] > length - index) || (record[32] > record[0] - 33u)) {
                return PSXE_CD_BAD_FILESYSTEM;
            }
            if (((record[25] & 2u) != 0) == expect_directory &&
                psxe_cd_iso_matches(record + 33, record[32], part, size)) {
                return psxe_cd_iso_extent(record, limit, found) ? PSXE_CD_OK : PSXE_CD_BAD_FILESYSTEM;
            }
        }
    }
    return PSXE_CD_NOT_FOUND;
}

static bool psxe_cd_path_separator(char c) {
    return (c == '/') || (c == '\\');
}

psxe_cd_result psxe_cd_find(const char *path, psxe_cd_file *file) {
    const uint8_t *volume = (const uint8_t *)psxe_cd_scratch;
    psxe_cd_file entry;
    uint32_t limit;
    psxe_cd_result result;
    if ((path == NULL) || (file == NULL)) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    if (psxe_cd_path_separator(*path)) {
        ++path;
    }
    if (!*path) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    result = psxe_cd_read_data(16, 1, psxe_cd_scratch, sizeof(psxe_cd_scratch));
    if (result != PSXE_CD_OK) {
        return result;
    }
    limit = psxe_cd_le32(volume + 80);
    if ((volume[0] != 1u) || memcmp(volume + 1, "CD001", 5) || (volume[6] != 1u) ||
        (volume[128] != 0u) || (volume[129] != 8u) ||
        (limit > PSXE_CD_LBA_LIMIT) || (volume[156] < 34u) || !(volume[181] & 2u) ||
        !psxe_cd_iso_extent(volume + 156, limit, &entry)) {
        return PSXE_CD_BAD_FILESYSTEM;
    }
    for (;;) {
        const char *end = path;
        psxe_cd_file directory = entry;
        while (*end && !psxe_cd_path_separator(*end)) {
            ++end;
        }
        if ((end == path) || ((size_t)(end - path) > 31u) || (*end && !end[1])) {
            return PSXE_CD_INVALID_ARGUMENT;
        }
        result = psxe_cd_iso_search(&directory, limit, path, (size_t)(end - path), *end != 0, &entry);
        if (result != PSXE_CD_OK) {
            return result;
        }
        if (!*end) {
            *file = entry;
            return PSXE_CD_OK;
        }
        path = end + 1;
    }
}

psxe_cd_result psxe_cd_read_file(const psxe_cd_file *file, uint32_t offset, void *destination, size_t size) {
    uint8_t *output = destination;
    psxe_cd_result result = psxe_cd_require_idle();
    if (result != PSXE_CD_OK) {
        return result;
    }
    if ((file == NULL) || !psxe_cd_extent_valid(file, PSXE_CD_LBA_LIMIT) ||
        (offset > file->size) || (size > file->size - offset) || ((destination == NULL) && size)) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    while (size) {
        uint32_t lba = file->lba + offset / PSXE_CD_SECTOR_SIZE;
        size_t start = offset % PSXE_CD_SECTOR_SIZE;
        size_t chunk;
        if (!start && !((uintptr_t)output & 3u) && (size >= PSXE_CD_SECTOR_SIZE)) {
            uint32_t sectors = (uint32_t)(size / PSXE_CD_SECTOR_SIZE);
            chunk = sectors * PSXE_CD_SECTOR_SIZE;
            result = psxe_cd_read_data(lba, sectors, output, chunk);
        } else {
            chunk = PSXE_CD_SECTOR_SIZE - start;
            if (chunk > size) {
                chunk = size;
            }
            result = psxe_cd_read_data(lba, 1, psxe_cd_scratch, sizeof(psxe_cd_scratch));
            if (result == PSXE_CD_OK) {
                memcpy(output, (const uint8_t *)psxe_cd_scratch + start, chunk);
            }
        }
        if (result != PSXE_CD_OK) {
            return result;
        }
        output += chunk;
        offset += (uint32_t)chunk;
        size -= chunk;
    }
    return PSXE_CD_OK;
}

static bool psxe_cd_filter(uint8_t file, uint8_t channel) {
    const uint8_t filter[] = { file, channel };
    return psxe_cd_command(CdlSetfilter, filter, 2, false);
}

psxe_cd_result psxe_cd_xa_filter(uint8_t file, uint8_t channel) {
    if (channel > 31u) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    if (psxe_cd_operation_active != PSXE_CD_XA) {
        return PSXE_CD_BUSY;
    }
    return psxe_cd_filter(file, channel) ? PSXE_CD_OK : PSXE_CD_IO_ERROR;
}

static psxe_cd_result psxe_cd_play(uint32_t lba, uint8_t mode, CdlCommand command, psxe_cd_operation operation) {
    if (!psxe_cd_command(CdlSetmode, &mode, 1, false) || !psxe_cd_set_location(lba)) {
        return PSXE_CD_IO_ERROR;
    }
    if (!psxe_cd_command(command, NULL, 0, false)) {
        psxe_cd_halt(CdlPause);
        return PSXE_CD_IO_ERROR;
    }
    psxe_cd_operation_active = operation;
    return PSXE_CD_OK;
}

psxe_cd_result psxe_cd_play_xa(uint32_t lba, uint8_t file, uint8_t channel, bool double_speed) {
    psxe_cd_result status = psxe_cd_require_idle();
    if (status != PSXE_CD_OK) {
        return status;
    }
    if ((lba >= PSXE_CD_LBA_LIMIT) || (channel > 31u)) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    if (!psxe_cd_filter(file, channel)) {
        return PSXE_CD_IO_ERROR;
    }
    return psxe_cd_play(lba, (uint8_t)(CdlModeRT | CdlModeSF | (double_speed ? CdlModeSpeed : 0)),
                        CdlReadS, PSXE_CD_XA);
}

psxe_cd_result psxe_cd_play_cdda(uint32_t lba) {
    psxe_cd_result status = psxe_cd_require_idle();
    if (status != PSXE_CD_OK) {
        return status;
    }
    if (lba >= PSXE_CD_LBA_LIMIT) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    return psxe_cd_play(lba, CdlModeDA | CdlModeAP, CdlPlay, PSXE_CD_CDDA);
}

psxe_cd_result psxe_cd_set_audio_volume(uint16_t left, uint16_t right) {
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    if ((left > 0x7fffu) || (right > 0x7fffu)) {
        return PSXE_CD_INVALID_ARGUMENT;
    }
    SPU_CD_VOL_L = left;
    SPU_CD_VOL_R = right;
    return PSXE_CD_OK;
}

psxe_cd_result psxe_cd_set_muted(bool muted) {
    if (!psxe_cd_ready) {
        return PSXE_CD_NOT_READY;
    }
    if (psxe_cd_operation_active == PSXE_CD_TRANSFER) {
        return PSXE_CD_BUSY;
    }
    return psxe_cd_command(muted ? CdlMute : CdlDemute, NULL, 0, false) ?
           PSXE_CD_OK : PSXE_CD_IO_ERROR;
}
