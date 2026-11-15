/*
 * sensors_hw.c - real sensors (-DSENSORS=HW).
 *
 * Milestone 4: buttons, joystick, optical sensor and IMU are all real.
 */
#include "sensors.h"

#include <stdbool.h>

#include "config.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include <stdio.h>

#include "imu_mpu6050.h"
#include "opt_pmw3360.h"

static sensor_status_t s_status[SENSOR_COUNT];
static uint16_t s_joy_centre = CFG_JOY_CENTRE_DEFAULT; /* v0 */
static char s_boot_info[100] = "none";

/* Mean of n ADC readings, 0.5 ms apart. Blocking, so boot time only. */
static uint16_t adc_mean(unsigned n) {
    uint32_t sum = 0;
    for (unsigned i = 0; i < n; i++) {
        sum += adc_read();
        sleep_us(500);
    }
    return (uint16_t)(sum / n);
}

/*
 * Joystick boot check (spec T7). The stick is a potentiometer: a low-resistance
 * voltage divider. The pin's weak internal pull resistors (about 50 k) cannot
 * move a real potentiometer much, but they swing an unconnected pin to the rails:
 *
 *     connected:     pull-up reading ~ pull-down reading
 *     not connected: pull-up reading ~ 4095, pull-down reading ~ 0
 *
 * So: read with each pull, compare, then measure the centre with pulls off.
 */
static void joystick_init(void) {
    adc_init();
    adc_gpio_init(CFG_PIN_JOY_ADC);           /* high-impedance analogue input, digital buffer off */
    adc_select_input(CFG_JOY_ADC_CHANNEL);

    gpio_pull_up(CFG_PIN_JOY_ADC);
    sleep_ms(2);
    uint16_t high = adc_mean(8);
    gpio_pull_down(CFG_PIN_JOY_ADC);
    sleep_ms(2);
    uint16_t low = adc_mean(8);
    gpio_disable_pulls(CFG_PIN_JOY_ADC);
    sleep_ms(2);

    uint16_t centre = adc_mean(CFG_JOY_CENTRE_SAMPLES);
    bool floating = (high > low) && ((uint16_t)(high - low) > CFG_JOY_FLOAT_DELTA);
    bool off_centre = (centre < CFG_JOY_CENTRE_LO) || (centre > CFG_JOY_CENTRE_HI);

    if (floating || off_centre) {
        s_joy_centre = CFG_JOY_CENTRE_DEFAULT;
        s_status[SENSOR_JOY] = SENSOR_MISSING; /* reported in the status line and on the LED */
    } else {
        s_joy_centre = centre;
        s_status[SENSOR_JOY] = SENSOR_OK;
    }
}

/* One button input: GPIO input with the internal pull-up, so the pin reads 1
 * when the switch is open and 0 when the switch connects it to GND. */
static void button_pin_init(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

void sensors_init(void) {
    button_pin_init(CFG_PIN_BTN_LEFT);
    button_pin_init(CFG_PIN_BTN_RIGHT);
    button_pin_init(CFG_PIN_BTN_MIDDLE);

    s_status[SENSOR_BTN] = SENSOR_OK;  /* plain switches: there is nothing to probe */
    joystick_init();
    /* Optical sensor: the ID registers decide whether it is there (spec B4: if
     * not, it contributes zeros and everything else keeps working). */
    pmw3360_info_t opt = {0};
#if CFG_OPT_ENABLE
    s_status[SENSOR_OPT] = pmw3360_init(&opt) ? SENSOR_OK : SENSOR_MISSING;
#else
    s_status[SENSOR_OPT] = SENSOR_OFF; /* switched off in config.h: SPI pins left untouched */
#endif

    /* IMU: WHO_AM_I decides. */
    uint8_t who;
    s_status[SENSOR_IMU] = mpu6050_init(&who) ? SENSOR_OK : SENSOR_MISSING;

    snprintf(s_boot_info, sizeof s_boot_info, "opt_id=0x%02x opt_inv=0x%02x opt_srom=0x%02x opt_cpi=%u imu_id=0x%02x",
             opt.product_id, opt.inverse_id, opt.srom_id, opt.cpi_ok ? (unsigned)CFG_OPT_CPI : 0u, who);
}

sensor_status_t sensors_status(sensor_id_t id) {
    return (id < SENSOR_COUNT) ? s_status[id] : SENSOR_OFF;
}

const char *sensors_backend_name(void) { return "HW"; }

void sensors_tick(uint32_t now_us) { (void)now_us; }

uint8_t sensors_buttons_raw(void) {
    /* One read of all GPIO levels, so the three buttons are sampled at the same instant. */
    uint32_t pins = gpio_get_all();
    uint8_t bits = 0;
    /* Active low: a 0 on the pin means pressed, so each level is inverted. */
    if (!(pins & (1u << CFG_PIN_BTN_LEFT)))   bits |= SENSORS_BTN_LEFT;
    if (!(pins & (1u << CFG_PIN_BTN_RIGHT)))  bits |= SENSORS_BTN_RIGHT;
    if (!(pins & (1u << CFG_PIN_BTN_MIDDLE))) bits |= SENSORS_BTN_MIDDLE;
    return bits;
}

uint16_t sensors_joystick_raw(void) {
    /* One conversion takes 2 us (96 cycles of the 48 MHz ADC clock): fine inside the 1 ms tick. */
    if (s_status[SENSOR_JOY] != SENSOR_OK) return s_joy_centre; /* missing: reads as "at rest" */
    return adc_read();
}

uint16_t sensors_joystick_centre(void) { return s_joy_centre; }

const char *sensors_boot_info(void) { return s_boot_info; }

void sensors_optical_delta(int16_t *dx, int16_t *dy) {
    *dx = 0;
    *dy = 0;
    if (s_status[SENSOR_OPT] != SENSOR_OK) return;

    int16_t sx, sy;
    pmw3360_read(&sx, &sy);            /* raw sensor axes */
    /* Mounting corrections, so the caller always gets +x right, +y down (spec T11). */
    if (CFG_OPT_SWAP_XY) { int16_t t = sx; sx = sy; sy = t; }
    *dx = CFG_OPT_INVERT_X ? (int16_t)-sx : sx;
    *dy = CFG_OPT_INVERT_Y ? (int16_t)-sy : sy;
}

/* IMU health, counted since boot. A sample is bad if the I2C read failed, or
 * if all six values are exactly 0 (a real chip always shows noise and gravity;
 * all zeros means it reset itself and is asleep). */
static volatile uint32_t s_imu_fail, s_imu_zero, s_imu_recover;
static uint16_t s_imu_bad_run; /* bad samples in a row */

void sensors_imu_raw(int16_t a[3], int16_t g[3]) {
    for (int i = 0; i < 3; i++) { a[i] = 0; g[i] = 0; }
    if (s_status[SENSOR_IMU] == SENSOR_OFF) return;
    /* While it is not answering, only try every 20th sample (10 per second), so
     * a dead or stuck bus cannot eat the tick's time. Zeros are sent meanwhile. */
    static uint8_t skip;
    if (s_status[SENSOR_IMU] == SENSOR_MISSING && ++skip < 20u) return;
    skip = 0;

    bool ok = mpu6050_read(a, g);
    bool zero = ok && !(a[0] | a[1] | a[2] | g[0] | g[1] | g[2]);
    if (ok && !zero) {
        s_imu_bad_run = 0;
        s_status[SENSOR_IMU] = SENSOR_OK;
        return;
    }
    if (!ok) s_imu_fail++; else s_imu_zero++;
    /* 0.5 s of bad samples in a row (or 5 tries once it is already marked missing): report it, try to wake the chip. */
    if (++s_imu_bad_run >= (s_status[SENSOR_IMU] == SENSOR_MISSING ? 5u : CFG_IMU_RATE_HZ / 2u)) {
        s_imu_bad_run = 0;
        s_status[SENSOR_IMU] = SENSOR_MISSING;
        s_imu_recover++;
        mpu6050_configure();                         /* 5 short writes; gives up after 2 ms if nothing answers */
    }
}

void sensors_imu_health(uint32_t *read_failures, uint32_t *zero_samples, uint32_t *recoveries) {
    *read_failures = s_imu_fail;
    *zero_samples = s_imu_zero;
    *recoveries = s_imu_recover;
}

void sensors_sim_restart(void) {}

void sensors_sim_stop(void) {}
