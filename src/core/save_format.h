#ifndef PSXE_SAVE_FORMAT_H
#define PSXE_SAVE_FORMAT_H

#include <stdbool.h>

#include "psxe/memory_card.h"

/* Write only the engine header and payload. The caller owns the BIOS header
 * and clears unused bytes in the block before writing the file. */
bool psxe_save_encode(uint8_t *block, size_t block_size, uint16_t version,
                      const void *payload, size_t payload_size);
psxe_memory_card_result psxe_save_decode(const uint8_t *block, size_t block_size,
                                        uint16_t version, void *payload,
                                        size_t capacity, size_t *payload_size);

#endif
