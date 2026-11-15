#include "debounce.h"

void debounce_init(debounce_t *d, bool initial) {
    d->state = initial;
    d->pending = false;
    d->since_us = 0;
}

bool debounce_update(debounce_t *d, bool raw, uint32_t now_us, uint32_t stable_us) {
    if (raw == d->state) {
        d->pending = false;               /* back to the accepted level: forget the candidate */
    } else if (!d->pending) {
        d->pending = true;                /* first sample at the new level: start the clock */
        d->since_us = now_us;
    } else if ((uint32_t)(now_us - d->since_us) >= stable_us) {
        d->state = raw;                   /* held long enough: accept it */
        d->pending = false;
    }
    return d->state;
}
