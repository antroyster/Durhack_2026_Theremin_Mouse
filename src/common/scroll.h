/* scroll.h - joystick rate-control scrolling. Pure C, integer maths only. */
#ifndef SCROLL_H
#define SCROLL_H

#include <stdint.h>

#define SCROLL_Q15_ONE 32768 /* u = 1.0 in Q15 fixed point */

/* Convert a deadzone in thousandths (100 = 0.1) to Q15. */
#define SCROLL_DEADZONE_Q15(permille) ((int32_t)(((permille) * 32768L + 500L) / 1000L))

typedef struct {
    int64_t acc; /* accumulator a_k, in units of 1 / (32768 * 1e6) detent */
} scroll_t;

void scroll_init(scroll_t *s);

/*
 * u = (v - v0) / (vmax - v0) for v >= v0, (v - v0) / (v0 - vmin) below it.
 * Result is Q15, clamped to -32768..+32768, and 0 when |u| < deadzone_q15.
 */
int32_t scroll_normalise_q15(uint16_t v, uint16_t v0, uint16_t vmin, uint16_t vmax,
                             int32_t deadzone_q15);

/*
 * a_k = a_(k-1) + ks * u * dt ; w_k = trunc(a_k) ; a_k = a_k - w_k.
 * ks = detents per second at full tilt, dt_us = tick period in microseconds.
 * Returns w_k, the wheel value to send (clamped to -127..127, excess is kept).
 */
int8_t scroll_update(scroll_t *s, int32_t u_q15, uint32_t ks_detents_per_s, uint32_t dt_us);

#endif
