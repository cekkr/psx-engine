#include "save_format.h"

#include <string.h>

#include "byte_order.h"

#define PSXE_SAVE_DATA_OFFSET 512u
#define PSXE_SAVE_MAGIC 0x45535850u /* "PXSE" in little-endian byte order. */
#define PSXE_SAVE_HEADER_VERSION 1u
#define PSXE_SAVE_HEADER_SIZE 16u

static uint32_t psxe_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffu;

    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

bool psxe_save_encode(uint8_t *block, size_t block_size, uint16_t version,
                      const void *payload, size_t payload_size) {
    if ((block == NULL) || (block_size != PSXE_MEMORY_CARD_BLOCK_SIZE) ||
        (payload == NULL) || (payload_size == 0) || (payload_size > PSXE_MAX_SAVE_PAYLOAD)) {
        return false;
    }

    uint8_t *header = block + PSXE_SAVE_DATA_OFFSET;
    psxe_write_le32(header, PSXE_SAVE_MAGIC);
    psxe_write_le16(header + 4, PSXE_SAVE_HEADER_VERSION);
    psxe_write_le16(header + 6, version);
    psxe_write_le32(header + 8, (uint32_t)payload_size);
    psxe_write_le32(header + 12, psxe_crc32(payload, payload_size));
    memcpy(header + PSXE_SAVE_HEADER_SIZE, payload, payload_size);
    return true;
}

psxe_memory_card_result psxe_save_decode(const uint8_t *block, size_t block_size,
                                        uint16_t version, void *payload,
                                        size_t capacity, size_t *payload_size) {
    if ((block == NULL) || (payload == NULL) || (capacity == 0)) {
        return PSXE_MEMORY_CARD_INVALID_ARGUMENT;
    }
    if (block_size != PSXE_MEMORY_CARD_BLOCK_SIZE) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }

    const uint8_t *header = block + PSXE_SAVE_DATA_OFFSET;
    uint32_t stored_size = psxe_read_le32(header + 8);
    if ((psxe_read_le32(header) != PSXE_SAVE_MAGIC) ||
        (psxe_read_le16(header + 4) != PSXE_SAVE_HEADER_VERSION) ||
        (stored_size == 0) || (stored_size > PSXE_MAX_SAVE_PAYLOAD)) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }
    if (psxe_read_le16(header + 6) != version) {
        return PSXE_MEMORY_CARD_VERSION_MISMATCH;
    }
    if (stored_size > capacity) {
        return PSXE_MEMORY_CARD_INVALID_ARGUMENT;
    }

    const uint8_t *stored_payload = header + PSXE_SAVE_HEADER_SIZE;
    if (psxe_crc32(stored_payload, stored_size) != psxe_read_le32(header + 12)) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }
    memcpy(payload, stored_payload, stored_size);
    if (payload_size != NULL) {
        *payload_size = stored_size;
    }
    return PSXE_MEMORY_CARD_OK;
}
