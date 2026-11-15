/*
 * move_cmd.h - recognises the PC's cursor command on the CDC port. Pure C.
 *
 *   "@M <dx> <dy>\n"     e.g. "@M 12 -3\n"  = move the cursor 12 right, 3 up
 *
 * The PC program (theremin) works out cursor movement from
 * the IMU stream and sends it back; the Pico adds it to its HID mouse report,
 * so the PC still sees one ordinary USB mouse. dx, dy are decimal, each
 * limited to -MOVE_CMD_MAX..+MOVE_CMD_MAX (larger values are clamped).
 */
#ifndef MOVE_CMD_H
#define MOVE_CMD_H

#include <stdbool.h>
#include <stdint.h>

#define MOVE_CMD_MAX 2000

typedef struct {
    uint8_t state;   /* 0 idle, 1 seen '@', 2 seen 'M', 3 in dx, 4 in dy */
    bool    neg;     /* current number is negative */
    bool    digits;  /* current number has at least one digit */
    int32_t val;     /* current number, magnitude */
    int16_t dx;
} move_cmd_t;

void move_cmd_init(move_cmd_t *m);

/* Feed one received byte. Returns true when a complete command ended with
 * '\n'; then *dx, *dy hold the movement. */
bool move_cmd_feed(move_cmd_t *m, uint8_t byte, int16_t *dx, int16_t *dy);

#endif
