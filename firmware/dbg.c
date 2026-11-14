/* dbg.c - non-blocking debug text on the CDC port. See dbg.h. */
#include "dbg.h"

#include <stdarg.h>
#include <stdio.h>

#include "tusb.h"

void dbg_printf(const char *fmt, ...) {
    /* "Connected" = the PC has set DTR, which Linux does when a program opens
     * the port. With no listener the text is dropped here, before any work. */
    if (!tud_cdc_connected()) return;

    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1; /* too long: cut to the buffer */

    /* All or nothing: a half-written line would confuse the reader, and
     * waiting for space would block the main loop. */
    if (tud_cdc_write_available() < (uint32_t)n) return;

    tud_cdc_write(buf, (uint32_t)n); /* copy into TinyUSB's TX FIFO */
    tud_cdc_write_flush();           /* start sending now instead of waiting for a full 64-byte packet */
}
