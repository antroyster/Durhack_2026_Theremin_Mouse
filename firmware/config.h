/*
 * config.h - every pin, rate and tuning number of the firmware in one place.
 * Source of truth for the values: CLAUDE.md "Hardware" and docs/phase1-spec.md.
 * Values for later milestones (joystick, optical, IMU) are already here so the
 * wiring is fixed from the start.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "pico.h" /* PICO_DEFAULT_LED_PIN for the selected board */

/* ---- Firmware identity (spec B5) ---- */
#define CFG_FW_VERSION "0.1.0"
#ifndef CFG_GIT_HASH             /* normally set by CMakeLists.txt at configure time */
#define CFG_GIT_HASH "unknown"
#endif

/* ---- USB identity (spec U1, U2, U6) ---- */
#define CFG_USB_VID          0xCAFE /* TinyUSB example vendor ID: bench use only */
#define CFG_USB_PID          0x4005
#define CFG_USB_BCD_DEVICE   0x0100 /* device release 1.00 */
#define CFG_USB_MANUFACTURER "Ant"
#define CFG_USB_PRODUCT      "Theremin Mouse"
#define CFG_USB_MAX_POWER_MA 250    /* bus powered, 250 mA */

/* ---- Timing (spec T1, T2, T8, B1) ---- */
#define CFG_TICK_US         1000u    /* logic tick: 1 kHz repeating timer */
#define CFG_DEBOUNCE_US     5000u    /* a new button level must hold this long */
#define CFG_STATUS_PERIOD_US 1000000u /* status line + healthy LED toggle: 1 Hz */

/* ---- LED (spec B2, B3) ---- */
#define CFG_PIN_LED          PICO_DEFAULT_LED_PIN /* GPIO25 on a plain Pico */
#define CFG_LED_BLINK_ON_MS  100u  /* error code: LED on time of one quick blink */
#define CFG_LED_BLINK_MS     250u  /* error code: period of one quick blink */
#define CFG_LED_PAUSE_MS     1000u /* error code: pause after the N blinks */
#define CFG_LED_CODE_JOY     2u    /* blinks for a missing joystick */
#define CFG_LED_CODE_OPT     3u    /* blinks for a missing optical sensor */
#define CFG_LED_CODE_IMU     4u    /* blinks for a missing IMU */

/* ---- Buttons: internal pull-up, switch to GND, active low ---- */
#define CFG_PIN_BTN_LEFT   14
#define CFG_PIN_BTN_RIGHT  15
#define CFG_PIN_BTN_MIDDLE 22 /* joystick press */

/* ---- Joystick Y axis (milestone 2; spec T3-T7) ---- */
#define CFG_PIN_JOY_ADC        26   /* GPIO26 = ADC input 0 */
#define CFG_JOY_ADC_CHANNEL    0
#define CFG_JOY_ADC_MIN        0u   /* vmin */
#define CFG_JOY_ADC_MAX        4095u /* vmax (12-bit ADC) */
#define CFG_JOY_CENTRE_SAMPLES 64u  /* v0 = mean of this many samples at boot */
#define CFG_JOY_CENTRE_LO      1024u /* v0 outside LO..HI = joystick missing ... */
#define CFG_JOY_CENTRE_HI      3072u
#define CFG_JOY_CENTRE_DEFAULT 2048u /* ... and this centre is used instead */
#define CFG_JOY_FLOAT_DELTA    1000u /* reading moves this much between pull-up and pull-down = nothing connected */
#define CFG_JOY_PRINT_PERIOD_US 200000u /* "joy raw=..." debug line, 5 per second */
#define CFG_SCROLL_KS          20u  /* ks: detents per second at full tilt */
#define CFG_SCROLL_DEADZONE_PERMILLE 100 /* d = 0.1 */
#define CFG_SCROLL_INVERT      0    /* 0: higher ADC reading = scroll up; 1: flipped */

/* ---- Optical sensor PAW3360 on spi0, mode 3 (milestone 3; spec T11) ---- */
#define CFG_OPT_SPI_PORT   spi0
#define CFG_PIN_OPT_MISO   16
#define CFG_PIN_OPT_CS     17
#define CFG_PIN_OPT_SCK    18
#define CFG_PIN_OPT_MOSI   19
#define CFG_PIN_OPT_MOTION 20 /* active low (not used yet: the sensor is polled every tick) */
#define CFG_OPT_SPI_BAUD   1000000u /* 1 MHz; the PMW3360 limit is 2 MHz */
#define CFG_OPT_CPI        1600u /* counts per inch, 100..12000 in steps of 100 */
#define CFG_OPT_PRODUCT_ID 0x42u /* PMW3360 */
#define CFG_OPT_INVERT_X   0  /* 1: flip the sign of dx (HID: +x = right) */
#define CFG_OPT_INVERT_Y   0  /* 1: flip the sign of dy (HID: +y = down) */
#define CFG_OPT_SWAP_XY    0  /* 1: swap the two axes (sensor mounted rotated) */

/* ---- IMU MPU-6050 on i2c0 (milestone 4; spec T1, T12) ---- */
#define CFG_IMU_I2C_PORT        i2c0
#define CFG_PIN_IMU_SDA         8
#define CFG_PIN_IMU_SCL         9
#define CFG_IMU_I2C_HZ          400000u
#define CFG_IMU_I2C_ADDR        0x68
#define CFG_IMU_ACCEL_RANGE_G   2   /* +/-2 g   -> S_a = 16384 LSB/g */
#define CFG_IMU_GYRO_RANGE_DPS  250 /* +/-250 dps -> S_g = 131 LSB/(deg/s) */
#define CFG_IMU_RATE_HZ         200u
#define CFG_IMU_DLPF_HZ         44u /* digital low-pass filter bandwidth */
#define CFG_IMU_TICK_DIV        5u  /* IMU sampled on every 5th 1 kHz tick = 200 Hz */
#define CFG_IMU_PRINT_DIV       4u  /* text printout of every 4th sample = 50 lines per second */
#define CFG_IMU_QUEUE_SIZE      32u /* samples waiting between the tick and the main loop */

/* ---- Hardening (milestone 6) ---- */
#define CFG_WATCHDOG_MS         1000u /* main loop silent for this long = automatic reset */

#endif /* CONFIG_H */
