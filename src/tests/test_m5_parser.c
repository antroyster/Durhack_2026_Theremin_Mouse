/*
 * test_m5_parser.c - L0 unit tests for host/imu_parser.c (milestone 5).
 * No hardware, no syscalls: bytes are built with the common framer
 * (proto_pack_imu / proto_pack_log) and pushed through the parser.
 *
 * Build: the common/ sources + ../host/imu_parser.c + this file, with
 * -std=c11 -Wall -Wextra -Werror -I../common -I../host -lm (see tests/Makefile).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "imu_frame.h"
#include "imu_parser.h"
#include "protocol.h"

/* xorshift64: seeded, so every run of the test sees the same "random" data. */
static uint64_t g_rng;

static uint32_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

static imu_parser_t P; /* static: 4 kB */

/* What the parser handed back. */
#define MAX_OUT 20000
static imu_sample_t out_s[MAX_OUT];
static size_t out_n;
static imu_log_t out_log[8];
static size_t out_logs;

static void drain(void) {
    imu_sample_t s;
    imu_log_t l;
    imu_event_t ev;
    while ((ev = imu_parser_next(&P, &s, &l)) != IMU_EV_NONE) {
        if (ev == IMU_EV_SAMPLE) { if (out_n < MAX_OUT) out_s[out_n++] = s; }
        else { if (out_logs < 8) out_log[out_logs] = l; out_logs++; }
    }
}

/* Feed a stream in chunks of 1..max_chunk bytes. Returns bytes the parser refused (should be 0). */
static size_t feed_chunks(const uint8_t *data, size_t len, size_t max_chunk) {
    size_t refused = 0;
    for (size_t off = 0; off < len;) {
        size_t n = 1u + (max_chunk > 1 ? rnd() % max_chunk : 0u);
        if (n > len - off) n = len - off;
        size_t took = imu_parser_feed(&P, data + off, n);
        refused += n - took;
        off += n;
        drain();
    }
    return refused;
}

static void reset(uint64_t seed) {
    g_rng = seed;
    imu_parser_init(&P);
    out_n = 0;
    out_logs = 0;
}

static int same(const imu_sample_t *x, const imu_sample_t *y) {
    return x->seq == y->seq && x->t_us == y->t_us &&
           memcmp(x->a, y->a, sizeof x->a) == 0 && memcmp(x->g, y->g, sizeof x->g) == 0;
}

static imu_sample_t random_sample(void) {
    imu_sample_t s;
    s.seq = (uint16_t)rnd();
    s.t_us = rnd() ^ (rnd() << 16);
    for (int i = 0; i < 3; i++) { s.a[i] = (int16_t)rnd(); s.g[i] = (int16_t)rnd(); }
    return s;
}

static int has_inner_sync(const uint8_t *f) {
    for (size_t i = 1; i + 1 < PROTO_IMU_FRAME_SIZE; i++) {
        if (f[i] == PROTO_SYNC0 && f[i + 1] == PROTO_SYNC1) return 1;
    }
    return 0;
}

/* ---- 1. framer -> parser round trip, 10,000 random samples, random chunks ---- */
static void test_round_trip(void) {
    enum { N = 10000 };
    static imu_sample_t in[N];
    static uint8_t stream[N * PROTO_IMU_FRAME_SIZE];
    reset(0x1234567ULL);
    for (int i = 0; i < N; i++) {
        in[i] = random_sample(); /* every field random, seq included */
        proto_pack_imu(&stream[(size_t)i * PROTO_IMU_FRAME_SIZE], in[i].seq, in[i].t_us, in[i].a, in[i].g);
    }
    size_t refused = feed_chunks(stream, sizeof stream, 64);
    long bad = 0;
    for (size_t i = 0; i < out_n && i < N; i++) bad += !same(&in[i], &out_s[i]);
    check_eq("M5 L0 round trip: frames out (10000 in)", (double)out_n, N);
    check_eq("M5 L0 round trip: samples with a wrong field", (double)bad, 0);
    check_eq("M5 L0 round trip: crc errors", (double)P.stats.crc_errors, 0);
    check_eq("M5 L0 round trip: bytes discarded", (double)P.stats.bytes_discarded, 0);
    check_eq("M5 L0 round trip: bytes refused by feed", (double)refused, 0);
}

/* ---- 2. one byte at a time, IMU and log frames mixed ---- */
static void test_byte_at_a_time(void) {
    enum { N = 1000 };
    static uint8_t stream[N * PROTO_IMU_FRAME_SIZE + 10 * 64];
    size_t len = 0;
    reset(2);
    for (int i = 0; i < N; i++) {
        int16_t a[3] = { (int16_t)i, (int16_t)-i, 16384 }, g[3] = { 1, 2, (int16_t)(3 * i) };
        if (i % 100 == 0) len += proto_pack_log(&stream[len], 64, (uint16_t)(i / 100), 0, "tick", 4);
        len += proto_pack_imu(&stream[len], (uint16_t)i, (uint32_t)i * 5000u, a, g);
    }
    feed_chunks(stream, len, 1);
    long bad = 0;
    for (size_t i = 0; i < out_n; i++) {
        bad += !(out_s[i].seq == i && out_s[i].t_us == i * 5000u && out_s[i].a[1] == -(int)i);
    }
    check_eq("M5 L0 byte-at-a-time: IMU frames", (double)P.stats.imu_frames, N);
    check_eq("M5 L0 byte-at-a-time: log frames", (double)P.stats.log_frames, 10);
    check_eq("M5 L0 byte-at-a-time: wrong samples", (double)bad, 0);
    check_eq("M5 L0 byte-at-a-time: seq gaps", (double)P.stats.seq_gaps, 0);
    check_eq("M5 L0 byte-at-a-time: bytes discarded", (double)P.stats.bytes_discarded, 0);
}

/* ---- 3. noise + split frames + bad CRCs + dropped seq: counters exact ---- */
static void test_faults(void) {
    enum { N = 5000 };
    static uint8_t stream[N * (PROTO_IMU_FRAME_SIZE + 16)];
    static uint16_t expect_seq[N];
    size_t len = 0, n_expect = 0;
    long sent = 0, bad_crc = 0, dropped = 0, noise_bytes = 0, gap_events = 0;
    int in_gap = 0; /* inside a run of faulted frames: one run = one gap event */
    reset(3);
    for (int i = 0; i < N; i++) {
        int edge = (i == 0 || i == N - 1); /* first and last frames are never faulted */
        if (!edge && rnd() % 100 < 3) { dropped++; in_gap = 1; continue; }
        if (rnd() % 100 < 5) { /* noise never contains 0xAA or 0x55 */
            size_t k = 1u + rnd() % 16u;
            for (size_t j = 0; j < k; j++) {
                uint8_t b;
                do { b = (uint8_t)rnd(); } while (b == PROTO_SYNC0 || b == PROTO_SYNC1);
                stream[len++] = b;
            }
            noise_bytes += (long)k;
        }
        uint8_t *f = &stream[len];
        imu_sample_t s = random_sample();
        proto_pack_imu(f, (uint16_t)i, s.t_us, s.a, s.g);
        len += PROTO_IMU_FRAME_SIZE;
        sent++;
        int corrupted = 0;
        if (!edge && rnd() % 100 < 3 && !has_inner_sync(f)) {
            while (!corrupted) { /* flip one bit in bytes 3..22, creating no 0xAA 0x55 */
                size_t byte = 3u + rnd() % 20u;
                uint8_t bit = (uint8_t)(1u << (rnd() % 8u));
                f[byte] ^= bit;
                if (has_inner_sync(f)) f[byte] ^= bit; else corrupted = 1;
            }
        }
        if (corrupted) { bad_crc++; in_gap = 1; }
        else {
            expect_seq[n_expect++] = (uint16_t)i;
            gap_events += in_gap;
            in_gap = 0;
        }
    }
    feed_chunks(stream, len, 40); /* random chunks cut frames at random places */
    long wrong = 0;
    for (size_t i = 0; i < out_n && i < n_expect; i++) wrong += out_s[i].seq != expect_seq[i];
    check_range("M5 L0 faults: bad CRCs injected (sanity)", (double)bad_crc, 50, 500);
    check_range("M5 L0 faults: frames dropped (sanity)", (double)dropped, 50, 500);
    check_eq("M5 L0 faults: frames = sent - bad_crc", (double)P.stats.imu_frames, (double)(sent - bad_crc));
    check_eq("M5 L0 faults: crc errors = bad_crc", (double)P.stats.crc_errors, (double)bad_crc);
    check_eq("M5 L0 faults: frames lost = bad_crc + dropped", (double)P.stats.frames_lost, (double)(bad_crc + dropped));
    check_eq("M5 L0 faults: seq gap events", (double)P.stats.seq_gaps, (double)gap_events);
    check_eq("M5 L0 faults: bytes discarded = 23*bad + noise", (double)P.stats.bytes_discarded,
             (double)(23 * bad_crc + noise_bytes));
    check_eq("M5 L0 faults: frames delivered out of order/wrong", (double)wrong + (double)(out_n != n_expect), 0);
}

/* ---- 4. seq wrap 65535 -> 0 is not a gap; a gap across the wrap is counted mod 65536 ---- */
static void test_seq_wrap(void) {
    const uint16_t seqs[] = { 65534, 65535, 0, 1 };
    const int16_t z[3] = { 0, 0, 0 };
    uint8_t f[PROTO_IMU_FRAME_SIZE];
    reset(4);
    for (size_t i = 0; i < 4; i++) {
        proto_pack_imu(f, seqs[i], 0, z, z);
        imu_parser_feed(&P, f, sizeof f);
        drain();
    }
    check_eq("M5 L0 seq wrap: frames", (double)P.stats.imu_frames, 4);
    check_eq("M5 L0 seq wrap: gaps (65535 -> 0)", (double)P.stats.seq_gaps, 0);
    check_eq("M5 L0 seq wrap: frames lost", (double)P.stats.frames_lost, 0);

    const uint16_t seqs2[] = { 65534, 2 }; /* 65535, 0 and 1 are missing */
    reset(4);
    for (size_t i = 0; i < 2; i++) {
        proto_pack_imu(f, seqs2[i], 0, z, z);
        imu_parser_feed(&P, f, sizeof f);
        drain();
    }
    check_eq("M5 L0 seq wrap: gap across wrap, events", (double)P.stats.seq_gaps, 1);
    check_eq("M5 L0 seq wrap: gap across wrap, lost", (double)P.stats.frames_lost, 3);
}

/* ---- 5. log packets: len 0, typical, 255; log seq does not disturb the IMU gap check ---- */
static void test_logs(void) {
    static uint8_t stream[1024];
    char big[255];
    const char *typ = "hello fw=0.1.0 git=abc1234 sensors=SIM";
    const int16_t z[3] = { 0, 0, 0 };
    size_t len = 0;
    for (int i = 0; i < 255; i++) big[i] = (char)(' ' + i % 95);
    reset(5);
    len += proto_pack_imu(&stream[len], 0, 0, z, z);
    len += proto_pack_log(&stream[len], sizeof stream - len, 500, 111, "", 0);
    len += proto_pack_log(&stream[len], sizeof stream - len, 501, 222, typ, strlen(typ));
    len += proto_pack_imu(&stream[len], 1, 0, z, z);
    len += proto_pack_log(&stream[len], sizeof stream - len, 502, 333, big, 255);
    len += proto_pack_imu(&stream[len], 2, 0, z, z);
    feed_chunks(stream, len, 7);
    check_eq("M5 L0 log: log frames", (double)P.stats.log_frames, 3);
    check_eq("M5 L0 log: IMU frames", (double)P.stats.imu_frames, 3);
    check_eq("M5 L0 log: IMU seq gaps (separate counters, P5)", (double)P.stats.seq_gaps, 0);
    check_eq("M5 L0 log: len 0 round trip", out_log[0].len == 0 && out_log[0].text[0] == '\0' &&
             out_log[0].seq == 500 && out_log[0].t_us == 111, 1);
    check_eq("M5 L0 log: typical round trip", out_log[1].len == strlen(typ) &&
             strcmp(out_log[1].text, typ) == 0 && out_log[1].seq == 501, 1);
    check_eq("M5 L0 log: len 255 round trip", out_log[2].len == 255 &&
             memcmp(out_log[2].text, big, 255) == 0 && out_log[2].t_us == 333, 1);
    check_eq("M5 L0 log: crc errors", (double)P.stats.crc_errors, 0);
    check_eq("M5 L0 log: bytes discarded", (double)P.stats.bytes_discarded, 0);
}

/* ---- 6. 1 MB of seeded random garbage: no false frames, every byte accounted for ---- */
static void test_garbage(void) {
    enum { N = 1 << 20 };
    uint8_t *g = malloc(N);
    if (!g) { check_eq("M5 L0 garbage: malloc", 0, 1); return; }
    reset(0xC0FFEEULL);
    for (size_t i = 0; i < N; i++) g[i] = (uint8_t)rnd();
    size_t refused = feed_chunks(g, N, 512);
    free(g);
    check_eq("M5 L0 garbage 1 MB: false IMU frames", (double)P.stats.imu_frames, 0);
    check_eq("M5 L0 garbage 1 MB: false log frames", (double)P.stats.log_frames, 0);
    check_eq("M5 L0 garbage 1 MB: bytes refused by feed", (double)refused, 0);
    check_eq("M5 L0 garbage 1 MB: discarded + pending = fed",
             (double)(P.stats.bytes_discarded + imu_parser_pending(&P)), N);
    check_range("M5 L0 garbage 1 MB: pending at end (< 1 frame)", (double)imu_parser_pending(&P), 0,
                PROTO_MAX_FRAME_SIZE - 1);
}

/* ---- 7. unknown type byte is skipped; a real frame hidden behind a false start is found ---- */
static void test_unknown_type_and_resync(void) {
    uint8_t stream[128];
    const int16_t a[3] = { 1, 2, 3 }, g[3] = { 4, 5, 6 };
    size_t len = 0;
    reset(7);
    stream[len++] = PROTO_SYNC0;
    stream[len++] = PROTO_SYNC1;
    stream[len++] = 0x07; /* no such type */
    for (int i = 0; i < 20; i++) stream[len++] = (uint8_t)(0x10 + i);
    len += proto_pack_imu(&stream[len], 9, 99, a, g);
    feed_chunks(stream, len, 5);
    check_eq("M5 L0 unknown type: frames found after it", (double)P.stats.imu_frames, 1);
    check_eq("M5 L0 unknown type: not counted as a CRC error", (double)P.stats.crc_errors, 0);
    check_eq("M5 L0 unknown type: bytes discarded", (double)P.stats.bytes_discarded, 23);
    check_eq("M5 L0 unknown type: sample intact", out_n == 1 && out_s[0].seq == 9 && out_s[0].g[2] == 6, 1);

    /* "AA 55 01" then a complete good frame: the false start fails its CRC, and
     * skipping ONE byte (not 23) is what lets the real frame survive. */
    len = 0;
    reset(7);
    stream[len++] = PROTO_SYNC0;
    stream[len++] = PROTO_SYNC1;
    stream[len++] = PROTO_TYPE_IMU;
    len += proto_pack_imu(&stream[len], 10, 100, a, g);
    len += proto_pack_imu(&stream[len], 11, 101, a, g); /* gives the false start its 23 bytes */
    feed_chunks(stream, len, 5);
    check_eq("M5 L0 resync: frames found behind a false start", (double)P.stats.imu_frames, 2);
    check_eq("M5 L0 resync: crc errors", (double)P.stats.crc_errors, 1);
    check_eq("M5 L0 resync: bytes discarded", (double)P.stats.bytes_discarded, 3);
}

/* ---- 8. feed() never overruns: a full buffer refuses the extra bytes ---- */
static void test_backpressure(void) {
    static uint8_t zeros[IMU_PARSER_BUF_SIZE + 100];
    reset(8);
    size_t took = imu_parser_feed(&P, zeros, sizeof zeros);
    check_eq("M5 L0 feed: bytes accepted when over-filled", (double)took, IMU_PARSER_BUF_SIZE);
    check_eq("M5 L0 feed: free space when full", (double)imu_parser_free_space(&P), 0);
    drain();
    check_eq("M5 L0 feed: free space after draining junk", (double)imu_parser_free_space(&P), IMU_PARSER_BUF_SIZE);
}

int main(void) {
    test_round_trip();
    test_byte_at_a_time();
    test_faults();
    test_seq_wrap();
    test_logs();
    test_garbage();
    test_unknown_type_and_resync();
    test_backpressure();
    return check_done();
}
