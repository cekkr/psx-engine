#include "psxe/memory_card.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psxapi.h>
#include <sys/fcntl.h>

#define PSXE_SAVE_DATA_OFFSET 512u
#define PSXE_SAVE_MAGIC 0x45535850u /* "PXSE" in little-endian byte order. */
#define PSXE_SAVE_HEADER_VERSION 1u

typedef struct psxe_save_header {
    uint32_t magic;
    uint16_t header_version;
    uint16_t format_version;
    uint32_t payload_size;
    uint32_t payload_crc;
} psxe_save_header;

static uint8_t psxe_card_buffer[PSXE_MEMORY_CARD_BLOCK_SIZE] __attribute__((aligned(4)));

static uint32_t psxe_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    size_t index;
    unsigned int bit;

    for (index = 0; index < size; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static bool psxe_make_filename(
    char *output,
    size_t capacity,
    const psxe_memory_card_config *config,
    unsigned int slot
) {
    int count;
    size_t product_length;
    size_t identifier_length;

    if ((output == NULL) || (config == NULL) || (slot > 1) ||
        (config->product_code == NULL) || (config->identifier == NULL)) {
        return false;
    }

    product_length = strlen(config->product_code);
    identifier_length = strlen(config->identifier);
    if ((product_length == 0) || (identifier_length == 0) ||
        ((product_length + identifier_length) > 20)) {
        return false;
    }

    count = snprintf(output, capacity, "bu%02u:%s%s", slot, config->product_code, config->identifier);
    return (count > 0) && ((size_t)count < capacity);
}

static void psxe_write_card_visual_header(const psxe_memory_card_config *config) {
    static const uint16_t palette[16] = {
        0x0000, 0x0421, 0x0c63, 0x14a5,
        0x1ce7, 0x2529, 0x2d6b, 0x35ad,
        0x3def, 0x4631, 0x4e73, 0x56b5,
        0x5ef7, 0x6739, 0x6f7b, 0x7fff
    };
    size_t title_length;
    size_t index;

    psxe_card_buffer[0] = 'S';
    psxe_card_buffer[1] = 'C';
    psxe_card_buffer[2] = 0x11; /* One static 16x16 icon frame. */
    psxe_card_buffer[3] = 1;

    if (config->title != NULL) {
        title_length = strlen(config->title);
        if (title_length > 63) {
            title_length = 63;
        }
        memcpy(&psxe_card_buffer[4], config->title, title_length);
    }

    memcpy(&psxe_card_buffer[0x60], palette, sizeof(palette));
    for (index = 0; index < 128; ++index) {
        uint8_t x = (uint8_t)((index * 2u) & 15u);
        uint8_t y = (uint8_t)((index * 2u) >> 4);
        uint8_t a = (uint8_t)(((x ^ y) & 7u) + 6u);
        uint8_t b = (uint8_t)((((x + 1u) ^ y) & 7u) + 6u);
        psxe_card_buffer[0x80u + index] = (uint8_t)(a | (uint8_t)(b << 4));
    }
}

void psxe_memory_card_init(void) {
    InitCARD(0);
    StartCARD();
    _bu_init();
}

psxe_memory_card_result psxe_memory_card_save(
    const psxe_memory_card_config *config,
    unsigned int slot,
    const void *payload,
    size_t payload_size
) {
    psxe_save_header *header;
    char filename[32];
    int descriptor;
    int written;

    if ((payload == NULL) || (payload_size == 0) || (payload_size > PSXE_MAX_SAVE_PAYLOAD) ||
        !psxe_make_filename(filename, sizeof(filename), config, slot)) {
        return PSXE_MEMORY_CARD_INVALID_ARGUMENT;
    }

    memset(psxe_card_buffer, 0, sizeof(psxe_card_buffer));
    psxe_write_card_visual_header(config);
    header = (psxe_save_header *)&psxe_card_buffer[PSXE_SAVE_DATA_OFFSET];
    header->magic = PSXE_SAVE_MAGIC;
    header->header_version = PSXE_SAVE_HEADER_VERSION;
    header->format_version = config->format_version;
    header->payload_size = (uint32_t)payload_size;
    header->payload_crc = psxe_crc32((const uint8_t *)payload, payload_size);
    memcpy((uint8_t *)(header + 1), payload, payload_size);

    descriptor = open(filename, FWRITE | FCREATE | FTRUNC | FNBLOCKS(1));
    if (descriptor < 0) {
        return PSXE_MEMORY_CARD_IO_ERROR;
    }
    written = write(descriptor, psxe_card_buffer, sizeof(psxe_card_buffer));
    close(descriptor);
    return (written == (int)sizeof(psxe_card_buffer)) ? PSXE_MEMORY_CARD_OK : PSXE_MEMORY_CARD_IO_ERROR;
}

psxe_memory_card_result psxe_memory_card_load(
    const psxe_memory_card_config *config,
    unsigned int slot,
    void *payload,
    size_t payload_capacity,
    size_t *payload_size
) {
    const psxe_save_header *header;
    const uint8_t *stored_payload;
    char filename[32];
    int descriptor;
    int bytes_read;

    if ((payload == NULL) || (payload_capacity == 0) ||
        !psxe_make_filename(filename, sizeof(filename), config, slot)) {
        return PSXE_MEMORY_CARD_INVALID_ARGUMENT;
    }

    descriptor = open(filename, FREAD);
    if (descriptor < 0) {
        return PSXE_MEMORY_CARD_NOT_FOUND;
    }
    bytes_read = read(descriptor, psxe_card_buffer, sizeof(psxe_card_buffer));
    close(descriptor);
    if (bytes_read != (int)sizeof(psxe_card_buffer)) {
        return PSXE_MEMORY_CARD_IO_ERROR;
    }

    header = (const psxe_save_header *)&psxe_card_buffer[PSXE_SAVE_DATA_OFFSET];
    if ((header->magic != PSXE_SAVE_MAGIC) || (header->header_version != PSXE_SAVE_HEADER_VERSION) ||
        (header->payload_size == 0) || (header->payload_size > PSXE_MAX_SAVE_PAYLOAD)) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }
    if (header->format_version != config->format_version) {
        return PSXE_MEMORY_CARD_VERSION_MISMATCH;
    }
    if (header->payload_size > payload_capacity) {
        return PSXE_MEMORY_CARD_INVALID_ARGUMENT;
    }

    stored_payload = (const uint8_t *)(header + 1);
    if (psxe_crc32(stored_payload, header->payload_size) != header->payload_crc) {
        return PSXE_MEMORY_CARD_CORRUPT;
    }

    memcpy(payload, stored_payload, header->payload_size);
    if (payload_size != NULL) {
        *payload_size = header->payload_size;
    }
    return PSXE_MEMORY_CARD_OK;
}

const char *psxe_memory_card_result_string(psxe_memory_card_result result) {
    switch (result) {
        case PSXE_MEMORY_CARD_OK: return "OK";
        case PSXE_MEMORY_CARD_INVALID_ARGUMENT: return "INVALID ARGUMENT";
        case PSXE_MEMORY_CARD_NOT_FOUND: return "SAVE NOT FOUND";
        case PSXE_MEMORY_CARD_IO_ERROR: return "CARD I/O ERROR";
        case PSXE_MEMORY_CARD_CORRUPT: return "CORRUPT SAVE";
        case PSXE_MEMORY_CARD_VERSION_MISMATCH: return "SAVE VERSION MISMATCH";
        default: return "UNKNOWN CARD ERROR";
    }
}
