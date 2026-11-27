#ifndef PSXE_BYTE_ORDER_H
#define PSXE_BYTE_ORDER_H

#include <stdint.h>

static inline uint16_t psxe_read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t psxe_read_le32(const uint8_t *bytes) {
    return (uint32_t)psxe_read_le16(bytes) | ((uint32_t)psxe_read_le16(bytes + 2) << 16);
}

static inline uint32_t psxe_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static inline void psxe_write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void psxe_write_le32(uint8_t *bytes, uint32_t value) {
    psxe_write_le16(bytes, (uint16_t)value);
    psxe_write_le16(bytes + 2, (uint16_t)(value >> 16));
}

#endif
