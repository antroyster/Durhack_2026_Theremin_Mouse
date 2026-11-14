/* debounce.h - time-based button debounce. Pure C (no Pico SDK). */
#ifndef DEBOUNCE_H
#define DEBOUNCE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     state;    /* the accepted (debounced) level */
    bool     pending;  /* raw level currently differs from state */
    uint32_t since_us; /* when the difference was first seen */
} debounce_t;

void debounce_init(debounce_t *d, bool initial);

/*
 * Feed one raw sample taken at time now_us. A raw level that differs from the
 * accepted state must hold for stable_us before it is accepted. Returns the
 * accepted state. now_us may wrap; only differences are used.
 */
bool debounce_update(debounce_t *d, bool raw, uint32_t now_us, uint32_t stable_us);

#endif
