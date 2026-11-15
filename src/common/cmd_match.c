#include "cmd_match.h"

#include <string.h>

void cmd_matcher_init(cmd_matcher_t *m) {
    m->active = false;
    m->len = 0;
}

cmd_t cmd_matcher_feed(cmd_matcher_t *m, uint8_t byte) {
    if (byte == '@') {                 /* '@' always starts a fresh command */
        m->active = true;
        m->len = 0;
        return CMD_NONE;
    }
    if (!m->active) return CMD_NONE;   /* ordinary bytes outside a command are ignored */

    if (byte == '\n') {                /* end of command: is the word one we know? */
        cmd_t cmd = CMD_NONE;
        if (m->len == 3 && memcmp(m->word, "RUN", 3) == 0) cmd = CMD_RUN;
        if (m->len == 4 && memcmp(m->word, "STOP", 4) == 0) cmd = CMD_STOP;
        m->active = false;
        return cmd;
    }
    if (byte < 'A' || byte > 'Z' || m->len >= sizeof m->word) {
        m->active = false;             /* anything but capital letters (e.g. '\r') cancels it */
        return CMD_NONE;
    }
    m->word[m->len++] = (char)byte;
    return CMD_NONE;
}
