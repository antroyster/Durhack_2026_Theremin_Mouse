/* M7 L0: the "@M dx dy\n" cursor command parser. */
#include <string.h>

#include "check.h"
#include "move_cmd.h"

static int16_t g_dx, g_dy;

/* Feed a string; returns how many complete commands it contained. Sums the movement. */
static int feed(move_cmd_t *m, const char *s) {
    int n = 0;
    for (; *s; s++) {
        int16_t dx, dy;
        if (move_cmd_feed(m, (uint8_t)*s, &dx, &dy)) { n++; g_dx += dx; g_dy += dy; }
    }
    return n;
}

int main(void) {
    move_cmd_t m;
    move_cmd_init(&m);

    g_dx = g_dy = 0;
    check_eq("M7 L0 '@M 12 -3': commands", feed(&m, "@M 12 -3\n"), 1);
    check_eq("M7 L0 '@M 12 -3': dx", g_dx, 12);
    check_eq("M7 L0 '@M 12 -3': dy", g_dy, -3);

    g_dx = g_dy = 0;
    check_eq("M7 L0 three commands back to back", feed(&m, "@M 1 1\n@M -5 0\n@M 0 7\n"), 3);
    check_eq("M7 L0 three commands: dx total", g_dx, -4);
    check_eq("M7 L0 three commands: dy total", g_dy, 8);

    g_dx = g_dy = 0;
    check_eq("M7 L0 over the limit is clamped: commands", feed(&m, "@M 99999 -99999\n"), 1);
    check_eq("M7 L0 clamped dx", g_dx, MOVE_CMD_MAX);
    check_eq("M7 L0 clamped dy", g_dy, -MOVE_CMD_MAX);

    g_dx = g_dy = 0;
    check_eq("M7 L0 malformed commands are ignored",
             feed(&m, "@M 5\n@M  5 5\n@M 5 5\r\n@M a b\n@RUN\n@STOP\nM 5 5\n@M 5 - 5\n@M --5 5\n@M 5 5 5\n"), 0);
    check_eq("M7 L0 '@' restarts a half-finished command", feed(&m, "@M 4@M 2 3\n"), 1);
    check_eq("M7 L0 restart: dx", g_dx, 2);
    check_eq("M7 L0 restart: dy", g_dy, 3);
    return check_done();
}
