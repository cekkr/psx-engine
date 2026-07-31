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
    PSXE_MEMORY_CARD_VERSION_MISMATCH
} psxe_memory_card_result;

typedef struct psxe_memory_card_config {
    const char *product_code;
    const char *identifier;
    const char *title;
    uint16_t format_version;
} psxe_memory_card_config;

void psxe_memory_card_init(void);
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
