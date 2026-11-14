/*
 * sensors_sim.c - scripted sensors (-DSENSORS=SIM). A thin wrapper over
 * common/sim_data.c; no GPIO, ADC, SPI or I2C is touched.
 *
 * SAFETY RULE (spec, Testing section 3): the mouse channels (buttons,
 * joystick, optical) stay idle until the PC sends the run command on the CDC
 * port (matched in main.c cdc_rx_task). Then exactly one 30 s cycle plays and
 * the channels go idle again. The stop command ends a cycle early. The IMU is
 * not gated: it runs from boot.
 *
 *   main loop                       tick ISR
 *   s_request = REQ_RUN   ------->  s_active = true, s_tau_ms = 0
 *                                   s_tau_ms = 0, 1, 2 ... 29999   (one per tick)
 *                                   s_active = false               (idle again)
 *   s_request = REQ_STOP  ------->  s_active = false               (idle at once)
 *
 * Idle means: buttons all released, joystick at centre, no optical counts. A
 * stop in the middle of a click therefore releases the button; the debounce
 * passes the release on 5 ms later.
 *
 * tau is counted in ticks, not read from the clock, so one cycle is exactly
 * 30000 samples and matches the PC unit tests sample for sample.
 */
#include "sensors.h"

#include <stdbool.h>

#include "sim_data.h"

/* Written by the main loop, consumed by the tick ISR. It is ONE byte, so a
 * store is atomic on the Cortex-M0+ and no lock is needed; and because run
 * and stop share it, the most recent command always wins. */
enum { REQ_NONE = 0, REQ_RUN, REQ_STOP };
static volatile uint8_t s_request;

/* Owned by the tick ISR only. */
static bool     s_active; /* the mouse script is playing */
static uint32_t s_tau_ms; /* sample number inside the cycle, 0..29999 */
static uint32_t s_now_us; /* time of the current tick, for the IMU model */

void sensors_init(void) {
    s_request = REQ_NONE;
    s_active = false;
    s_tau_ms = 0;
    s_now_us = 0;
}

sensor_status_t sensors_status(sensor_id_t id) {
    (void)id;
    return SENSOR_OK; /* scripted sensors are always present */
}

const char *sensors_backend_name(void) { return "SIM"; }

void sensors_tick(uint32_t now_us) {
    s_now_us = now_us;
    uint8_t req = s_request; /* one read; a command arriving after it waits for the next tick */
    if (req != REQ_NONE) {
        s_request = REQ_NONE;
    }

    if (req == REQ_RUN) {
        s_active = true;  /* this tick is sample 0 of a new cycle */
        s_tau_ms = 0;
    } else if (req == REQ_STOP) {
        s_active = false; /* idle from this tick on: every button reads released */
    } else if (s_active) {
        s_tau_ms++;
        if (s_tau_ms >= SIM_CYCLE_MS) {
            s_active = false; /* 30000 samples played: idle until the next run command */
        }
    }
}

uint8_t sensors_buttons_raw(void) {
    return s_active ? sim_buttons(s_tau_ms * 1000u) : 0u;
}

uint16_t sensors_joystick_raw(void) {
    return s_active ? sim_joystick_adc(s_tau_ms * 1000u) : (uint16_t)SIM_ADC_CENTRE;
}

uint16_t sensors_joystick_centre(void) { return (uint16_t)SIM_ADC_CENTRE; }

const char *sensors_boot_info(void) { return "sim"; }

void sensors_optical_delta(int16_t *dx, int16_t *dy) {
    if (s_active) {
        sim_optical_delta(s_tau_ms, dx, dy);
    } else {
        *dx = 0;
        *dy = 0;
    }
}

void sensors_imu_raw(int16_t a[3], int16_t g[3]) {
    sim_imu_raw(s_now_us, a, g);
}

void sensors_sim_restart(void) { s_request = REQ_RUN; }

void sensors_sim_stop(void) { s_request = REQ_STOP; }
