#ifndef PSXE_CD_H
#define PSXE_CD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PSXE_CD_SECTOR_SIZE 2048u
#define PSXE_CD_RAW_SECTOR_SIZE 2340u

typedef enum psxe_cd_result {
    PSXE_CD_OK = 0,
    PSXE_CD_PENDING,
    PSXE_CD_BUSY,
    PSXE_CD_INVALID_ARGUMENT,
    PSXE_CD_NOT_READY,
    PSXE_CD_NOT_FOUND,
    PSXE_CD_BAD_FILESYSTEM,
    PSXE_CD_IO_ERROR
} psxe_cd_result;

typedef enum psxe_cd_sector_mode {
    PSXE_CD_DATA,
    PSXE_CD_RAW /* Full data sector minus its 12-byte sync field. */
} psxe_cd_sector_mode;

typedef struct psxe_cd_file {
    uint32_t lba;
    uint32_t size;
} psxe_cd_file;

typedef struct psxe_cd_drive_status {
    bool lid_open;
    bool motor_on;
    bool seeking;
    bool reading; /* Data or XA. */
    bool playing; /* CD-DA. */
} psxe_cd_drive_status;

/* Single drive, owned by this module. Call after renderer initialization. */
psxe_cd_result psxe_cd_init(void);
psxe_cd_result psxe_cd_status(psxe_cd_drive_status *status);
psxe_cd_result psxe_cd_seek(uint32_t lba, bool audio_track);
psxe_cd_result psxe_cd_position(uint32_t *lba);
psxe_cd_result psxe_cd_motor_on(void);
psxe_cd_result psxe_cd_pause(void);
psxe_cd_result psxe_cd_stop(void); /* Stop transport and spindle. */

/* IRQ-driven transfer. Keep destination alive until completion or cancellation.
 * The address must be word-aligned, and capacity is in bytes. */
psxe_cd_result psxe_cd_read_start(
    uint32_t lba, uint32_t sectors, psxe_cd_sector_mode mode,
    void *destination, size_t capacity
);
psxe_cd_result psxe_cd_read_poll(void);
psxe_cd_result psxe_cd_read_wait(void);
psxe_cd_result psxe_cd_read_cancel(void);
/* Successful data and raw DMA sectors since boot, including filesystem reads. */
uint32_t psxe_cd_sectors_read(void);

/* Blocking ISO9660 lookup and exact byte-range read. */
psxe_cd_result psxe_cd_find(const char *path, psxe_cd_file *file);
psxe_cd_result psxe_cd_read_file(const psxe_cd_file *file, uint32_t offset, void *destination, size_t size);

/* XA streams run until paused/stopped or the disc ends. CD-DA auto-pauses
 * at the end of the current track. LBA is relative to 00:02:00. */
psxe_cd_result psxe_cd_play_xa(uint32_t lba, uint8_t file, uint8_t channel, bool double_speed);
psxe_cd_result psxe_cd_xa_filter(uint8_t file, uint8_t channel);
psxe_cd_result psxe_cd_play_cdda(uint32_t lba);
psxe_cd_result psxe_cd_set_audio_volume(uint16_t left, uint16_t right);
psxe_cd_result psxe_cd_set_muted(bool muted);

#endif
