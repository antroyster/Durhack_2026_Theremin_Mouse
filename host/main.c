/*
 * imu_reader - reads the Theremin Mouse IMU stream and reports its health.
 *
 *   imu_reader <port> [--duration SECONDS] [--csv FILE] [--quiet]
 *
 * Prints a summary line every second and each log packet as "LOG: <text>".
 * On unplug it closes the port and retries every 100 ms. On exit (duration
 * over or Ctrl-C) it prints one TOTAL line that the bench scripts parse.
 * --quiet leaves only the TOTAL line on stdout.
 */
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "imu_reader.h"

#define RETRY_MS 100

static volatile sig_atomic_t g_stop; /* set by the signal handler, read by the loop */

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void sleep_ms(long ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    /* nanosleep(): sleep for ts. Returns 0, or -1/EINTR if a signal cut it short. */
    nanosleep(&ts, NULL);
}

static void on_log(void *ctx, const imu_log_t *log) {
    const int *quiet = ctx;
    if (!*quiet) printf("LOG: %s\n", log->text);
}

/* a += b, field by field. */
static void stats_add(imu_parser_stats_t *a, const imu_parser_stats_t *b) {
    a->imu_frames += b->imu_frames;
    a->log_frames += b->log_frames;
    a->crc_errors += b->crc_errors;
    a->seq_gaps += b->seq_gaps;
    a->frames_lost += b->frames_lost;
    a->bytes_discarded += b->bytes_discarded;
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s <port> [--duration SECONDS] [--csv FILE] [--quiet]\n", argv0);
}

int main(int argc, char **argv) {
    const char *port = NULL, *csv_path = NULL;
    double duration = 0.0; /* 0 = run until Ctrl-C */
    int quiet = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--duration") && i + 1 < argc) duration = atof(argv[++i]);
        else if (!strcmp(argv[i], "--csv") && i + 1 < argc) csv_path = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (argv[i][0] != '-' && !port) port = argv[i];
        else { usage(argv[0]); return 2; }
    }
    if (!port) { usage(argv[0]); return 2; }

    FILE *csv = NULL;
    if (csv_path) {
        csv = fopen(csv_path, "w");
        if (!csv) { perror(csv_path); return 1; }
        fprintf(csv, "seq,t_us,ax,ay,az,gx,gy,gz\n");
    }

    /*
     * sigaction(): run on_signal on Ctrl-C (SIGINT) or kill (SIGTERM).
     * sa_flags = 0 (no SA_RESTART) so that a poll() in progress returns
     * EINTR and the loop sees g_stop straight away. Returns 0, or -1/EINVAL.
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0); /* whole lines even when piped to a script */

    imu_parser_stats_t done;   /* counters from ports that have since been closed */
    imu_parser_stats_t total;  /* done + the port that is open now */
    imu_parser_stats_t prev;   /* total at the last one-second summary */
    memset(&done, 0, sizeof done);
    memset(&total, 0, sizeof total);
    memset(&prev, 0, sizeof prev);

    unsigned long reconnects = 0;
    int fd = -1, ever_open = 0, told_waiting = 0;
    double t_start = now_s(), t_tick = t_start;
    double t_first = 0.0, t_last = 0.0; /* arrival time of the first / latest frame */
    int second = 0;

    while (!g_stop) {
        double now = now_s();
        if (duration > 0.0 && now - t_start >= duration) break;

        if (fd < 0) {
            fd = imu_open(port);
            if (fd < 0) {
                if (!told_waiting) {
                    fprintf(stderr, "waiting for %s: %s\n", port, strerror(errno));
                    told_waiting = 1;
                }
                sleep_ms(RETRY_MS);
            } else {
                imu_set_log_handler(fd, on_log, &quiet);
                if (ever_open) {
                    reconnects++;
                    fprintf(stderr, "reconnected (%lu)\n", reconnects);
                }
                ever_open = 1;
                told_waiting = 0;
            }
        } else {
            imu_sample_t s;
            int r = imu_read(fd, &s, RETRY_MS);
            if (r == 1) {
                t_last = now_s();
                if (t_first == 0.0) t_first = t_last;
                if (csv) {
                    fprintf(csv, "%u,%" PRIu32 ",%d,%d,%d,%d,%d,%d\n", s.seq, s.t_us,
                            s.a[0], s.a[1], s.a[2], s.g[0], s.g[1], s.g[2]);
                }
            }
            imu_parser_stats_t cur;
            imu_get_stats(fd, &cur);
            total = done;
            stats_add(&total, &cur);
            if (r < 0) {
                fprintf(stderr, "port lost (%s), retrying every %d ms\n", strerror(errno), RETRY_MS);
                done = total; /* keep its counts; the next open starts from zero */
                imu_close(fd);
                fd = -1;
            }
        }

        now = now_s();
        if (now - t_tick >= 1.0) {
            second++;
            if (!quiet) {
                uint64_t n = total.imu_frames - prev.imu_frames;
                printf("[%4ds] frames=%" PRIu64 " rate=%.1fHz crc_errors=%" PRIu64
                       " seq_gaps=%" PRIu64 " lost=%" PRIu64 "%s\n",
                       second, n, (double)n / (now - t_tick), total.crc_errors,
                       total.seq_gaps, total.frames_lost, fd < 0 ? " (port closed)" : "");
            }
            prev = total;
            t_tick = now;
        }
    }

    if (fd >= 0) imu_close(fd);
    if (csv) fclose(csv);

    /* Mean rate between the first and the last frame: n frames span n-1 periods. */
    double rate = 0.0;
    if (total.imu_frames > 1 && t_last > t_first) {
        rate = (double)(total.imu_frames - 1) / (t_last - t_first);
    }
    printf("TOTAL frames=%" PRIu64 " crc_errors=%" PRIu64 " seq_gaps=%" PRIu64
           " frames_lost=%" PRIu64 " log_frames=%" PRIu64 " bytes_discarded=%" PRIu64
           " reconnects=%lu rate_hz=%.2f\n",
           total.imu_frames, total.crc_errors, total.seq_gaps, total.frames_lost,
           total.log_frames, total.bytes_discarded, reconnects, rate);
    return 0;
}
