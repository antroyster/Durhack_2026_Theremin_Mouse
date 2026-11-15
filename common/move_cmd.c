#include "move_cmd.h"

void move_cmd_init(move_cmd_t *m) {
    m->state = 0;
    m->neg = false;
    m->digits = false;
    m->val = 0;
    m->dx = 0;
}

static int16_t finish_number(const move_cmd_t *m) {
    int32_t v = (m->val > MOVE_CMD_MAX) ? MOVE_CMD_MAX : m->val;
    return (int16_t)(m->neg ? -v : v);
}

static void start_number(move_cmd_t *m) {
    m->neg = false;
    m->digits = false;
    m->val = 0;
}

bool move_cmd_feed(move_cmd_t *m, uint8_t b, int16_t *dx, int16_t *dy) {
    if (b == '@') { m->state = 1; return false; }  /* '@' always starts afresh */
    switch (m->state) {
        case 1: m->state = (b == 'M') ? 2 : 0; return false;
        case 2:
            if (b != ' ') { m->state = 0; return false; }
            m->state = 3; start_number(m); return false;
        case 3:
        case 4:
            if (b == '-' && !m->digits && !m->neg) { m->neg = true; return false; }
            if (b >= '0' && b <= '9') {
                m->digits = true;
                if (m->val <= MOVE_CMD_MAX) m->val = m->val * 10 + (b - '0'); /* stops growing once past the limit */
                return false;
            }
            if (m->state == 3 && b == ' ' && m->digits) {
                m->dx = finish_number(m);
                m->state = 4; start_number(m); return false;
            }
            if (m->state == 4 && b == '\n' && m->digits) {
                *dx = m->dx;
                *dy = finish_number(m);
                m->state = 0;
                return true;
            }
            m->state = 0;                           /* anything else cancels the command */
            return false;
        default: return false;
    }
}
