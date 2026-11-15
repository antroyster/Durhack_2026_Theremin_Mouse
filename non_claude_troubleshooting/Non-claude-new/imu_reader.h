/*
 * imu_reader.h - the PC side of the Pico's CDC port: open it in raw mode,
 * wait for bytes with poll(), run them through imu_parser, hand out samples.
 * POSIX + libc only. API from docs/phase1-spec.md, section "Reader".
 */
#ifndef IMU_READER_H
#define IMU_READER_H

#include "/home/anthony-royster/Durhack_2026_Theremin_Mouse/Non-claude-new/imu_parser.h" /* imu_sample_t, imu_log_t, imu_parser_stats_t */

/* How many ports can be open at once (per-fd parser state lives in a static table). */
#define IMU_MAX_PORTS 4

/* Open the port and put it in raw mode. Returns the fd, or -1 (errno says why). */
int imu_open(const char *path);

/*
 * Wait up to timeout_ms for one IMU sample (timeout_ms < 0 = wait for ever).
 * Returns 1 = *s filled in, 0 = timeout (or interrupted by a signal),
 * -1 = error or unplug (read returned 0, EIO, POLLHUP/POLLERR): close and reopen.
 */
int imu_read(int fd, imu_sample_t *s, int timeout_ms);

/* Close the port and free its parser state. */
void imu_close(int fd);

/* ---- extras, not in the spec ---- */

/* Copy this port's counters (they start at zero on every imu_open). 0 ok, -1 bad fd. */
int imu_get_stats(int fd, imu_parser_stats_t *out);

/* Called from inside imu_read for every log packet (type 0x02). 0 ok, -1 bad fd. */
typedef void (*imu_log_fn)(void *ctx, const imu_log_t *log);
int imu_set_log_handler(int fd, imu_log_fn fn, void *ctx);

#endif
