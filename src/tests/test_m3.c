/* M3 L0: optical clamp-and-carry. */
#include <stdint.h>
#include <stdlib.h>

#include "carry.h"
#include "check.h"
#include "sim_data.h"

int main(void) {
    carry_t c;

    /* One burst of 300 counts, then nothing: it must come out as 127 + 127 + 46. */
    carry_init(&c);
    int sum = 0, reports = 0, worst = 0;
    for (int i = 0; i < 10; i++) {
        int x = carry_update(&c, i == 0 ? 300 : 0);
        sum += x;
        if (x != 0) reports++;
        if (abs(x) > worst) worst = abs(x);
    }
    check_eq("M3 L0 burst of +300: sum sent", sum, 300);
    check_eq("M3 L0 burst of +300: reports used", reports, 3);
    check_range("M3 L0 burst of +300: largest |x| in a report", worst, 0, 127);
    check_eq("M3 L0 burst of +300: carry left over", c.r, 0);

    carry_init(&c);
    sum = 0;
    for (int i = 0; i < 10; i++) sum += carry_update(&c, i == 0 ? -300 : 0);
    check_eq("M3 L0 burst of -300: sum sent", sum, -300);

    /* Random stream: what is sent plus what is still carried always equals what came in. */
    srand(12345);
    carry_init(&c);
    long in = 0, out = 0;
    worst = 0;
    for (int i = 0; i < 100000; i++) {
        int delta = (rand() % 1201) - 600; /* -600..+600 counts per sample */
        in += delta;
        int x = carry_update(&c, delta);
        out += x;
        if (abs(x) > worst) worst = abs(x);
    }
    check_eq("M3 L0 random stream of 100000: in - (sent + carry)", (double)(in - (out + c.r)), 0);
    check_range("M3 L0 random stream: largest |x| in a report", worst, 0, 127);
    for (int i = 0; i < 100000 && c.r != 0; i++) out += carry_update(&c, 0);
    check_eq("M3 L0 random stream after draining: in - sent", (double)(in - out), 0);

    /* Scripted optical cycle: bursts + square path end where they started. */
    carry_t cx, cy;
    carry_init(&cx);
    carry_init(&cy);
    long sx = 0, sy = 0, ax = 0, ay = 0;
    worst = 0;
    for (uint32_t ms = 0; ms < SIM_CYCLE_MS; ms++) {
        int16_t dx, dy;
        sim_optical_delta(ms, &dx, &dy);
        int x = carry_update(&cx, dx), y = carry_update(&cy, dy);
        sx += x; sy += y; ax += abs(x); ay += abs(y);
        if (abs(x) > worst) worst = abs(x);
        if (abs(y) > worst) worst = abs(y);
    }
    check_eq("M3 L0 sim cycle: sum of REL_X", (double)sx, 0);
    check_eq("M3 L0 sim cycle: sum of REL_Y", (double)sy, 0);
    check_eq("M3 L0 sim cycle: sum of |REL_X|", (double)ax, SIM_OPT_ABS_TOTAL);
    check_eq("M3 L0 sim cycle: sum of |REL_Y|", (double)ay, SIM_OPT_ABS_TOTAL);
    check_range("M3 L0 sim cycle: largest |x| or |y| in a report", worst, 0, 127);

    return check_done();
}
