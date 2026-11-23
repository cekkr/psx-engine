#ifndef PSXE_INPUT_H
#define PSXE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <psxpad.h>

typedef struct psxe_pad_state {
    uint16_t held;
    uint16_t pressed;
    uint16_t released;
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t type;
    bool connected;
    bool rumble_supported;
} psxe_pad_state;

typedef struct psxe_input {
    psxe_pad_state pads[2];
    uint32_t last_poll;
    uint8_t rumble_retry[2];
    uint8_t rumble_frames[2];
    uint8_t small_motor[2];
    uint8_t large_motor[2];
} psxe_input;

void psxe_input_init(psxe_input *input);
void psxe_input_poll(psxe_input *input);
/* Send motor levels for the next `frames` polls, or stop with frames = 0. */
bool psxe_input_rumble(psxe_input *input, unsigned int port, bool small, uint8_t large, uint8_t frames);
void psxe_input_stop_rumble(psxe_input *input);
const psxe_pad_state *psxe_input_pad(const psxe_input *input, unsigned int port);
bool psxe_button_held(const psxe_pad_state *pad, PadButton button);
bool psxe_button_pressed(const psxe_pad_state *pad, PadButton button);
bool psxe_button_released(const psxe_pad_state *pad, PadButton button);

#endif
