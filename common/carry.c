#include "carry.h"

void carry_init(carry_t *c) { c->r = 0; }

int8_t carry_update(carry_t *c, int32_t delta) {
    int32_t s = c->r + delta;
    int32_t x = s;
    if (x > 127) x = 127;
    if (x < -127) x = -127;
    c->r = s - x;
    return (int8_t)x;
}
