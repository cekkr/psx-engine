#ifndef PSXE_CONFIG_H
#define PSXE_CONFIG_H

/* Override these from the game target before including psxe.h if required. */
#ifndef PSXE_OT_LENGTH
#define PSXE_OT_LENGTH 1024
#endif

#ifndef PSXE_PACKET_BUFFER_SIZE
#define PSXE_PACKET_BUFFER_SIZE (48 * 1024)
#endif

#ifndef PSXE_MAX_ENTITIES
#define PSXE_MAX_ENTITIES 128
#endif

#ifndef PSXE_MEMORY_CARD_BLOCK_SIZE
#define PSXE_MEMORY_CARD_BLOCK_SIZE 8192
#endif

#define PSXE_MAX_AUDIO_VOICES 24
#define PSXE_MAX_SAVE_PAYLOAD (PSXE_MEMORY_CARD_BLOCK_SIZE - 544)

#endif
