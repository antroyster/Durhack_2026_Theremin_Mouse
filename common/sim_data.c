#include "sim_data.h"

#include <math.h>
#include <stdbool.h>

/* One click with bouncy edges. tau and start are in microseconds within a slot. */
static bool bouncy_pulse(uint32_t tau, uint32_t start, uint32_t hold) {
    if (tau < start) return false;
    uint32_t d = tau - start;
    if (d < SIM_BOUNCE_US) return ((d / SIM_BOUNCE_STEP_US) % 2u) == 0u; /* press edge bouncing */
    if (d < hold) return true;                                           /* held */
    d -= hold;
    if (d < SIM_BOUNCE_US) return ((d / SIM_BOUNCE_STEP_US) % 2u) == 1u; /* release edge bouncing */
    return false;
}

uint8_t sim_buttons(uint32_t tau_us) {
    if (tau_us < SIM_BTN_FIRST_US) return 0;
    uint32_t t = tau_us - SIM_BTN_FIRST_US;
    uint32_t slot = t / SIM_BTN_SLOT_US;
    uint32_t off = t % SIM_BTN_SLOT_US;
    if (slot >= SIM_BTN_SLOTS) return 0;

    uint8_t bits = 0;
    if (bouncy_pulse(off, 0u, SIM_BTN_HOLD_US)) bits |= 0x01u;
    if (bouncy_pulse(off, SIM_BTN_RIGHT_US, SIM_BTN_HOLD_US)) bits |= 0x02u;
    if (bouncy_pulse(off, SIM_BTN_MIDDLE_US, SIM_BTN_HOLD_US)) bits |= 0x04u;

    /* A 4 ms glitch on one button in turn: too short to be a real press. */
    if (off >= SIM_BTN_GLITCH_AT_US && off < SIM_BTN_GLITCH_AT_US + SIM_GLITCH_US) {
        bits |= (uint8_t)(1u << (slot % 3u));
    }
    return bits;
}

int32_t sim_joystick_u_q15(uint32_t tau_us) {
    if (tau_us >= 1000000u && tau_us < 6000000u) {            /* ramp 0 -> 1 over 5 s */
        return (int32_t)(((int64_t)(tau_us - 1000000u) * 32768) / 5000000);
    }
    if (tau_us >= 7000000u && tau_us < 12000000u) return 16384;   /* half tilt */
    if (tau_us >= 13000000u && tau_us < 18000000u) return -32768; /* full negative tilt */
    if (tau_us >= 19000000u && tau_us < 24000000u) return 1638;   /* 0.05: inside the deadzone */
    return 0;
}

uint16_t sim_joystick_adc(uint32_t tau_us) {
    int32_t u = sim_joystick_u_q15(tau_us);
    int32_t span = (u >= 0) ? (int32_t)(SIM_ADC_MAX - SIM_ADC_CENTRE)
                            : (int32_t)(SIM_ADC_CENTRE - SIM_ADC_MIN);
    return (uint16_t)((int32_t)SIM_ADC_CENTRE + (u * span) / 32768);
}

void sim_optical_delta(uint32_t tau_ms, int16_t *dx, int16_t *dy) {
    *dx = 0;
    *dy = 0;
    switch (tau_ms) {                    /* single-sample bursts */
        case 2000: *dx = SIM_OPT_BURST; return;
        case 3000: *dx = -SIM_OPT_BURST; return;
        case 4000: *dy = SIM_OPT_BURST; return;
        case 5000: *dy = -SIM_OPT_BURST; return;
        default: break;
    }
    /* Square path: 1 count every 5 ms, 200 counts per 1 s side. */
    if (tau_ms >= 8000u && tau_ms < 12000u && (tau_ms % 5u) == 0u) {
        switch ((tau_ms - 8000u) / 1000u) {
            case 0: *dx = 1; break;
            case 1: *dy = 1; break;
            case 2: *dx = -1; break;
            default: *dy = -1; break;
        }
    }
}

void sim_imu_raw(uint32_t t_us, int16_t a[3], int16_t g[3]) {
    uint32_t phase_us = t_us % 1000000u;                 /* position inside the current second */
    float phase = (float)phase_us / 1000000.0f;          /* 0..1 */
    a[0] = (int16_t)lroundf((float)SIM_IMU_AX_AMPL * sinf(6.28318530718f * phase));
    a[1] = 0;
    a[2] = SIM_IMU_AZ;
    g[0] = 0;
    g[1] = 0;
    g[2] = (phase_us < 500000u) ? SIM_IMU_GZ : -SIM_IMU_GZ;
}
