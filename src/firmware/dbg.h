/*
 * dbg.h - debug text over the CDC serial port (spec U7).
 *
 * Milestones 1-4: plain text lines. From milestone 5 the body of dbg_printf
 * changes to send type 0x02 log packets; callers stay the same.
 */
#ifndef DBG_H
#define DBG_H

/*
 * printf-style, never blocks. The text is sent only if a program has the
 * port open AND the whole text fits in the CDC transmit buffer; otherwise it
 * is dropped at once. Longest text: 127 characters.
 *
 * Call from the main loop only (TinyUSB is not interrupt-safe).
 */
void dbg_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* DBG_H */
