#include "mouse_report.h"

void mouse_report_init(mouse_report_state_t *s) { s->last_buttons = 0; }

bool mouse_report_build(mouse_report_state_t *s, uint8_t buttons, int8_t x, int8_t y,
                        int8_t wheel, uint8_t out[MOUSE_REPORT_SIZE]) {
    buttons &= MOUSE_BTN_MASK;
    if (buttons == s->last_buttons && x == 0 && y == 0 && wheel == 0) {
        return false;                    /* nothing changed: send nothing */
    }
    out[0] = buttons;
    out[1] = (uint8_t)x;
    out[2] = (uint8_t)y;
    out[3] = (uint8_t)wheel;
    s->last_buttons = buttons;
    return true;
}
