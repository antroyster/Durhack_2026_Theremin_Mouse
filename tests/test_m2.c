/* M2 L0: joystick normalisation, deadzone and the scroll accumulator. */
#include <stdint.h>

#include "check.h"
#include "scroll.h"
#include "sim_data.h"

#define KS      20u    /* detents per second at full tilt */
#define DT_US   1000u  /* 1 ms tick */
#define DEAD    SCROLL_DEADZONE_Q15(100) /* 0.1 */

static int32_t norm(uint16_t v) {
    return scroll_normalise_q15(v, SIM_ADC_CENTRE, SIM_ADC_MIN, SIM_ADC_MAX, DEAD);
}

/* Hold one ADC value for `ms` milliseconds and return the detents sent. */
static int hold(scroll_t *s, uint16_t v, int ms) {
    int total = 0;
    for (int i = 0; i < ms; i++) total += scroll_update(s, norm(v), KS, DT_US);
    return total;
}

int main(void) {
    scroll_t s;

    check_eq("M2 L0 normalise: centre", norm(2048), 0);
    check_eq("M2 L0 normalise: full positive (Q15)", norm(4095), 32768);
    check_eq("M2 L0 normalise: full negative (Q15)", norm(0), -32768);
    check_eq("M2 L0 normalise: u = 0.05 is inside the deadzone", norm(2048 + 102), 0);
    check_range("M2 L0 normalise: u just above 0.1 passes through (Q15)", norm(2048 + 210), 3277, 3400);

    scroll_init(&s);
    check_eq("M2 L0 inside deadzone for 10 s: detents", hold(&s, 2048 + 102, 10000), 0);
    scroll_init(&s);
    check_eq("M2 L0 negative side of deadzone for 10 s: detents", hold(&s, 2048 - 102, 10000), 0);

    scroll_init(&s);
    check_range("M2 L0 full tilt for 5 s: detents", hold(&s, 4095, 5000), 99, 101);
    scroll_init(&s);
    check_range("M2 L0 half tilt for 5 s: detents", hold(&s, 2048 + 1024, 5000), 49, 51);
    scroll_init(&s);
    check_range("M2 L0 full negative tilt for 5 s: detents", hold(&s, 0, 5000), -101, -99);
    scroll_init(&s);
    check_range("M2 L0 half negative tilt for 5 s: detents", hold(&s, 1024, 5000), -51, -49);

    /* The fraction is kept, never thrown away: 20 short holds add up like one long one. */
    scroll_init(&s);
    int pieces = 0;
    for (int i = 0; i < 20; i++) pieces += hold(&s, 4095, 250);
    check_range("M2 L0 full tilt in 20 pieces of 0.25 s: detents", pieces, 99, 101);

    /* Smoothness: at full tilt no single report carries more than 1 detent. */
    scroll_init(&s);
    int worst = 0;
    for (int i = 0; i < 5000; i++) {
        int w = scroll_update(&s, norm(4095), KS, DT_US);
        if (w > worst) worst = w;
    }
    check_eq("M2 L0 full tilt: largest wheel value in one report", worst, 1);

    /* Scripted joystick cycle (spec Testing section 3), checked segment by segment. */
    scroll_init(&s);
    int total = 0, after_ramp = 0, after_half = 0, after_neg = 0, after_drift = 0;
    for (uint32_t ms = 0; ms < SIM_CYCLE_MS; ms++) {
        total += scroll_update(&s, norm(sim_joystick_adc(ms * 1000u)), KS, DT_US);
        if (ms == 6999) after_ramp = total;
        if (ms == 12999) after_half = total;
        if (ms == 18999) after_neg = total;
        if (ms == 24999) after_drift = total;
    }
    /* Ramp: integral of 20*(t/5) from 0.5 s (deadzone edge) to 5 s = 49.5 detents. */
    check_range("M2 L0 sim ramp 0->full over 5 s: detents", after_ramp, 48, 51);
    check_range("M2 L0 sim half tilt 5 s: detents", after_half - after_ramp, 49, 51);
    check_range("M2 L0 sim negative tilt 5 s: detents", after_neg - after_half, -101, -99);
    check_eq("M2 L0 sim drift inside deadzone 5 s: detents", after_drift - after_neg, 0);
    check_range("M2 L0 sim whole cycle: wheel total", total, -2, 1);
    printf("INFO  M2 predicted REL_WHEEL total for one sim cycle = %d\n", total);

    return check_done();
}
