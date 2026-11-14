// Milestone 0: blink the onboard LED and print "hello" over USB serial once a second.
//
// USB is handled entirely by the SDK's stdio_usb in this milestone: it provides the
// CDC descriptors, services TinyUSB (tud_task) from a background timer interrupt, and
// reboots into BOOTSEL when the host opens the port at 1200 baud.

#include <stdio.h>

#include "pico/stdlib.h"

#define LED_PIN        PICO_DEFAULT_LED_PIN  // GPIO25 on a plain Pico
#define TICK_PERIOD_US 1000000               // 1 s between ticks

int main(void) {
    stdio_init_all();  // starts USB CDC stdio (UART stdio is disabled in CMakeLists.txt)

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    bool led_on = false;

    // Absolute time of the next tick. The first tick is one period after boot.
    absolute_time_t next_due = make_timeout_time_us(TICK_PERIOD_US);

    while (true) {
        // Non-blocking: just compare "now" with the deadline and fall through if not due.
        if (time_reached(next_due)) {
            // Advance from the previous deadline, not from "now", so lateness never accumulates.
            next_due = delayed_by_us(next_due, TICK_PERIOD_US);

            led_on = !led_on;
            gpio_put(LED_PIN, led_on);

            printf("hello\n");
        }

        // Later milestones add tud_task() and queued-data sends here.
    }
}
