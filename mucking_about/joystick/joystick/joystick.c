#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

#define SW_PIN 22
#define VY_ADC 0  // GP26 = ADC0

int main() {
    stdio_init_all();

    // Initialise switch
    gpio_init(SW_PIN);
    gpio_set_dir(SW_PIN, GPIO_IN);
    gpio_pull_up(SW_PIN);

    // Initialise ADC
    adc_init();
    adc_gpio_init(26);
    adc_select_input(VY_ADC);

    while (true) {

        // Read potentiometer (12-bit ADC: 0-4095)
        uint16_t vy_value = adc_read();

        // Read joystick switch (0 = pressed, 1 = released)
        bool sw_value = gpio_get(SW_PIN);

        printf("VY: %u | SW: %s\n",
               vy_value,
               sw_value ? "Released" : "Pressed");

        sleep_ms(100);
    }

    return 0;
}