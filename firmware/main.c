/*
 * Milestone 2: composite USB device = HID mouse (buttons + scroll wheel) + CDC serial.
 *
 *   1 kHz timer interrupt                      main loop (never blocks)
 *   +--------------------------------+         +----------------------------------+
 *   | sensors_tick(now)              |         | tud_task()        USB stack      |
 *   | raw = sensors_buttons_raw()    |         | cdc_rx_task()     @RUN / @STOP   |
 *   | debounce x3 (5 ms)             |         | hid_task()        send on change |
 *   | g_buttons = debounced byte ----+-------->| status_task()     1 Hz line, LED |
 *   | joystick -> scroll accumulator |         |                                  |
 *   | g_wheel_pending += detents ----+-------->| hid_task() takes them (IRQs off) |
 *   +--------------------------------+         +----------------------------------+
 *
 * The interrupt only samples and debounces. Everything that talks to USB runs
 * in the main loop, because TinyUSB must not be called from an interrupt.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "tusb.h"

#include "cmd_match.h"
#include "config.h"
#include "dbg.h"
#include "debounce.h"
#include "mouse_report.h"
#include "scroll.h"
#include "sensors.h"

/* ---------------------------------------------------------------- tick ---- */

/* Debounced buttons (bit0 left, bit1 right, bit2 middle). Written by the timer
 * interrupt, read by the main loop. One byte is read or written in a single
 * instruction on the Cortex-M0+, so the main loop can never see a half-updated
 * value; "volatile" makes the compiler really re-read it every time. */
static volatile uint8_t g_buttons;

/* Debounce state: touched by the timer interrupt only. */
static debounce_t s_debounce[3];

/* Joystick scroll. The interrupt runs the accumulator and adds whole detents
 * to g_wheel_pending; the main loop takes them out. This one is a running
 * total that BOTH sides modify, so the main loop briefly disables interrupts
 * around its read-and-clear (a lost update would be a lost detent). */
static volatile int32_t  g_wheel_pending;
static volatile uint16_t g_joy_raw;   /* last ADC reading, for the debug line only */
static scroll_t s_scroll;             /* touched by the timer interrupt only */
static uint16_t s_joy_v0;             /* centre measured at boot */
static bool     s_joy_ok;             /* joystick present: scroll enabled */

static repeating_timer_t s_tick_timer;

/* False if the tick timer could not be started: nothing samples the buttons
 * then, so the status line reports them as missing. */
static bool s_tick_ok;

/* Runs in interrupt context every CFG_TICK_US. Must be short and never block. */
static bool tick_cb(repeating_timer_t *rt) {
    (void)rt;
    uint32_t now_us = time_us_32();

    sensors_tick(now_us);
    uint8_t raw = sensors_buttons_raw();

    uint8_t debounced = 0;
    for (unsigned i = 0; i < 3; i++) {
        bool raw_level = (raw >> i) & 1u;
        if (debounce_update(&s_debounce[i], raw_level, now_us, CFG_DEBOUNCE_US)) {
            debounced |= (uint8_t)(1u << i);
        }
    }
    g_buttons = debounced; /* one atomic byte store */

    if (s_joy_ok) {
        uint16_t v = sensors_joystick_raw();
        g_joy_raw = v;
        /* u = normalised deflection in Q15, 0 inside the deadzone (spec T3, T4). */
        int32_t u = scroll_normalise_q15(v, s_joy_v0, CFG_JOY_ADC_MIN, CFG_JOY_ADC_MAX,
                                         SCROLL_DEADZONE_Q15(CFG_SCROLL_DEADZONE_PERMILLE));
        if (CFG_SCROLL_INVERT) u = -u;
        /* a += ks * u * dt ; w = trunc(a) ; a -= w. The fraction stays in s_scroll. */
        int8_t w = scroll_update(&s_scroll, u, CFG_SCROLL_KS, CFG_TICK_US);
        if (w != 0) g_wheel_pending += w; /* safe here: the main loop cannot interrupt an interrupt */
    }

    return true; /* keep the timer repeating */
}

/* ----------------------------------------------------------------- HID ---- */

static mouse_report_state_t s_mouse;

static void hid_task(void) {
    /* Ready = configured by the PC, not suspended, and the previous report has
     * been collected. If it is busy the button state simply waits in g_buttons
     * for the next pass (spec T10). */
    if (!tud_hid_ready()) return;

    uint8_t buttons = g_buttons; /* one snapshot; the interrupt may change it right after */
    uint8_t report[MOUSE_REPORT_SIZE];
    uint8_t previous = s_mouse.last_buttons;

    /* Take every pending wheel detent, with interrupts off so the timer cannot
     * add one between the read and the clear. */
    uint32_t irq = save_and_disable_interrupts();
    int32_t pending = g_wheel_pending;
    g_wheel_pending = 0;
    restore_interrupts(irq);

    /* One report carries at most +/-127; anything more goes back for the next one. */
    int32_t wheel = pending;
    if (wheel > 127) wheel = 127;
    if (wheel < -127) wheel = -127;

    /* x and y are 0 until the optical sensor is built.
     * mouse_report_build() returns true only when something changed (spec T9). */
    if (mouse_report_build(&s_mouse, buttons, 0, 0, (int8_t)wheel, report)) {
        /* Report ID 0 = "no report ID": exactly the 4 bytes go on the wire. */
        if (tud_hid_report(0, report, MOUSE_REPORT_SIZE)) {
            pending -= wheel;                /* sent */
        } else {
            s_mouse.last_buttons = previous; /* not queued: try again on the next pass */
        }
    }

    /* Whatever was not sent is not lost (spec T10): put it back. */
    if (pending != 0) {
        irq = save_and_disable_interrupts();
        g_wheel_pending += pending;
        restore_interrupts(irq);
    }
}

/* GET_REPORT on the control endpoint: the PC asks for the current report
 * instead of waiting for the next one on the interrupt endpoint. HID 1.11
 * makes GET_REPORT(Input) mandatory, so it is answered with the current state:
 * the debounced buttons and no movement (movement is relative, so "now" = 0).
 * Returns the number of bytes written; 0 makes TinyUSB stall the request,
 * which is the right answer for report types a mouse does not have. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id;
    if (report_type != HID_REPORT_TYPE_INPUT) return 0;

    const uint8_t report[MOUSE_REPORT_SIZE] = { (uint8_t)(g_buttons & MOUSE_BTN_MASK), 0, 0, 0 };
    uint16_t n = (reqlen < MOUSE_REPORT_SIZE) ? reqlen : (uint16_t)MOUSE_REPORT_SIZE; /* never more than asked for */
    memcpy(buffer, report, n);
    return n;
}

/* SET_REPORT: data from the PC to the device (e.g. keyboard LEDs). A mouse has
 * no output reports, so there is nothing to do. */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

/* ----------------------------------------------------------------- CDC ---- */

/* The PC changed the serial settings. Opening the port at 1200 baud is the
 * agreed "reboot into the bootloader" signal (one cable rule):
 *     stty -F /dev/serial/by-id/<pico> 1200
 * reset_usb_boot(0, 0): no activity LED pin, both BOOTSEL interfaces enabled
 * (mass storage + picotool). It does not return. */
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding) {
    (void)itf;
    if (p_line_coding->bit_rate == 1200) {
        reset_usb_boot(0, 0);
    }
}

/* DTR changed: a program opened (dtr = 1) or closed (dtr = 0) the port. On
 * close, throw away any text still queued so the next listener does not start
 * with a stale half line. */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf; (void)rts;
    if (!dtr) tud_cdc_write_clear();
}

/* Commands from the PC, recognised by common/cmd_match.c (pure, unit-tested):
 *   "@RUN\n"   play one 30 s sim cycle from the start
 *   "@STOP\n"  stop the sim cycle now; every sim button reads released
 * Both do nothing on a HW build. Every other byte is ignored. The matcher
 * keeps its state between calls, so a command may be split over USB packets. */
static cmd_matcher_t s_cmd;

static void cdc_rx_task(void) {
    while (tud_cdc_available()) {
        int32_t c = tud_cdc_read_char();
        if (c < 0) break; /* nothing left after all */
        switch (cmd_matcher_feed(&s_cmd, (uint8_t)c)) {
            case CMD_RUN:  sensors_sim_restart(); break;
            case CMD_STOP: sensors_sim_stop();    break;
            default:       break;
        }
    }
}

/* ------------------------------------------------------ status and LED ---- */

static const char *status_str(sensor_status_t s) {
    switch (s) {
        case SENSOR_OK:      return "ok";
        case SENSOR_MISSING: return "missing";
        default:             return "off";
    }
}

/* Error code to blink (spec B3): 0 = healthy, else the lowest-numbered fault. */
static unsigned led_fault_code(void) {
    if (sensors_status(SENSOR_JOY) == SENSOR_MISSING) return CFG_LED_CODE_JOY;
    if (sensors_status(SENSOR_OPT) == SENSOR_MISSING) return CFG_LED_CODE_OPT;
    if (sensors_status(SENSOR_IMU) == SENSOR_MISSING) return CFG_LED_CODE_IMU;
    return 0;
}

/* N quick blinks, then a pause, repeating. Computed from the clock alone, so
 * there is no state and nothing waits:
 *   period = N * BLINK + PAUSE ;  p = now mod period
 *   LED on  <=>  p < N * BLINK  and  (p mod BLINK) < BLINK_ON             */
static bool led_error_pattern(unsigned n_blinks, uint32_t now_ms) {
    uint32_t blink_part = n_blinks * CFG_LED_BLINK_MS;
    uint32_t p = now_ms % (blink_part + CFG_LED_PAUSE_MS);
    return (p < blink_part) && ((p % CFG_LED_BLINK_MS) < CFG_LED_BLINK_ON_MS);
}

static void status_task(void) {
    static absolute_time_t next_due;   /* deadline of the next 1 Hz slot */
    static bool started = false;
    static bool led_on = false;
    static uint8_t last_printed = 0;   /* debounced buttons at the last "evt" line */

    if (!started) {
        next_due = make_timeout_time_us(CFG_STATUS_PERIOD_US);
        started = true;
    }

    unsigned fault = led_fault_code();

    if (time_reached(next_due)) {
        /* Advance from the previous deadline, not from "now": lateness never accumulates. */
        next_due = delayed_by_us(next_due, CFG_STATUS_PERIOD_US);

        if (fault == 0) {              /* healthy: toggle once a second (spec B2) */
            led_on = !led_on;
            gpio_put(CFG_PIN_LED, led_on);
        }

        /* Status line (spec B1). It repeats so a late listener still sees it. */
        dbg_printf("hello fw=%s git=%s sensors=%s btn=%s joy=%s opt=%s imu=%s\n",
                   CFG_FW_VERSION, CFG_GIT_HASH, sensors_backend_name(),
                   status_str(s_tick_ok ? sensors_status(SENSOR_BTN) : SENSOR_MISSING),
                   status_str(sensors_status(SENSOR_JOY)),
                   status_str(sensors_status(SENSOR_OPT)), status_str(sensors_status(SENSOR_IMU)));
    }

    if (fault != 0) {
        gpio_put(CFG_PIN_LED, led_error_pattern(fault, to_ms_since_boot(get_absolute_time())));
    }

    /* Joystick debug line, 5 per second: lets the bench see the raw ADC value. */
    static absolute_time_t joy_due;
    if (s_joy_ok && time_reached(joy_due)) {
        joy_due = make_timeout_time_us(CFG_JOY_PRINT_PERIOD_US);
        dbg_printf("joy raw=%u v0=%u\n", (unsigned)g_joy_raw, (unsigned)s_joy_v0);
    }

    uint8_t buttons = g_buttons;
    if (buttons != last_printed) {
        last_printed = buttons;
        dbg_printf("evt buttons=0x%02x\n", buttons);
    }
}

/* ---------------------------------------------------------------- main ---- */

int main(void) {
    gpio_init(CFG_PIN_LED);
    gpio_set_dir(CFG_PIN_LED, GPIO_OUT);

    sensors_init();
    for (unsigned i = 0; i < 3; i++) debounce_init(&s_debounce[i], false);
    mouse_report_init(&s_mouse);
    scroll_init(&s_scroll);
    s_joy_ok = (sensors_status(SENSOR_JOY) == SENSOR_OK);
    s_joy_v0 = sensors_joystick_centre();
    cmd_matcher_init(&s_cmd);

    tud_init(0); /* start TinyUSB as a device on port 0 */

    /* Negative period = "CFG_TICK_US between the STARTS of two callbacks", so
     * the tick rate does not depend on how long the callback takes. */
    s_tick_ok = add_repeating_timer_us(-(int64_t)CFG_TICK_US, tick_cb, NULL, &s_tick_timer);

    while (true) {
        tud_task();     /* USB stack: enumeration, control requests, transfer completions */
        cdc_rx_task();  /* commands from the PC: @RUN, @STOP */
        hid_task();     /* mouse report, only on a change */
        status_task();  /* 1 Hz status line, LED, button event text */
    }
}
