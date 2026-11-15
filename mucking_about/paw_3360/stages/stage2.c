// paw_3360.c  --  STAGE 2: reset the sensor and read delta X / delta Y
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
#define REG_MOTION          0x02
#define REG_DELTA_X_L       0x03
#define REG_DELTA_X_H       0x04
#define REG_DELTA_Y_L       0x05
#define REG_DELTA_Y_H       0x06
#define REG_SQUAL           0x07
#define REG_POWER_UP_RESET  0x3A
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

// ---- Power-up: put the sensor in a known state ------------------------
static void sensor_reset(void) {
    cs_deselect(); sleep_us(40);
    cs_select();   sleep_us(40);
    cs_deselect(); sleep_us(40);         // toggling NCS resyncs the serial port
    paw_write(REG_POWER_UP_RESET, 0x5A); // 0x5A = "reset now"
    sleep_ms(50);                        // tMOT-RST: sensor reboots
    paw_read(REG_MOTION);                // read 0x02..0x06 once and throw the
    paw_read(REG_DELTA_X_L);             // values away (stale data from before
    paw_read(REG_DELTA_X_H);             // the reset)
    paw_read(REG_DELTA_Y_L);
    paw_read(REG_DELTA_Y_H);
}

// ---- Read one (dx, dy) pair; returns the Motion register ---------------
static uint8_t read_delta(int16_t *dx, int16_t *dy) {
    paw_write(REG_MOTION, 0x01);         // any write to Motion + a read of it
    uint8_t motion = paw_read(REG_MOTION); // freezes the four delta registers
    uint8_t xl = paw_read(REG_DELTA_X_L);
    uint8_t xh = paw_read(REG_DELTA_X_H);
    uint8_t yl = paw_read(REG_DELTA_Y_L);
    uint8_t yh = paw_read(REG_DELTA_Y_H);
    *dx = (int16_t)((xh << 8) | xl);     // low byte first, signed 16-bit
    *dy = (int16_t)((yh << 8) | yl);
    return motion;
}

int main(void) {
    stdio_init_all();
    wait_for_terminal();
    spi_setup();

    printf("\n=== PAW3360 stage 2: delta X / delta Y ===\n");
    uint8_t id  = paw_read(REG_PRODUCT_ID);
    uint8_t inv = paw_read(REG_INVERSE_ID);
    printf("Product_ID = 0x%02X   Inverse_ID = 0x%02X\n", id, inv);
    if ((uint8_t)(id ^ inv) != 0xFF || id != 0x42) {
        printf("STOP: sensor not answering, or not a PMW3360 core (ID must be 0x42).\n");
        printf("      Go back to stage 1.\n");
        while (true) sleep_ms(1000);
    }

    sensor_reset();
    printf("Reset done. Slide the sensor over a surface.\n");
    printf("(Silence is normal while it is still. A status line prints every 2 s.)\n\n");

    int32_t total_x = 0, total_y = 0;
    uint32_t last_status_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        int16_t dx, dy;
        uint8_t motion = read_delta(&dx, &dy);

        if (motion & 0x80) {             // bit 7 = MOT: moved since last read
            total_x += dx;
            total_y += dy;
            printf("dx=%6d dy=%6d   total x=%8ld y=%8ld   (Motion=0x%02X)\n",
                   dx, dy, (long)total_x, (long)total_y, motion);
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_status_ms >= 2000) {
            last_status_ms = now;
            printf("[alive] Motion=0x%02X  SQUAL=%u\n", motion, paw_read(REG_SQUAL));
        }
        sleep_ms(10);                    // ~100 reads per second
    }
}
