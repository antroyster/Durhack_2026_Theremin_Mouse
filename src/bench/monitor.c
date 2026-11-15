/*
 * monitor - live terminal view of the Theremin Mouse, for poking at the
 * hardware by hand. Ctrl-C quits.
 *
 *   monitor [--port PATH] [--event PATH] [--duration SECONDS]
 *
 * Two inputs, both watched with one poll():
 *   CDC port  (/dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00): framed IMU +
 *             log packets (milestone 5 on) or plain text lines (up to 4).
 *             Bytes the parser throws away are shown as text when they form
 *             printable lines.
 *   evdev node (/dev/input/by-id/usb-Ant_Theremin_Mouse_*-event-mouse): what
 *             the kernel's mouse driver decoded from the HID reports. It is
 *             only read, never grabbed, so the desktop still gets the events.
 */
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "imu_reader.h"
#include "scaling.h"

#define PORT_GLOB  "/dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00"
#define EVENT_GLOB "/dev/input/by-id/usb-Ant_Theremin_Mouse_*-event-mouse"
#define N_LINES    6
#define LINE_MAX_  100
#define DRAW_MS    100
#define RETRY_MS   1000

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* The last few text lines, plus the line being built from discarded bytes. */
typedef struct {
    char   lines[N_LINES][LINE_MAX_];
    int    count;
    char   cur[LINE_MAX_];
    size_t cur_len;
    int    binary; /* the current line contained a non-printable byte */
} textlog_t;

static void push_line(textlog_t *t, const char *prefix, const char *s) {
    if (t->count == N_LINES) memmove(t->lines[0], t->lines[1], (N_LINES - 1) * LINE_MAX_);
    else t->count++;
    snprintf(t->lines[t->count - 1], LINE_MAX_, "%s%s", prefix, s);
}

/* Parser hook: every byte that was not part of a valid frame arrives here. */
static void on_discard(void *ctx, uint8_t b) {
    textlog_t *t = ctx;
    if (b == '\n') {
        t->cur[t->cur_len] = '\0';
        if (t->cur_len > 0 && !t->binary) push_line(t, "", t->cur);
        t->cur_len = 0;
        t->binary = 0;
    } else if (b == '\r') {
        /* ignore */
    } else if (b >= 0x20 && b < 0x7F) {
        if (t->cur_len < LINE_MAX_ - 1) t->cur[t->cur_len++] = (char)b;
    } else {
        t->binary = 1; /* noise or a broken frame: do not show this line */
    }
}

/* First path matching a shell pattern, copied into out. Returns 1 if found. */
static int find_first(const char *pattern, char *out, size_t cap) {
    glob_t g;
    int found = 0;
    if (glob(pattern, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
        snprintf(out, cap, "%s", g.gl_pathv[0]);
        found = 1;
    }
    globfree(&g);
    return found;
}

int main(int argc, char **argv) {
    const char *port_arg = NULL, *event_arg = NULL;
    double duration = 0.0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) port_arg = argv[++i];
        else if (!strcmp(argv[i], "--event") && i + 1 < argc) event_arg = argv[++i];
        else if (!strcmp(argv[i], "--duration") && i + 1 < argc) duration = atof(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--port PATH] [--event PATH] [--duration SECONDS]\n", argv[0]);
            return 2;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    static imu_parser_t parser; /* static: it holds a 4 kB buffer */
    static textlog_t text;
    imu_parser_init(&parser);
    imu_parser_set_discard(&parser, on_discard, &text);

    char port_path[512] = "", event_path[512] = "", event_hint[640] = "";
    int port_fd = -1, ev_fd = -1;
    int64_t t_start = now_ms(), next_try = 0, next_draw = 0, rate_t0 = t_start;
    uint64_t rate_n0 = 0;
    double rate = 0.0;

    int btn[3] = { 0, 0, 0 };                 /* L, R, M: 1 = pressed */
    long tot_dx = 0, tot_dy = 0, tot_wheel = 0;
    int last_dx = 0, last_dy = 0, last_wheel = 0; /* values in the latest report */
    int acc_dx = 0, acc_dy = 0, acc_wheel = 0;    /* report being received */
    unsigned long reports = 0;
    imu_sample_t s;
    int have_sample = 0;
    memset(&s, 0, sizeof s);
    const float sa_lsb = scale_accel_lsb_per_g(2), sg_lsb = scale_gyro_lsb_per_dps(250);

    printf("\x1b[2J\x1b[?25l"); /* clear the screen, hide the cursor */

    while (!g_stop) {
        int64_t now = now_ms();
        if (duration > 0.0 && (double)(now - t_start) >= duration * 1000.0) break;

        /* (Re)open whatever is missing, once a second: this also covers unplug/replug. */
        if (now >= next_try) {
            next_try = now + RETRY_MS;
            if (port_fd < 0) {
                if (port_arg) snprintf(port_path, sizeof port_path, "%s", port_arg);
                if (port_arg || find_first(PORT_GLOB, port_path, sizeof port_path)) {
                    port_fd = imu_open(port_path); /* raw mode, as in the reader */
                }
            }
            if (ev_fd < 0) {
                if (event_arg) snprintf(event_path, sizeof event_path, "%s", event_arg);
                if (event_arg || find_first(EVENT_GLOB, event_path, sizeof event_path)) {
                    /* O_NONBLOCK: read() returns EAGAIN instead of waiting. No EVIOCGRAB. */
                    ev_fd = open(event_path, O_RDONLY | O_NONBLOCK);
                    if (ev_fd < 0) {
                        snprintf(event_hint, sizeof event_hint,
                                 "cannot open %s (%s): read permission is needed - run with sudo "
                                 "or join the 'input' group", event_path, strerror(errno));
                    }
                } else {
                    snprintf(event_hint, sizeof event_hint, "not found (%s)", EVENT_GLOB);
                }
            }
        }

        /* Sleep until either fd has data or the next redraw is due. A negative fd is skipped. */
        struct pollfd pf[2] = { { .fd = port_fd, .events = POLLIN }, { .fd = ev_fd, .events = POLLIN } };
        int wait = (int)(next_draw - now);
        poll(pf, 2, wait > 0 ? wait : 0);

        if (port_fd >= 0 && pf[0].revents) {
            uint8_t buf[1024];
            ssize_t n = (pf[0].revents & POLLIN) ? read(port_fd, buf, sizeof buf) : 0;
            if (n > 0) {
                imu_log_t log;
                imu_event_t ev;
                for (size_t off = 0; off < (size_t)n;) {
                    off += imu_parser_feed(&parser, buf + off, (size_t)n - off);
                    while ((ev = imu_parser_next(&parser, &s, &log)) != IMU_EV_NONE) {
                        if (ev == IMU_EV_SAMPLE) have_sample = 1;
                        else push_line(&text, "LOG: ", log.text);
                    }
                }
            } else if (!(n < 0 && (errno == EINTR || errno == EAGAIN))) {
                imu_close(port_fd); /* unplugged: read 0, EIO or POLLHUP */
                port_fd = -1;
            }
        }

        if (ev_fd >= 0 && pf[1].revents) {
            struct input_event e[32];
            ssize_t n = (pf[1].revents & POLLIN) ? read(ev_fd, e, sizeof e) : 0;
            if (n > 0) {
                for (size_t i = 0; i < (size_t)n / sizeof e[0]; i++) {
                    if (e[i].type == EV_KEY && e[i].code == BTN_LEFT) btn[0] = e[i].value != 0;
                    else if (e[i].type == EV_KEY && e[i].code == BTN_RIGHT) btn[1] = e[i].value != 0;
                    else if (e[i].type == EV_KEY && e[i].code == BTN_MIDDLE) btn[2] = e[i].value != 0;
                    else if (e[i].type == EV_REL && e[i].code == REL_X) { acc_dx += e[i].value; tot_dx += e[i].value; }
                    else if (e[i].type == EV_REL && e[i].code == REL_Y) { acc_dy += e[i].value; tot_dy += e[i].value; }
                    else if (e[i].type == EV_REL && e[i].code == REL_WHEEL) { acc_wheel += e[i].value; tot_wheel += e[i].value; }
                    else if (e[i].type == EV_SYN && e[i].code == SYN_REPORT) { /* end of one HID report */
                        last_dx = acc_dx; last_dy = acc_dy; last_wheel = acc_wheel;
                        acc_dx = acc_dy = acc_wheel = 0;
                        reports++;
                    }
                }
            } else if (!(n < 0 && (errno == EINTR || errno == EAGAIN))) {
                close(ev_fd);
                ev_fd = -1;
                snprintf(event_hint, sizeof event_hint, "lost %s", event_path);
            }
        }

        now = now_ms();
        if (now < next_draw) continue;
        next_draw = now + DRAW_MS;

        const imu_parser_stats_t *st = imu_parser_stats(&parser);
        if (now - rate_t0 >= 1000) { /* frame rate over the last second */
            rate = (double)(st->imu_frames - rate_n0) * 1000.0 / (double)(now - rate_t0);
            rate_n0 = st->imu_frames;
            rate_t0 = now;
        }

        /* \x1b[H = cursor home, \x1b[K = clear to end of line, \x1b[J = clear below. */
        printf("\x1b[HTheremin Mouse monitor   (Ctrl-C to quit)\x1b[K\n\x1b[K\n");
        printf("CDC port : %s\x1b[K\n", port_fd >= 0 ? port_path : "not open (" PORT_GLOB ")");
        printf("Mouse    : %s\x1b[K\n\x1b[K\n", ev_fd >= 0 ? event_path : event_hint);
        printf("Buttons  : L[%c]  R[%c]  M[%c]        HID reports %lu\x1b[K\n",
               btn[0] ? '#' : ' ', btn[1] ? '#' : ' ', btn[2] ? '#' : ' ', reports);
        printf("Wheel    : last %+5d          total %+8ld\x1b[K\n", last_wheel, tot_wheel);
        printf("dx / dy  : last %+5d %+5d    total %+8ld %+8ld\x1b[K\n\x1b[K\n",
               last_dx, last_dy, tot_dx, tot_dy);
        if (have_sample) {
            printf("IMU raw  : a = %+6d %+6d %+6d    g = %+6d %+6d %+6d    seq %5u  t_us %" PRIu32 "\x1b[K\n",
                   s.a[0], s.a[1], s.a[2], s.g[0], s.g[1], s.g[2], s.seq, s.t_us);
            printf("IMU      : a = %+6.3f %+6.3f %+6.3f g  w = %+8.2f %+8.2f %+8.2f deg/s\x1b[K\n",
                   (double)scale_accel_g(s.a[0], sa_lsb), (double)scale_accel_g(s.a[1], sa_lsb),
                   (double)scale_accel_g(s.a[2], sa_lsb), (double)scale_gyro_dps(s.g[0], sg_lsb),
                   (double)scale_gyro_dps(s.g[1], sg_lsb), (double)scale_gyro_dps(s.g[2], sg_lsb));
        } else {
            printf("IMU raw  : no frames yet\x1b[K\nIMU      : -\x1b[K\n");
        }
        printf("Stream   : frames %" PRIu64 "  rate %.1f Hz  crc errors %" PRIu64 "  gaps %" PRIu64
               "  lost %" PRIu64 "  logs %" PRIu64 "  discarded %" PRIu64 "\x1b[K\n\x1b[K\n",
               st->imu_frames, rate, st->crc_errors, st->seq_gaps, st->frames_lost,
               st->log_frames, st->bytes_discarded);
        printf("Text / log lines:\x1b[K\n");
        for (int i = 0; i < text.count; i++) printf("  %s\x1b[K\n", text.lines[i]);
        printf("\x1b[J");
        fflush(stdout);
    }

    if (port_fd >= 0) imu_close(port_fd);
    if (ev_fd >= 0) close(ev_fd);
    printf("\x1b[?25h\n"); /* show the cursor again */
    return 0;
}
