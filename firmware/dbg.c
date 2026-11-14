/* dbg.c - non-blocking debug text on the CDC port. See dbg.h. */
#include "imu_frame.h"
#include "pico/time.h"
#include "dbg.h"

#include <stdarg.h>
#include <stdio.h>

#include "tusb.h"

void dbg_printf(const char *fmt, ...) {
    /* "Connected" = the PC has set DTR, which Linux does when a program opens
     * the port. With no listener the text is dropped here, before any work. */
    if (!tud_cdc_connected()) return;

    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1; /* too long: cut to the buffer */

    if (buf[n - 1] == '\n') n--;     /* a log packet carries the text only, no newline (spec P4) */

    /* From milestone 5 everything on CDC is framed: the text goes out as a
     * type 0x02 log packet with its own sequence counter (spec P5). */
    static uint16_t seq;
    uint8_t frame[PROTO_LOG_FRAME_SIZE(sizeof buf)];
    size_t len = proto_pack_log(frame, sizeof frame, seq, time_us_32(), buf, (size_t)n);
    if (len == 0) return;

    /* All or nothing: a half-written packet would be a CRC error, and waiting
     * for space would block the main loop. */
    if (tud_cdc_write_available() < len) return;
    seq++;

    tud_cdc_write(frame, (uint32_t)len); /* copy into TinyUSB's TX FIFO */
    tud_cdc_write_flush();               /* start sending now instead of waiting for a full 64-byte packet */
}
