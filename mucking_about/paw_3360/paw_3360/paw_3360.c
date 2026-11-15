

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define SPI_BAUD 1000000

// gpio_put(pin, value)
//   pin   - GPIO number to drive
//   value - 0 = drive low, 1 = drive high
// NCS is active-low: low means "sensor, I'm talking to you", high means "ignore the bus".
static inline void cs_select(void)   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(PIN_CS, 1); }

static void spi_setup(void) {
    // spi_init(spi, baudrate)
    //   spi      - which SPI block to use (spi0 or spi1). Must match the pins chosen:
    //              GPIO 16-19 are the spi0 pins on the Pico.
    //   baudrate - requested SCK frequency in Hz. 1 MHz is well inside what the
    //              PAW3360 allows, and slow enough to be forgiving of jumper wires.
    // This also takes the SPI block out of reset. Without it every SPI call hangs.
    spi_init(SPI_PORT, SPI_BAUD);

    // spi_set_format(spi, data_bits, cpol, cpha, order)
    //   spi       - the SPI block being configured
    //   data_bits - bits per transfer; 8 because the sensor works in bytes
    //   cpol      - clock polarity: SPI_CPOL_1 = SCK idles HIGH
    //   cpha      - clock phase:    SPI_CPHA_1 = data sampled on the rising (2nd) edge
    //               CPOL=1 + CPHA=1 is "SPI mode 3", which the PAW3360 requires.
    //   order     - SPI_MSB_FIRST = most significant bit sent first, as the sensor expects
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    // gpio_set_function(pin, function)
    //   pin      - GPIO number to reassign
    //   function - which peripheral owns the pin; GPIO_FUNC_SPI hands it to the SPI block
    //              so the hardware drives the clock/data lines instead of plain GPIO.
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);   // sensor -> Pico data
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);   // clock from Pico
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);   // Pico -> sensor data

    // Chip select is NOT given to the SPI block. We drive it by hand because the
    // sensor needs NCS held low across a whole multi-byte transaction plus delays,
    // which the hardware CS line wouldn't do.
    gpio_init(PIN_CS);                 // gpio_init(pin): set pin up as a plain GPIO
    gpio_set_dir(PIN_CS, GPIO_OUT);    // gpio_set_dir(pin, out): GPIO_OUT = we drive it, GPIO_IN = we read it
    cs_deselect();                     // start idle (high) so the sensor ignores the bus
}

// reg - register address to read (0x00-0x7F). Returns the byte stored there.
static uint8_t paw_read(uint8_t reg) {
    uint8_t addr = reg & 0x7F;        // bit 7 = 0 -> read (mask makes sure it's clear)
    uint8_t value = 0;
    cs_select();

    // spi_write_blocking(spi, src, len)
    //   spi - SPI block to use
    //   src - pointer to the bytes to send (here, the address byte)
    //   len - how many bytes to send (1)
    // "Blocking" = doesn't return until all bytes have been clocked out.
    spi_write_blocking(SPI_PORT, &addr, 1);
    sleep_us(160);                    // wait for the sensor to fetch the data (tSRAD)

    // spi_read_blocking(spi, repeated_tx_data, dst, len)
    //   spi              - SPI block to use
    //   repeated_tx_data - dummy byte sent on MOSI while we clock in the reply; SPI only
    //                      receives while it transmits, so we must send *something* (0x00)
    //   dst              - where the received bytes are stored (&value)
    //   len              - how many bytes to read (1)
    spi_read_blocking(SPI_PORT, 0x00, &value, 1);
    sleep_us(1);                      // short hold before releasing NCS
    cs_deselect();
    sleep_us(20);                     // gap before the next transaction (tSRR)
    return value;
}

// reg   - register address to write (0x00-0x7F)
// value - byte to store in that register
static void paw_write(uint8_t reg, uint8_t value) {
    // A write is a 2-byte transfer: address with bit 7 set, then the data.
    uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };   // bit 7 = 1 -> write
    cs_select();
    spi_write_blocking(SPI_PORT, buf, 2);   // buf = both bytes, 2 = number of bytes to send
    sleep_us(20);                     // let the sensor finish latching the byte
    cs_deselect();
    sleep_us(180);                    // gap after a write (tSWW)
}

int main() {
    stdio_init_all();
    for (int i =0; i<10 ;i++)
    {
        sleep_ms(1000);               // sleep_ms(ms): pause for that many milliseconds
        printf("Setup Serial Monitor\n");
    }

    spi_setup();                      // FIX: must run before any paw_read/paw_write, otherwise
                                      // spi0 is still in reset and the first SPI call hangs forever

    uint8_t id  = paw_read(0x00);   // Product_ID
    uint8_t inv = paw_read(0x3F);   // Inverse_Product_ID
    printf("Product_ID = 0x%02X, Inverse = 0x%02X\n", id, inv);
    // The inverse register is the bitwise NOT of the ID, so XOR-ing them gives 0xFF
    // if the SPI link is working (and not just reading all 0s or all 1s).
    if ((uint8_t)(id ^ inv) == 0xFF) printf("SPI link looks good.\n");
    while (true) {
        printf("Hello from the Pico!\n");
        sleep_ms(1000);
    }
    return 0;
}


