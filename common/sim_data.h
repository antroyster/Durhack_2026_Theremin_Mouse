/*
 * sim_data.h - scripted sensor data for testing (docs/phase1-spec.md,
 * Testing section 3). Pure functions of time: the same code feeds the Pico's
 * SIM sensor backend, the PC unit tests and bench/pico_sim, so expected
 * results can be calculated exactly.
 *
 * tau = time since the script was (re)started. One cycle lasts 30 s; outside
 * the cycle every mouse channel is idle. The IMU uses t_us (time since boot).
 */
#ifndef SIM_DATA_H
#define SIM_DATA_H

#include <stdint.h>

#define SIM_CYCLE_US        30000000u
#define SIM_CYCLE_MS        30000u

/* Buttons */
#define SIM_BTN_SLOTS       14u        /* clicks per button per cycle */
#define SIM_BTN_FIRST_US    1000000u   /* first slot starts at tau = 1 s */
#define SIM_BTN_SLOT_US     2000000u   /* one click of each button every 2 s */
#define SIM_BTN_HOLD_US     100000u    /* each click is held 100 ms */
#define SIM_BTN_RIGHT_US    500000u    /* right click offset inside a slot */
#define SIM_BTN_MIDDLE_US   1000000u   /* middle click offset inside a slot */
#define SIM_BTN_GLITCH_AT_US 1500000u  /* glitch offset inside a slot */
#define SIM_BOUNCE_US       3000u      /* edges bounce for 3 ms ... */
#define SIM_BOUNCE_STEP_US  500u       /* ... toggling every 0.5 ms */
#define SIM_GLITCH_US       4000u      /* glitch length: shorter than the debounce time */

/* Joystick */
#define SIM_ADC_MIN         0u
#define SIM_ADC_CENTRE      2048u
#define SIM_ADC_MAX         4095u

/* Optical */
#define SIM_OPT_BURST       300        /* counts in one 1 ms sample */
#define SIM_OPT_SIDE        200        /* counts per side of the square */
#define SIM_OPT_ABS_TOTAL   1000       /* sum of |dx| (and of |dy|) per cycle */

/* IMU */
#define SIM_IMU_AX_AMPL     8192       /* 0.5 g at +/-2 g full scale */
#define SIM_IMU_AZ          16384      /* 1 g */
#define SIM_IMU_GZ          11790      /* 90 deg/s at +/-250 deg/s full scale */

/* Raw button levels, bit0 left, bit1 right, bit2 middle, 1 = pressed. Includes bounce and glitches. */
uint8_t sim_buttons(uint32_t tau_us);

/* The intended joystick deflection u in Q15 (-32768..+32768), before the ADC. */
int32_t sim_joystick_u_q15(uint32_t tau_us);

/* The 12-bit ADC reading for that deflection (centre 2048). */
uint16_t sim_joystick_adc(uint32_t tau_us);

/* Optical counts in the 1 ms sample number tau_ms (0..29999). Call once per sample. */
void sim_optical_delta(uint32_t tau_ms, int16_t *dx, int16_t *dy);

/* Raw IMU counts at time t_us since boot. */
void sim_imu_raw(uint32_t t_us, int16_t a[3], int16_t g[3]);

#endif
