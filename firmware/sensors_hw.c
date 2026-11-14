/*
 * sensors_hw.c - real sensors (-DSENSORS=HW).
 *
 * Milestone 2: the buttons and the joystick are real. The optical sensor (3)
 * and IMU (4) report SENSOR_OFF and return zeros.
 */
#include "sensors.h"

#include <stdbool.h>

#include "config.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static sensor_status_t s_status[SENSOR_COUNT];
static uint16_t s_joy_centre = CFG_JOY_CENTRE_DEFAULT; /* v0 */

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
    s_status[SENSOR_OPT] = SENSOR_OFF; /* milestone 3 */
    s_status[SENSOR_IMU] = SENSOR_OFF; /* milestone 4 */
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

void sensors_optical_delta(int16_t *dx, int16_t *dy) {
    *dx = 0;
    *dy = 0;
}

void sensors_imu_raw(int16_t a[3], int16_t g[3]) {
    for (int i = 0; i < 3; i++) {
        a[i] = 0;
        g[i] = 0;
    }
}

void sensors_sim_restart(void) {}

void sensors_sim_stop(void) {}
