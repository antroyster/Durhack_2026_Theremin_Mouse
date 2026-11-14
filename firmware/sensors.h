/*
 * sensors.h - the one interface between the firmware logic and its sensors.
 *
 * Two backends implement it; CMake picks one with -DSENSORS=HW or -DSENSORS=SIM:
 *
 *                 main.c (timer tick + USB)
 *                          |
 *                      sensors.h
 *                     /         \
 *          sensors_hw.c         sensors_sim.c
 *          real GPIO/ADC/       scripted data from
 *          SPI/I2C              common/sim_data.c
 *
 * Calling rules:
 *  - sensors_init() once from main(), before the tick timer starts.
 *  - sensors_tick() and the four read functions are called ONLY from the
 *    1 kHz timer interrupt, sensors_tick() first. They must never block.
 *  - sensors_status(), sensors_backend_name(), sensors_sim_restart() and
 *    sensors_sim_stop() are
 *    safe to call from the main loop.
 */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

typedef enum {
    SENSOR_OK = 0,   /* present and answering */
    SENSOR_MISSING,  /* built, but its ID check or boot check failed */
    SENSOR_OFF       /* not built yet (comes in a later milestone) */
} sensor_status_t;

typedef enum {
    SENSOR_BTN = 0,  /* the three buttons */
    SENSOR_JOY,      /* joystick Y axis (ADC) */
    SENSOR_OPT,      /* optical sensor */
    SENSOR_IMU,      /* accelerometer + gyroscope */
    SENSOR_COUNT
} sensor_id_t;

/* Bits of sensors_buttons_raw(); the same layout as byte 0 of the HID report. */
#define SENSORS_BTN_LEFT   0x01u
#define SENSORS_BTN_RIGHT  0x02u
#define SENSORS_BTN_MIDDLE 0x04u

/* Set up every sensor and record its status. */
void sensors_init(void);

/* Status recorded by sensors_init(). */
sensor_status_t sensors_status(sensor_id_t id);

/* "HW" or "SIM". */
const char *sensors_backend_name(void);

/*
 * Start of one 1 ms tick. now_us = microseconds since boot, taken by the
 * caller at the tick. The SIM backend advances its script here; the IMU data
 * returned in this tick belongs to exactly this now_us (spec P9).
 */
void sensors_tick(uint32_t now_us);

/* Raw (not debounced) button levels: bit0 left, bit1 right, bit2 middle; 1 = pressed. */
uint8_t sensors_buttons_raw(void);

/* Raw 12-bit joystick ADC reading, 0..4095. Returns the centre value while the
 * status is not SENSOR_OK, so a missing joystick never scrolls. */
uint16_t sensors_joystick_raw(void);

/* The joystick centre v0 measured by sensors_init() (spec T7). */
uint16_t sensors_joystick_centre(void);

/* Optical counts since the previous call (HID convention: +x right, +y down). */
void sensors_optical_delta(int16_t *dx, int16_t *dy);

/* One line of boot facts for the bench, e.g. "opt_id=0x42 opt_srom=0x04 opt_cpi=1600". */
const char *sensors_boot_info(void);

/* Raw IMU counts: a = ax, ay, az; g = gx, gy, gz. */
void sensors_imu_raw(int16_t a[3], int16_t g[3]);

/*
 * SIM only: ask for the 30 s mouse script to (re)start at the next tick.
 * Does nothing on the HW backend.
 */
void sensors_sim_restart(void);

/*
 * SIM only: stop the mouse script at the next tick (within 1 ms). The mouse
 * channels go idle at once and every button reads released, so no button can
 * be left held. Does nothing on the HW backend.
 */
void sensors_sim_stop(void);

#endif /* SENSORS_H */
