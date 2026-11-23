#include "psxe/input.h"

#include <string.h>

#include <hwregs_c.h>
#include <psxapi.h>
#include <psxgpu.h>

#define PSXE_SIO_TIMEOUT 2000u
#define PSXE_SIO_ACK 0x0200u
#define PSXE_SIO_ACK_LOW 0x0080u
#define PSXE_SIO_RX_READY 0x0002u
#define PSXE_SIO_DTR 0x0002u
#define PSXE_SIO_ACK_RESET 0x0010u
#define PSXE_SIO_IRQ_ENABLE 0x1000u
#define PSXE_SIO_IRQ 0x0080u
#define PSXE_PAD_IDLE_LIMIT 40u /* Below the DualShock's one-second watchdog. */

static bool psxe_sio_wait(uint16_t bit, bool set) {
    unsigned int timeout = PSXE_SIO_TIMEOUT;
    while (((SIO_STAT(0) & bit) != 0) != set) {
        if (--timeout == 0) {
            return false;
        }
    }
    return true;
}

/* A transaction is atomic with respect to the BIOS card handler on SIO0. */
static unsigned int psxe_pad_exchange(
    unsigned int port, const uint8_t request[9], uint8_t response[35]
) {
    uint16_t control = (uint16_t)(PSXE_SIO_IRQ_ENABLE | PSXE_SIO_DTR | 1u | (port << 13));
    unsigned int length = 3;
    unsigned int index;
    int restore_interrupts = EnterCriticalSection();

    SIO_CTRL(0) = PSXE_SIO_ACK_RESET;
    IRQ_STAT = (uint16_t)~PSXE_SIO_IRQ;
    SIO_MODE(0) = 0x000d; /* 8 bits, no parity, baud factor 1 */
    SIO_BAUD(0) = 0x0088;
    for (unsigned int i = 0; (i < 8u) && (SIO_STAT(0) & PSXE_SIO_RX_READY); ++i) {
        (void)SIO_DATA(0);
    }
    SIO_CTRL(0) = control;
    /* Controllers need about 20 us of select setup time at 33.87 MHz. */
    for (unsigned int delay = 0; delay < 400u; ++delay) {
        __asm__ volatile("nop");
    }

    for (index = 0; index < length; ++index) {
        SIO_DATA(0) = (index < 9u) ? request[index] : 0u;
        if (!psxe_sio_wait(PSXE_SIO_RX_READY, true)) {
            break;
        }
        response[index] = SIO_DATA(0);
        if (index == 1u) {
            unsigned int words = response[1] & 15u;
            if ((words == 0) || (response[1] == 0xffu)) {
                break;
            }
            length += words * 2u;
        }
        if ((index + 1u < length) &&
            (!psxe_sio_wait(PSXE_SIO_ACK, true) || !psxe_sio_wait(PSXE_SIO_ACK_LOW, false))) {
            break;
        }
        SIO_CTRL(0) = (uint16_t)(control | PSXE_SIO_ACK_RESET);
    }
    SIO_CTRL(0) = PSXE_SIO_ACK_RESET;
    IRQ_STAT = (uint16_t)~PSXE_SIO_IRQ;
    if (restore_interrupts) {
        ExitCriticalSection();
    }
    return (index == length) ? length : 0u;
}

static bool psxe_pad_type_supported(uint8_t type) {
    return (type == PAD_ID_DIGITAL) ||
           (type == PAD_ID_ANALOG_STICK) ||
           (type == PAD_ID_ANALOG);
}

static bool psxe_pad_configure(unsigned int port, bool already_in_config) {
    static const uint8_t enter[9] = { 1, PAD_CMD_CONFIG_MODE, 0, 1 };
    static const uint8_t map[9] = { 1, PAD_CMD_REQUEST_CONFIG, 0, 0, 1, 255, 255, 255, 255 };
    static const uint8_t leave[9] = { 1, PAD_CMD_CONFIG_MODE, 0, 0, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a };
    uint8_t response[35];
    bool mapped;

    if (!already_in_config && !psxe_pad_exchange(port, enter, response)) {
        return false;
    }
    mapped = (psxe_pad_exchange(port, map, response) == 9u) &&
             (response[1] == 0xf3u) && (response[2] == 0x5au);
    return (psxe_pad_exchange(port, leave, response) == 9u) &&
           (response[1] == 0xf3u) && mapped;
}

void psxe_input_init(psxe_input *input) {
    memset(input, 0, sizeof(*input));
    for (unsigned int port = 0; port < 2; ++port) {
        input->pads[port].type = PAD_ID_NONE;
        input->pads[port].left_x = input->pads[port].left_y = 128;
        input->pads[port].right_x = input->pads[port].right_y = 128;
    }
}

void psxe_input_poll(psxe_input *input) {
    uint32_t now = (uint32_t)VSync(-1);
    bool idle = now - input->last_poll > PSXE_PAD_IDLE_LIMIT;
    input->last_poll = now;

    for (unsigned int port = 0; port < 2; ++port) {
        psxe_pad_state *state = &input->pads[port];
        uint16_t previous = state->held;
        uint16_t current = 0;
        uint8_t previous_type = state->type;
        bool vibrating = !idle && state->rumble_supported && input->rumble_frames[port];
        const uint8_t request[9] = {
            1, PAD_CMD_READ, 0,
            vibrating ? input->small_motor[port] : 0,
            vibrating ? input->large_motor[port] : 0
        };
        uint8_t response[35];
        unsigned int length = psxe_pad_exchange(port, request, response);
        bool config = (length == 9u) && (response[1] == 0xf3u) && (response[2] == 0x5au);

        state->type = (length >= 5u) ? (uint8_t)(response[1] >> 4) : PAD_ID_NONE;
        state->connected = (length >= 5u) && ((response[2] == 0x5au) || !response[2]) &&
                           psxe_pad_type_supported(state->type) &&
                           ((state->type == PAD_ID_DIGITAL) || (length >= 9u));
        state->left_x = 128;
        state->left_y = 128;
        state->right_x = 128;
        state->right_y = 128;

        if (idle || !state->connected || (state->type != previous_type)) {
            state->rumble_supported = false;
            input->rumble_frames[port] = 0;
            input->rumble_retry[port] = 0;
        }
        if (config || state->connected) {
            if (state->connected) {
                current = (uint16_t)~(response[3] | ((uint16_t)response[4] << 8));
                if (length >= 9u && state->type != PAD_ID_DIGITAL) {
                    state->right_x = response[5];
                    state->right_y = response[6];
                    state->left_x = response[7];
                    state->left_y = response[8];
                }
            }
            if (config || (!state->rumble_supported && !input->rumble_retry[port] &&
                           (state->type != PAD_ID_ANALOG_STICK))) {
                bool configured = psxe_pad_configure(port, config);
                state->rumble_supported = state->connected && configured;
                input->rumble_retry[port] = 120;
            } else if (input->rumble_retry[port]) {
                --input->rumble_retry[port];
            }
            if (input->rumble_frames[port]) {
                --input->rumble_frames[port];
            }
        }

        state->held = current;
        state->pressed = (uint16_t)(current & (uint16_t)~previous);
        state->released = (uint16_t)(previous & (uint16_t)~current);
    }
}

bool psxe_input_rumble(psxe_input *input, unsigned int port, bool small, uint8_t large, uint8_t frames) {
    if ((input == NULL) || (port >= 2) || !input->pads[port].rumble_supported) {
        return false;
    }
    input->small_motor[port] = small ? 1u : 0u;
    input->large_motor[port] = large;
    input->rumble_frames[port] = frames;
    return true;
}

void psxe_input_stop_rumble(psxe_input *input) {
    if (input != NULL) {
        const uint8_t request[9] = { 1, PAD_CMD_READ };
        uint8_t response[35];
        for (unsigned int port = 0; port < 2; ++port) {
            input->rumble_frames[port] = 0;
            if (input->pads[port].rumble_supported) {
                psxe_pad_exchange(port, request, response);
            }
        }
    }
}

const psxe_pad_state *psxe_input_pad(const psxe_input *input, unsigned int port) {
    if ((input == NULL) || (port >= 2)) {
        return NULL;
    }
    return &input->pads[port];
}

bool psxe_button_held(const psxe_pad_state *pad, PadButton button) {
    return (pad != NULL) && ((pad->held & (uint16_t)button) != 0);
}

bool psxe_button_pressed(const psxe_pad_state *pad, PadButton button) {
    return (pad != NULL) && ((pad->pressed & (uint16_t)button) != 0);
}

bool psxe_button_released(const psxe_pad_state *pad, PadButton button) {
    return (pad != NULL) && ((pad->released & (uint16_t)button) != 0);
}
