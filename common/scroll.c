#include "scroll.h"

/* One whole detent in accumulator units: Q15 scale times microseconds per second. */
#define SCROLL_ONE_DETENT ((int64_t)SCROLL_Q15_ONE * 1000000LL)

void scroll_init(scroll_t *s) { s->acc = 0; }

int32_t scroll_normalise_q15(uint16_t v, uint16_t v0, uint16_t vmin, uint16_t vmax,
                             int32_t deadzone_q15) {
    int32_t num = (int32_t)v - (int32_t)v0;
    int32_t den = (num >= 0) ? ((int32_t)vmax - (int32_t)v0) : ((int32_t)v0 - (int32_t)vmin);
    if (den <= 0) return 0;                       /* bad calibration: no scrolling */

    int32_t u = (int32_t)(((int64_t)num * SCROLL_Q15_ONE) / den);
    if (u > SCROLL_Q15_ONE) u = SCROLL_Q15_ONE;
    if (u < -SCROLL_Q15_ONE) u = -SCROLL_Q15_ONE;

    int32_t mag = (u < 0) ? -u : u;
    return (mag < deadzone_q15) ? 0 : u;          /* hard deadzone, no rescaling */
}

int8_t scroll_update(scroll_t *s, int32_t u_q15, uint32_t ks_detents_per_s, uint32_t dt_us) {
    s->acc += (int64_t)ks_detents_per_s * (int64_t)u_q15 * (int64_t)dt_us;

    int64_t w = s->acc / SCROLL_ONE_DETENT;       /* C division truncates toward zero */
    if (w > 127) w = 127;
    if (w < -127) w = -127;
    s->acc -= w * SCROLL_ONE_DETENT;              /* keep the fraction (and any excess) */
    return (int8_t)w;
}
