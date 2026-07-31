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
} psxe_pad_state;

typedef struct psxe_input {
    uint8_t buffers[2][34];
    psxe_pad_state pads[2];
} psxe_input;

void psxe_input_init(psxe_input *input);
void psxe_input_poll(psxe_input *input);
const psxe_pad_state *psxe_input_pad(const psxe_input *input, unsigned int port);
bool psxe_button_held(const psxe_pad_state *pad, PadButton button);
bool psxe_button_pressed(const psxe_pad_state *pad, PadButton button);
bool psxe_button_released(const psxe_pad_state *pad, PadButton button);

#endif
