/* opt_pmw3360.h - driver for the optical sensor (a PMW3360 core, sold as "PAW3360"). */
#ifndef OPT_PMW3360_H
#define OPT_PMW3360_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t product_id;  /* register 0x00, 0x42 for a PMW3360 */
    uint8_t inverse_id;  /* register 0x3F, the bitwise NOT of the product ID */
    uint8_t srom_id;     /* register 0x2A after the firmware upload, 0x04 expected */
    bool    cpi_ok;      /* the CPI setting read back correctly */
} pmw3360_info_t;

/* Boot time only (blocks for about 0.5 s). Returns true if the ID registers are right. */
bool pmw3360_init(pmw3360_info_t *info);

/* Counts since the previous call, raw sensor axes. About 100 us; safe in the tick interrupt. */
void pmw3360_read(int16_t *dx, int16_t *dy);

#endif
