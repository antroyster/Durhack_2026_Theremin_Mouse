/* carry.h - optical clamp-and-carry. Pure C. */
#ifndef CARRY_H
#define CARRY_H

#include <stdint.h>

typedef struct {
    int32_t r; /* counts not yet sent */
} carry_t;

void carry_init(carry_t *c);

/* s = r + delta ; x = clamp(s, -127, 127) ; r = s - x. Returns x, the value to send. */
int8_t carry_update(carry_t *c, int32_t delta);

#endif
