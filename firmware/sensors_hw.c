/*
 * sensors_hw.c - real sensors (-DSENSORS=HW).
 *
 * Milestone 1: only the buttons are real. Joystick (milestone 2), optical
 * sensor (3) and IMU (4) report SENSOR_OFF and return zeros.
 */
#include "sensors.h"

#include "config.h"
#include "hardware/gpio.h"

static sensor_status_t s_status[SENSOR_COUNT];

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
    s_status[SENSOR_JOY] = SENSOR_OFF; /* milestone 2 */
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

uint16_t sensors_joystick_raw(void) { return 0; }

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
