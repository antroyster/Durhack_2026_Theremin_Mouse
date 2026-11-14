/* M1 L0: debounce, the "send only on change" report builder, and the scripted clicks. */
#include <stdbool.h>
#include <stdint.h>

#include "check.h"
#include "debounce.h"
#include "mouse_report.h"
#include "sim_data.h"

#define DEBOUNCE_US 5000u

/* Run a raw waveform through the debouncer and count accepted presses and releases. */
typedef bool (*wave_fn)(uint32_t t_us);

static void count_events(wave_fn wave, uint32_t end_us, uint32_t step_us, int *presses,
                         int *releases, uint32_t *first_press_us) {
    debounce_t d;
    debounce_init(&d, false);
    bool prev = false;
    *presses = *releases = 0;
    *first_press_us = 0;
    for (uint32_t t = 0; t < end_us; t += step_us) {
        bool now = debounce_update(&d, wave(t), t, DEBOUNCE_US);
        if (now && !prev) {
            if (*presses == 0) *first_press_us = t;
            (*presses)++;
        }
        if (!now && prev) (*releases)++;
        prev = now;
    }
}

/* A press at 10 ms whose edge toggles every 0.5 ms for 3 ms, then stays down. */
static bool wave_bouncy_press(uint32_t t) {
    if (t < 10000u) return false;
    uint32_t d = t - 10000u;
    if (d < 3000u) return ((d / 500u) % 2u) == 0u;
    return true;
}

/* A 4 ms blip at 10 ms: shorter than the debounce time. */
static bool wave_glitch_4ms(uint32_t t) { return t >= 10000u && t < 14000u; }

/* A 6 ms pulse: longer than the debounce time, so it is a real (short) click. */
static bool wave_pulse_6ms(uint32_t t) { return t >= 10000u && t < 16000u; }

static bool wave_sim_left(uint32_t t) { return (sim_buttons(t) & 0x01u) != 0; }
static bool wave_sim_right(uint32_t t) { return (sim_buttons(t) & 0x02u) != 0; }
static bool wave_sim_middle(uint32_t t) { return (sim_buttons(t) & 0x04u) != 0; }

int main(void) {
    int p, r;
    uint32_t first;

    /* --- debounce --- */
    count_events(wave_bouncy_press, 100000u, 100u, &p, &r, &first);
    check_eq("M1 L0 bouncy press, 0.1 ms sampling: press events", p, 1);
    check_eq("M1 L0 bouncy press, 0.1 ms sampling: release events", r, 0);
    /* Bounce ends 3 ms after the edge, then 5 ms stable: accepted 8 ms after the edge. */
    check_range("M1 L0 bouncy press: delay after edge [ms]", (first - 10000u) / 1000.0, 7.9, 8.1);

    count_events(wave_bouncy_press, 100000u, 1000u, &p, &r, &first);
    check_eq("M1 L0 bouncy press, 1 ms sampling: press events", p, 1);

    count_events(wave_glitch_4ms, 100000u, 100u, &p, &r, &first);
    check_eq("M1 L0 4 ms glitch, 0.1 ms sampling: events", p + r, 0);
    count_events(wave_glitch_4ms, 100000u, 1000u, &p, &r, &first);
    check_eq("M1 L0 4 ms glitch, 1 ms sampling: events", p + r, 0);

    count_events(wave_pulse_6ms, 100000u, 100u, &p, &r, &first);
    check_eq("M1 L0 6 ms pulse is a real click: presses", p, 1);
    check_eq("M1 L0 6 ms pulse is a real click: releases", r, 1);

    /* --- scripted clicks: 14 per button per cycle, glitches ignored --- */
    wave_fn waves[3] = { wave_sim_left, wave_sim_right, wave_sim_middle };
    const char *press_names[3] = { "M1 L0 sim cycle: left presses", "M1 L0 sim cycle: right presses",
                                   "M1 L0 sim cycle: middle presses" };
    const char *rel_names[3] = { "M1 L0 sim cycle: left releases", "M1 L0 sim cycle: right releases",
                                 "M1 L0 sim cycle: middle releases" };
    for (int b = 0; b < 3; b++) {
        count_events(waves[b], SIM_CYCLE_US, 1000u, &p, &r, &first);
        check_eq(press_names[b], p, SIM_BTN_SLOTS);
        check_eq(rel_names[b], r, SIM_BTN_SLOTS);
    }
    count_events(wave_sim_left, SIM_CYCLE_US, 100u, &p, &r, &first);
    check_eq("M1 L0 sim cycle, 0.1 ms sampling: left presses", p, SIM_BTN_SLOTS);

    /* --- report builder: nothing changed = nothing sent --- */
    mouse_report_state_t st;
    uint8_t rep[MOUSE_REPORT_SIZE];
    int sent = 0;

    mouse_report_init(&st);
    for (int i = 0; i < 1000; i++) sent += mouse_report_build(&st, 0, 0, 0, 0, rep);
    check_eq("M1 L0 idle for 1000 ticks: reports", sent, 0);

    sent = 0;
    for (int i = 0; i < 1000; i++) sent += mouse_report_build(&st, MOUSE_BTN_LEFT, 0, 0, 0, rep);
    check_eq("M1 L0 button held for 1000 ticks: reports", sent, 1);
    check_eq("M1 L0 report byte 0 while left is held", rep[0], 0x01);

    sent = 0;
    for (int i = 0; i < 1000; i++) sent += mouse_report_build(&st, 0, 0, 0, 0, rep);
    check_eq("M1 L0 button released, then idle: reports", sent, 1);
    check_eq("M1 L0 report byte 0 after release", rep[0], 0x00);

    sent = 0;
    for (int i = 0; i < 10; i++) sent += mouse_report_build(&st, 0, 5, -3, 1, rep);
    check_eq("M1 L0 motion on 10 ticks: reports", sent, 10);
    check_eq("M1 L0 report byte 1 (x = 5)", rep[1], 5);
    check_eq("M1 L0 report byte 2 (y = -3 as two's complement)", rep[2], 0xFD);
    check_eq("M1 L0 report byte 3 (wheel = 1)", rep[3], 1);
    check_eq("M1 L0 bits above bit 2 are masked off", (mouse_report_build(&st, 0xF8, 0, 0, 0, rep)), 0);

    /* --- whole M1 pipeline on the scripted cycle: raw -> debounce -> report --- */
    debounce_t db[3];
    for (int b = 0; b < 3; b++) debounce_init(&db[b], false);
    mouse_report_init(&st);
    sent = 0;
    for (uint32_t t = 0; t < SIM_CYCLE_US; t += 1000u) {
        uint8_t raw = sim_buttons(t), bits = 0;
        for (int b = 0; b < 3; b++) {
            if (debounce_update(&db[b], (raw >> b) & 1u, t, DEBOUNCE_US)) bits |= (uint8_t)(1u << b);
        }
        sent += mouse_report_build(&st, bits, 0, 0, 0, rep);
    }
    /* 14 clicks x 3 buttons x (press + release) = 84 reports, and not one more. */
    check_eq("M1 L0 sim cycle through the pipeline: HID reports", sent, 84);

    return check_done();
}
