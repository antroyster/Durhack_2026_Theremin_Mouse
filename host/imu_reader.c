/*
 * imu_reader.c - see imu_reader.h.
 *
 *   Pico --USB CDC--> kernel tty (/dev/ttyACMn) --read()--> ring buffer --> parser --> sample
 *                          ^
 *                          poll() sleeps here until bytes arrive, the port
 *                          dies, or the timeout runs out
 *
 * Every syscall below has a comment: what it does, what it returns, how it fails.
 */
#include "imu_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* imu_read() only gets an fd, so the parser for each open port is kept here. */
typedef struct {
    int          in_use;
    int          fd;
    imu_parser_t parser;
    imu_log_fn   log_fn;
    void        *log_ctx;
} port_t;

static port_t g_ports[IMU_MAX_PORTS];

static port_t *find_port(int fd) {
    for (int i = 0; i < IMU_MAX_PORTS; i++) {
        if (g_ports[i].in_use && g_ports[i].fd == fd) return &g_ports[i];
    }
    return NULL;
}

/* Milliseconds on a clock that never jumps (not wall-clock time). */
static int64_t now_ms(void) {
    struct timespec ts;
    /*
     * clock_gettime(CLOCK_MONOTONIC): time since some fixed point (boot).
     * Returns 0, or -1 for an invalid clock id (cannot happen here).
     */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int imu_open(const char *path) {
    port_t *slot = NULL;
    for (int i = 0; i < IMU_MAX_PORTS && !slot; i++) {
        if (!g_ports[i].in_use) slot = &g_ports[i];
    }
    if (!slot) {
        errno = EMFILE; /* table full */
        return -1;
    }

    /*
     * open(): ask the kernel for a file descriptor (a small int) for the device.
     *   O_RDWR   - read and write (the bench sends its @RUN and @STOP commands on this port).
     *   O_NOCTTY - do NOT let this serial port become our controlling terminal;
     *              without it a Ctrl-C byte (0x03) or a hang-up on the port
     *              could send signals to this program.
     * Returns fd >= 0, or -1 with errno: ENOENT (not plugged in), EACCES (not
     * in the dialout group), EBUSY (another program holds it exclusively).
     */
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) return -1;

    /*
     * tcgetattr(): copy the port's current settings (struct termios) so that
     * only the flags we care about change. Returns 0, or -1 with errno ENOTTY
     * if the path is not a terminal (e.g. a plain file), EIO if it just died.
     */
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }

    /*
     * cfmakeraw(): edits the struct only (no syscall, cannot fail). It turns
     * off everything a terminal does to text, all of which would corrupt
     * binary frames: line buffering (ICANON), echo (ECHO), CR/LF translation
     * (ICRNL, OPOST), Ctrl-C/Ctrl-Z signals (ISIG), XON/XOFF flow control
     * (IXON) and 7-bit stripping (ISTRIP, CS8 is set).
     * The baud rate is left alone: USB CDC ignores it, and 1200 baud would
     * reboot the Pico into BOOTSEL.
     */
    cfmakeraw(&tio);
    tio.c_cflag |= (tcflag_t)(CLOCAL | CREAD); /* ignore modem lines, enable the receiver */
    /*
     * VMIN = 1, VTIME = 0: read() waits for at least 1 byte and never returns
     * 0 just because nothing has arrived. So read() == 0 can only mean "the
     * device is gone" (with VMIN = 0 it would also mean "no data yet").
     */
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    /*
     * tcsetattr(TCSANOW): apply the edited settings at once, without throwing
     * away bytes that already arrived (TCSAFLUSH would drop them). Returns 0
     * if ANY setting was applied, -1 with errno (EINVAL, EIO, ENOTTY) if none.
     */
    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }

    memset(slot, 0, sizeof *slot);
    slot->in_use = 1;
    slot->fd = fd;
    imu_parser_init(&slot->parser);
    return fd;
}

int imu_read(int fd, imu_sample_t *s, int timeout_ms) {
    port_t *pt = find_port(fd);
    if (!pt) {
        errno = EBADF;
        return -1;
    }
    int64_t deadline = now_ms() + timeout_ms;

    for (;;) {
        /* 1. Hand out whatever is already complete in the ring buffer. */
        imu_log_t log;
        imu_event_t ev;
        while ((ev = imu_parser_next(&pt->parser, s, &log)) != IMU_EV_NONE) {
            if (ev == IMU_EV_SAMPLE) return 1;
            if (pt->log_fn) pt->log_fn(pt->log_ctx, &log);
        }

        /* 2. Sleep until bytes arrive, the port dies or time runs out. */
        int wait_ms = -1;
        if (timeout_ms >= 0) {
            int64_t left = deadline - now_ms();
            wait_ms = left > 0 ? (int)left : 0;
        }
        struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
        /*
         * poll(): block until the fd is readable or wait_ms has passed.
         * Returns the number of fds with something to report (here 1),
         * 0 on timeout, or -1 with errno: EINTR (a signal such as Ctrl-C
         * arrived), EINVAL, ENOMEM.
         * revents says what happened: POLLIN = bytes waiting; POLLHUP = the
         * other end went away (USB unplugged, pty closed); POLLERR = device
         * error; POLLNVAL = fd is not open. HUP/ERR/NVAL are always reported,
         * whether asked for in .events or not.
         */
        int r = poll(&pfd, 1, wait_ms);
        if (r < 0) {
            if (errno == EINTR) return 0; /* let the caller check its stop flag */
            return -1;
        }
        if (r == 0) return 0; /* timeout: no sample this time */

        if (pfd.revents & POLLIN) {
            /* POLLIN is handled first so buffered bytes are not lost on a hang-up. */
            uint8_t tmp[1024];
            size_t want = imu_parser_free_space(&pt->parser);
            if (want > sizeof tmp) want = sizeof tmp;
            /*
             * read(): copy up to `want` waiting bytes into tmp. Returns how
             * many it copied (often fewer than asked; frames arrive in
             * pieces), 0 when a tty has hung up (unplugged), or -1 with errno:
             * EIO (device gone), EINTR (signal), EAGAIN (nothing after all).
             */
            ssize_t n = read(fd, tmp, want);
            if (n > 0) {
                imu_parser_feed(&pt->parser, tmp, (size_t)n);
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            if (n == 0) errno = EIO;
            return -1; /* unplugged */
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            errno = EIO;
            return -1; /* unplugged */
        }
    }
}

void imu_close(int fd) {
    port_t *pt = find_port(fd);
    if (pt) pt->in_use = 0;
    /*
     * close(): give the fd back to the kernel. Returns 0, or -1 with errno
     * (EBADF: not open; EIO). Either way the number must not be used again:
     * the next open() may hand out the same number for a different file.
     */
    if (fd >= 0) close(fd);
}

int imu_get_stats(int fd, imu_parser_stats_t *out) {
    port_t *pt = find_port(fd);
    if (!pt) return -1;
    *out = *imu_parser_stats(&pt->parser);
    return 0;
}

int imu_set_log_handler(int fd, imu_log_fn fn, void *ctx) {
    port_t *pt = find_port(fd);
    if (!pt) return -1;
    pt->log_fn = fn;
    pt->log_ctx = ctx;
    return 0;
}
