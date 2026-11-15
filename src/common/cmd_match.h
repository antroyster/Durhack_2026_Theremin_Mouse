/*
 * cmd_match.h - recognises the bench commands on the CDC port. Pure C.
 *
 * A command is '@', a word, then '\n':   "@RUN\n"  start one sim cycle
 *                                        "@STOP\n" stop the sim cycle now
 * A single letter would be unsafe: a serial port opened without
 * `stty raw -echo` echoes the Pico's own text back to it. The firmware never
 * prints '@', so an echo can never form a command.
 */
#ifndef CMD_MATCH_H
#define CMD_MATCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { CMD_NONE = 0, CMD_RUN, CMD_STOP } cmd_t;

typedef struct {
    bool    active; /* an '@' has been seen and the word is being collected */
    uint8_t len;    /* letters collected so far */
    char    word[6];
} cmd_matcher_t;

void cmd_matcher_init(cmd_matcher_t *m);

/* Feed one received byte. Returns a command when its final '\n' arrives, else CMD_NONE. */
cmd_t cmd_matcher_feed(cmd_matcher_t *m, uint8_t byte);

#endif
