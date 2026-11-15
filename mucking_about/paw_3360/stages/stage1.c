// paw_3360.c  --  STAGE 1: talk to the sensor and read its ID
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ---- Pins (SPI0) -------------------------------------------------------
#define SPI_PORT  spi0
#define PIN_MISO  16
#define PIN_CS    17
#define PIN_SCK   18
#define PIN_MOSI  19
#define SPI_BAUD  500000      // 500 kHz. The sensor's maximum is 2 MHz.

// ---- Registers ---------------------------------------------------------
#define REG_PRODUCT_ID      0x00
#define REG_REVISION_ID     0x01
#define REG_INVERSE_ID      0x3F

// ---- Chip select (active low) -----------------------------------------
static inline void cs_select(void)   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(PIN_CS, 1); }

static void spi_setup(void) {
    spi_init(SPI_PORT, SPI_BAUD);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); // mode 3
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();
}

// ---- Register access ---------------------------------------------------
static uint8_t paw_read(uint8_t reg) {
    uint8_t addr = reg & 0x7F;           // bit 7 = 0 means READ
    uint8_t value = 0;
    cs_select();
    sleep_us(1);                         // tNCS-SCLK: NCS low before first clock
    spi_write_blocking(SPI_PORT, &addr, 1);
    sleep_us(160);                       // tSRAD: sensor fetches the data
    spi_read_blocking(SPI_PORT, 0x00, &value, 1);
    sleep_us(1);                         // tSCLK-NCS (read)
    cs_deselect();
    sleep_us(20);                        // tSRR: gap before the next command
    return value;
}

static void paw_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };   // bit 7 = 1 means WRITE
    cs_select();
    sleep_us(1);
    spi_write_blocking(SPI_PORT, buf, 2);
    sleep_us(35);                        // tSCLK-NCS (write)
    cs_deselect();
    sleep_us(180);                       // tSWW / tSWR: gap before the next command
}

// ---- Wait for the USB serial terminal so no output is lost -------------
static void wait_for_terminal(void) {
    for (int i = 0; i < 150 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(500);
}

int main(void) {
    stdio_init_all();
    wait_for_terminal();
    spi_setup();

    printf("\n=== PAW3360 stage 1: ID check ===\n");
    uint8_t id  = paw_read(REG_PRODUCT_ID);
    uint8_t inv = paw_read(REG_INVERSE_ID);
    uint8_t rev = paw_read(REG_REVISION_ID);
    printf("Product_ID = 0x%02X   Inverse_ID = 0x%02X   Revision_ID = 0x%02X\n", id, inv, rev);

    if ((uint8_t)(id ^ inv) != 0xFF) {
        printf("FAIL: ID and inverse do not XOR to 0xFF. The sensor is not answering.\n");
        printf("      Go to the stage 1 troubleshooting table.\n");
    } else if (id == 0x42) {
        printf("PASS: PMW3360 core found (ID 0x42). Go on to stage 2.\n");
    } else {
        printf("The sensor answers but its ID is 0x%02X, not 0x42. Stop and read\n", id);
        printf("the 'unexpected ID' note in the guide before going on.\n");
    }

    while (true) sleep_ms(1000);
}
