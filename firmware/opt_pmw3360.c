/*
 * opt_pmw3360.c - PMW3360 optical sensor over SPI0 (mode 3, MSB first).
 *
 * Ported from Ant's working experiment (mucking_about/paw_3360), which follows
 * the PMW3360 datasheet: register addresses, timings, SROM upload and burst read.
 *
 * A read:   NCS low, send address (bit 7 = 0), wait tSRAD, clock the byte in, NCS high.
 * A write:  NCS low, send address | 0x80, send the value, NCS high.
 * A burst:  NCS low, send 0x50, wait 35 us, clock 6 bytes in, NCS high:
 *           [0] Motion  [1] Observation  [2] dx low  [3] dx high  [4] dy low  [5] dy high
 */
#include "opt_pmw3360.h"

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"
#include "pmw3360_srom.h" /* 4094-byte sensor firmware, from SunjunKim/PMW3360 (LGPL-2.1) */

#define REG_PRODUCT_ID      0x00
#define REG_MOTION          0x02
#define REG_DELTA_X_L       0x03
#define REG_DELTA_X_H       0x04
#define REG_DELTA_Y_L       0x05
#define REG_DELTA_Y_H       0x06
#define REG_CONFIG1         0x0F /* resolution: CPI / 100 - 1 */
#define REG_CONFIG2         0x10 /* 0x00 = rest mode off (wired mouse) */
#define REG_SROM_ENABLE     0x13
#define REG_SROM_ID         0x2A
#define REG_POWER_UP_RESET  0x3A
#define REG_SHUTDOWN        0x3B
#define REG_INVERSE_ID      0x3F
#define REG_MOTION_BURST    0x50
#define REG_SROM_LOAD_BURST 0x62

static bool s_in_burst; /* the sensor has been put in burst mode */

static inline void cs_select(void)   { gpio_put(CFG_PIN_OPT_CS, 0); }
static inline void cs_deselect(void) { gpio_put(CFG_PIN_OPT_CS, 1); }

/* busy_wait_us (not sleep_us) everywhere: these also run inside the tick interrupt. */
static uint8_t reg_read(uint8_t reg) {
    uint8_t addr = reg & 0x7Fu, value = 0;
    cs_select();
    spi_write_blocking(CFG_OPT_SPI_PORT, &addr, 1);
    busy_wait_us(160);                 /* tSRAD: the sensor fetches the data */
    spi_read_blocking(CFG_OPT_SPI_PORT, 0x00, &value, 1);
    busy_wait_us(1);
    cs_deselect();
    busy_wait_us(20);                  /* tSRR: gap before the next transaction */
    return value;
}

static void reg_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { (uint8_t)(reg | 0x80u), value };
    cs_select();
    spi_write_blocking(CFG_OPT_SPI_PORT, buf, 2);
    busy_wait_us(20);
    cs_deselect();
    busy_wait_us(180);                 /* tSWW: gap after a write */
}

/* Upload the sensor's own firmware; it is lost at every power-up. Returns the SROM ID. */
static uint8_t load_srom(void) {
    reg_write(REG_CONFIG2, 0x00);
    reg_write(REG_SROM_ENABLE, 0x1D);  /* get ready for a download */
    sleep_ms(10);
    reg_write(REG_SROM_ENABLE, 0x18);  /* start the download */

    uint8_t cmd = REG_SROM_LOAD_BURST | 0x80u;
    cs_select();                       /* NCS stays low for the whole image */
    busy_wait_us(1);
    spi_write_blocking(CFG_OPT_SPI_PORT, &cmd, 1);
    busy_wait_us(15);
    for (int i = 0; i < SROM_LENGTH; i++) {
        spi_write_blocking(CFG_OPT_SPI_PORT, &srom_data[i], 1);
        busy_wait_us(15);              /* at least 15 us between bytes */
    }
    busy_wait_us(1);
    cs_deselect();
    busy_wait_us(200);
    return reg_read(REG_SROM_ID);      /* must be the first access afterwards */
}

bool pmw3360_init(pmw3360_info_t *info) {
    spi_init(CFG_OPT_SPI_PORT, CFG_OPT_SPI_BAUD);
    spi_set_format(CFG_OPT_SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); /* mode 3 */
    gpio_set_function(CFG_PIN_OPT_MISO, GPIO_FUNC_SPI);
    gpio_set_function(CFG_PIN_OPT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(CFG_PIN_OPT_MOSI, GPIO_FUNC_SPI);
    /* A weak pull-down on MISO: with no sensor connected every read is 0x00, never noise. */
    gpio_pull_down(CFG_PIN_OPT_MISO);
    /* NCS is a plain GPIO so it can stay low across address, wait and data. */
    gpio_init(CFG_PIN_OPT_CS);
    gpio_set_dir(CFG_PIN_OPT_CS, GPIO_OUT);
    cs_deselect();

    busy_wait_us(40); cs_select(); busy_wait_us(40); cs_deselect(); busy_wait_us(40); /* resync the port */

    info->product_id = reg_read(REG_PRODUCT_ID);
    info->inverse_id = reg_read(REG_INVERSE_ID);
    info->srom_id = 0;
    info->cpi_ok = false;
    s_in_burst = false;
    if (info->product_id != CFG_OPT_PRODUCT_ID || (uint8_t)(info->product_id ^ info->inverse_id) != 0xFFu) {
        return false;                  /* nothing there, or not a PMW3360: do not go further */
    }

    /* The Pico may have been reflashed while the sensor stayed powered: shut down, then reset. */
    reg_write(REG_SHUTDOWN, 0xB6);
    sleep_ms(300);
    cs_select(); busy_wait_us(40); cs_deselect(); busy_wait_us(40);
    reg_write(REG_POWER_UP_RESET, 0x5A);
    sleep_ms(50);                      /* tMOT-RST: the sensor reboots */
    (void)reg_read(REG_MOTION);        /* read 0x02..0x06 once: stale data from before the reset */
    (void)reg_read(REG_DELTA_X_L);
    (void)reg_read(REG_DELTA_X_H);
    (void)reg_read(REG_DELTA_Y_L);
    (void)reg_read(REG_DELTA_Y_H);

    info->srom_id = load_srom();
    reg_write(REG_CONFIG2, 0x00);      /* rest mode off */

    uint8_t cpi_reg = (uint8_t)(CFG_OPT_CPI / 100u - 1u);
    reg_write(REG_CONFIG1, cpi_reg);
    info->cpi_ok = (reg_read(REG_CONFIG1) == cpi_reg);
    return true;
}

void pmw3360_read(int16_t *dx, int16_t *dy) {
    uint8_t buf[6];
    *dx = 0;
    *dy = 0;
    if (!s_in_burst) {
        reg_write(REG_MOTION_BURST, 0x00); /* any write here enters burst mode */
        s_in_burst = true;
    }
    uint8_t cmd = REG_MOTION_BURST;
    cs_select();
    spi_write_blocking(CFG_OPT_SPI_PORT, &cmd, 1);
    busy_wait_us(35);                  /* tSRAD_MOTBR */
    spi_read_blocking(CFG_OPT_SPI_PORT, 0x00, buf, sizeof buf);
    busy_wait_us(1);
    cs_deselect();                     /* raising NCS ends the burst; the deltas are cleared by the read */

    /* Motion bits 2..0 read 0 in burst mode. If not, the sensor fell out of it:
     * drop this sample and re-enter next time. */
    if (buf[0] & 0x07u) { s_in_burst = false; return; }
    if (buf[0] & 0x80u) {              /* MOT: there was movement since the last read */
        *dx = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
        *dy = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
    }
}
