#ifndef PSXE_MEMORY_CARD_H
#define PSXE_MEMORY_CARD_H

#include <stddef.h>
#include <stdint.h>

#include "psxe/config.h"

typedef enum psxe_memory_card_result {
    PSXE_MEMORY_CARD_OK = 0,
    PSXE_MEMORY_CARD_INVALID_ARGUMENT,
    PSXE_MEMORY_CARD_NOT_FOUND,
    PSXE_MEMORY_CARD_IO_ERROR,
    PSXE_MEMORY_CARD_CORRUPT,
    PSXE_MEMORY_CARD_VERSION_MISMATCH,
    PSXE_MEMORY_CARD_NO_CARD,
    PSXE_MEMORY_CARD_UNFORMATTED,
    PSXE_MEMORY_CARD_FULL
} psxe_memory_card_result;

typedef struct psxe_memory_card_config {
    const char *product_code;
    const char *identifier;
    const char *title;
    uint16_t format_version;
} psxe_memory_card_config;

#define PSXE_MEMORY_CARD_MAX_FILES 15

typedef struct psxe_memory_card_file {
    char name[21];
    uint32_t size;
} psxe_memory_card_file;

void psxe_memory_card_init(void);
/* Slot 0 = bu00:, slot 1 = bu10:. All operations block. */
psxe_memory_card_result psxe_memory_card_status(unsigned int slot);
/* Stores up to capacity entries. count receives the total, even when truncated.
 * Pass files = NULL and capacity = 0 to query the count/free blocks only. */
psxe_memory_card_result psxe_memory_card_list(
    unsigned int slot, psxe_memory_card_file *files, size_t capacity,
    size_t *count, unsigned int *free_blocks
);
psxe_memory_card_result psxe_memory_card_delete(const psxe_memory_card_config *config, unsigned int slot);
psxe_memory_card_result psxe_memory_card_save(
    const psxe_memory_card_config *config,
    unsigned int slot,
    const void *payload,
    size_t payload_size
);
psxe_memory_card_result psxe_memory_card_load(
    const psxe_memory_card_config *config,
    unsigned int slot,
    void *payload,
    size_t payload_capacity,
    size_t *payload_size
);
const char *psxe_memory_card_result_string(psxe_memory_card_result result);

#endif
