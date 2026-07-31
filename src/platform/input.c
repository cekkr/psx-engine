#include "psxe/input.h"

#include <string.h>

#include <psxapi.h>

static bool psxe_pad_type_supported(uint8_t type) {
    return (type == PAD_ID_DIGITAL) ||
           (type == PAD_ID_ANALOG_STICK) ||
           (type == PAD_ID_ANALOG);
}

void psxe_input_init(psxe_input *input) {
    memset(input, 0, sizeof(*input));
    InitPAD(input->buffers[0], sizeof(input->buffers[0]), input->buffers[1], sizeof(input->buffers[1]));
    StartPAD();
    ChangeClearPAD(0);
}

void psxe_input_poll(psxe_input *input) {
    unsigned int port;

    for (port = 0; port < 2; ++port) {
        const PADTYPE *raw = (const PADTYPE *)input->buffers[port];
        psxe_pad_state *state = &input->pads[port];
        uint16_t previous = state->held;
        uint16_t current = 0;

        state->connected = (raw->stat == 0) && psxe_pad_type_supported(raw->type);
        state->type = raw->type;
        state->left_x = 128;
        state->left_y = 128;
        state->right_x = 128;
        state->right_y = 128;

        if (state->connected) {
            current = (uint16_t)~raw->btn;
            if ((raw->type == PAD_ID_ANALOG_STICK) || (raw->type == PAD_ID_ANALOG)) {
                state->left_x = raw->ls_x;
                state->left_y = raw->ls_y;
                state->right_x = raw->rs_x;
                state->right_y = raw->rs_y;
            }
        }

        state->held = current;
        state->pressed = (uint16_t)(current & (uint16_t)~previous);
        state->released = (uint16_t)(previous & (uint16_t)~current);
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
