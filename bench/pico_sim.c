/*
 * pico_sim - a fake Pico on a pseudo-terminal, for test level L1
 * (docs/phase1-spec.md, Testing section 4). The real reader opens the pty
 * slave exactly as it would open /dev/serial/by-id/..., so the whole PC
 * pipeline is tested with no hardware.
 *
 *   pico_sim --write()--> pty master ==kernel== pty slave --read()--> imu_reader
 *                                               ^ --link PATH points here
 *
 *   pico_sim [--link PATH] [--rate HZ] [--duration S] [--seed N] [--wait-ms MS]
 *            [--corrupt PCT] [--drop PCT] [--noise PCT] [--split PCT]
 *
 * Frame n has seq = n and t_us = n * (1000000 / rate); the values come from
 * sim_imu_raw(t_us). One log packet ("hello sim") per second, own seq.
 * Faults are deterministic for a given --seed; each fault type has its own
 * random stream, so changing one percentage does not move the other faults.
 * The first and last frames are never corrupted or dropped.
 */
#define _XOPEN_SOURCE 700 /* posix_openpt, grantpt, unlockpt, ptsname */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "imu_frame.h"
#include "protocol.h"
#include "sim_data.h"

#define SETTLE_MS 100 /* after the reader opens: time for it to set raw mode */
#define LINGER_MS 300 /* after the last frame: time for the reader to drain the pty */

static volatile sig_atomic_t g_stop;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

/* xorshift32: a tiny seeded random generator, the same on every machine. */
typedef struct { uint32_t s; } rng_t;

static uint32_t rng_next(rng_t *r) {
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->s = x;
}

static void rng_seed(rng_t *r, uint32_t seed, uint32_t stream) {
    r->s = (seed * 2654435761u) ^ (stream * 0x9E3779B9u);
    if (r->s == 0) r->s = 1;
    for (int i = 0; i < 8; i++) rng_next(r);
}

/* True with probability pct percent. Always draws, so streams stay in step. */
static int chance(rng_t *r, double pct) {
    return (double)rng_next(r) / 4294967296.0 * 100.0 < pct;
}

static void sleep_ms(long ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* write() may take only part of the buffer; loop until all of it has gone. */
static int write_all(int fd, const uint8_t *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) { if (g_stop) return -1; continue; }
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* Is there a 0xAA 0x55 pair after the real sync at offset 0? */
static int has_inner_sync(const uint8_t *f) {
    for (size_t i = 1; i + 1 < PROTO_IMU_FRAME_SIZE; i++) {
        if (f[i] == PROTO_SYNC0 && f[i + 1] == PROTO_SYNC1) return 1;
    }
    return 0;
}

/*
 * Flip one bit in bytes 3..22 without creating a 0xAA 0x55 pair, so the
 * reader sees exactly one CRC error and then finds no false frame start.
 * Returns 1 if the frame was corrupted.
 */
static int corrupt_frame(uint8_t *f, rng_t *r) {
    if (has_inner_sync(f)) return 0; /* cannot give an exact count: leave it alone */
    for (int tries = 0; tries < 64; tries++) {
        size_t byte = 3u + rng_next(r) % (PROTO_IMU_FRAME_SIZE - 3u);
        uint8_t bit = (uint8_t)(1u << (rng_next(r) % 8u));
        f[byte] ^= bit;
        if (!has_inner_sync(f)) return 1;
        f[byte] ^= bit; /* undo and try another bit */
    }
    return 0;
}

/* True while no program has the pty slave open (Linux reports POLLHUP on the master). */
static int slave_closed(int mfd) {
    struct pollfd p = { .fd = mfd, .events = 0, .revents = 0 };
    poll(&p, 1, 0);
    return (p.revents & POLLHUP) != 0;
}

static void usage(void) {
    fprintf(stderr,
            "usage: pico_sim [--link PATH] [--rate HZ] [--duration S] [--seed N] [--wait-ms MS]\n"
            "                [--corrupt PCT] [--drop PCT] [--noise PCT] [--split PCT]\n"
            "  --wait-ms MS  start after MS even if no reader has opened the port (0 = wait for ever)\n");
}

int main(int argc, char **argv) {
    const char *link_path = NULL;
    long rate = PROTO_IMU_RATE_HZ, wait_ms = 0;
    double duration = 60.0, p_corrupt = 0, p_drop = 0, p_noise = 0, p_split = 0;
    uint32_t seed = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!v) { usage(); return 2; }
        if (!strcmp(a, "--link")) link_path = v;
        else if (!strcmp(a, "--rate")) rate = atol(v);
        else if (!strcmp(a, "--duration")) duration = atof(v);
        else if (!strcmp(a, "--seed")) seed = (uint32_t)strtoul(v, NULL, 0);
        else if (!strcmp(a, "--wait-ms")) wait_ms = atol(v);
        else if (!strcmp(a, "--corrupt")) p_corrupt = atof(v);
        else if (!strcmp(a, "--drop")) p_drop = atof(v);
        else if (!strcmp(a, "--noise")) p_noise = atof(v);
        else if (!strcmp(a, "--split")) p_split = atof(v);
        else { usage(); return 2; }
        i++;
    }
    if (rate < 1 || rate > 10000 || duration <= 0) { usage(); return 2; }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /*
     * posix_openpt(): create a new pseudo-terminal pair and return the fd of
     * its master side. Returns -1 with errno ENOSPC/EMFILE when out of ptys/fds.
     * grantpt(): make the slave device belong to this user. unlockpt(): allow
     * the slave to be opened. Both return 0 or -1 (EBADF/EINVAL: not a master).
     * ptsname(): the slave's path (/dev/pts/N), or NULL on error.
     */
    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (mfd < 0) { perror("posix_openpt"); return 1; }
    if (grantpt(mfd) < 0 || unlockpt(mfd) < 0) { perror("grantpt/unlockpt"); return 1; }
    const char *slave = ptsname(mfd);
    if (!slave) { perror("ptsname"); return 1; }

    /*
     * Open the slave once ourselves, put it in raw mode and close it again:
     *  - a new pty starts in "cooked" mode (echo, CR/LF translation), which
     *    would mangle frames sent before the reader calls tcsetattr;
     *  - once a slave has been opened and closed, poll() on the master shows
     *    POLLHUP until the next program opens it: that is how we detect the reader.
     */
    int sfd = open(slave, O_RDWR | O_NOCTTY);
    if (sfd < 0) { perror(slave); return 1; }
    struct termios tio;
    if (tcgetattr(sfd, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(sfd, TCSANOW, &tio);
    }
    close(sfd);

    if (link_path) {
        /* symlink(): create link_path -> slave. Fails with EEXIST if the name is taken. */
        unlink(link_path); /* remove a stale link from an earlier run; ENOENT is fine */
        if (symlink(slave, link_path) < 0) { perror(link_path); return 1; }
    }
    fprintf(stderr, "pico_sim: port %s%s%s, waiting for a reader\n", slave,
            link_path ? " linked as " : "", link_path ? link_path : "");

    for (long waited = 0; !g_stop && slave_closed(mfd); waited += 10) {
        if (wait_ms > 0 && waited >= wait_ms) break;
        sleep_ms(10);
    }
    if (!g_stop) sleep_ms(SETTLE_MS);

    rng_t r_corrupt, r_drop, r_noise, r_split;
    rng_seed(&r_corrupt, seed, 1);
    rng_seed(&r_drop, seed, 2);
    rng_seed(&r_noise, seed, 3);
    rng_seed(&r_split, seed, 4);

    unsigned long frames_sent = 0, bad_crc = 0, dropped = 0;
    unsigned long noise_bursts = 0, noise_bytes = 0, split = 0;
    uint32_t total = (uint32_t)((double)rate * duration + 0.5);
    uint32_t step_us = (uint32_t)(1000000L / rate);
    long period_ns = 1000000000L / rate;
    uint16_t log_seq = 0;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (uint32_t n = 0; n < total && !g_stop; n++) {
        /*
         * Pace on an ABSOLUTE deadline t0 + n * period. Sleeping "for one
         * period" each loop would add the loop's own run time every frame and
         * the rate would drift low; an absolute deadline cannot drift.
         * clock_nanosleep() returns 0, or an error number (EINTR on a signal).
         */
        long long ns = (long long)t0.tv_nsec + (long long)n * period_ns;
        struct timespec dl = { .tv_sec = t0.tv_sec + (time_t)(ns / 1000000000LL),
                               .tv_nsec = (long)(ns % 1000000000LL) };
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &dl, NULL) == EINTR && !g_stop) {}
        if (g_stop) break;

        uint32_t t_us = n * step_us;

        if (n % (uint32_t)rate == 0) { /* one log packet per second */
            uint8_t lf[PROTO_LOG_FRAME_SIZE(PROTO_LOG_MAX_TEXT)];
            size_t len = proto_pack_log(lf, sizeof lf, log_seq++, t_us, "hello sim", 9);
            if (write_all(mfd, lf, len) < 0) break;
        }

        /* Draw every fault for every frame, so each stream depends only on n. */
        int first_or_last = (n == 0 || n == total - 1);
        int do_drop = chance(&r_drop, p_drop) && !first_or_last;
        int do_corrupt = chance(&r_corrupt, p_corrupt) && !first_or_last;
        int do_noise = chance(&r_noise, p_noise);
        int do_split = chance(&r_split, p_split);

        if (do_drop) { /* seq still advances: the reader sees a gap */
            dropped++;
            continue;
        }

        if (do_noise) {
            uint8_t nb[16];
            size_t len = 1u + rng_next(&r_noise) % 16u;
            for (size_t i = 0; i < len; i++) {
                do { nb[i] = (uint8_t)rng_next(&r_noise); }
                while (nb[i] == PROTO_SYNC0 || nb[i] == PROTO_SYNC1);
            }
            if (write_all(mfd, nb, len) < 0) break;
            noise_bursts++;
            noise_bytes += len;
        }

        int16_t a[3], g[3];
        uint8_t f[PROTO_IMU_FRAME_SIZE];
        sim_imu_raw(t_us, a, g);
        proto_pack_imu(f, (uint16_t)n, t_us, a, g);
        if (do_corrupt && corrupt_frame(f, &r_corrupt)) bad_crc++;

        if (do_split) {
            size_t cut = 1u + rng_next(&r_split) % (PROTO_IMU_FRAME_SIZE - 1u);
            if (write_all(mfd, f, cut) < 0) break;
            sleep_ms(1);
            if (write_all(mfd, f + cut, sizeof f - cut) < 0) break;
            split++;
        } else {
            if (write_all(mfd, f, sizeof f) < 0) break;
        }
        frames_sent++;
    }

    /* Closing the master discards unread bytes, so give the reader time to drain. */
    sleep_ms(LINGER_MS);
    if (link_path) unlink(link_path);
    close(mfd);

    printf("INJECTED frames_sent=%lu bad_crc=%lu dropped=%lu noise_bursts=%lu noise_bytes=%lu split=%lu\n",
           frames_sent, bad_crc, dropped, noise_bursts, noise_bytes, split);
    return 0;
}
