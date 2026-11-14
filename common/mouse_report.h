/* mouse_report.h - builds the 4-byte HID mouse report. Pure C. */
#ifndef MOUSE_REPORT_H
#define MOUSE_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#define MOUSE_REPORT_SIZE 4u
#define MOUSE_BTN_LEFT    0x01u
#define MOUSE_BTN_RIGHT   0x02u
#define MOUSE_BTN_MIDDLE  0x04u
#define MOUSE_BTN_MASK    0x07u

typedef struct {
    uint8_t last_buttons; /* button byte of the last report that was sent */
} mouse_report_state_t;

void mouse_report_init(mouse_report_state_t *s);

/*
 * Fill out[] = { buttons, x, y, wheel } and return true if a report must be
 * sent: the buttons differ from the last report sent, or x, y or wheel is
 * non-zero. Returns false (and leaves the state alone) when nothing changed.
 * Call it only when the HID endpoint is free, and send whenever it says true.
 */
bool mouse_report_build(mouse_report_state_t *s, uint8_t buttons, int8_t x, int8_t y,
                        int8_t wheel, uint8_t out[MOUSE_REPORT_SIZE]);

#endif
