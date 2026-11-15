/*
 * imu_parser.h - turns a raw byte stream from the Pico's CDC port into IMU
 * samples and log texts (packet format: common/protocol.h).
 *
 * PURE: no syscalls, no globals, only libc + common/. The same code runs in
 * the reader, in bench/monitor and in the unit tests (tests/test_m5_parser.c).
 *
 *   bytes in                    ring buffer                     events out
 *   --------> imu_parser_feed() [..AA 55 01 ......] imu_parser_next() --> sample / log
 *
 * Rules (docs/phase1-spec.md, Reader step 4 and gaps P3-P8):
 *   - a frame starts with 0xAA 0x55, then a type byte;
 *   - type 0x01 = IMU, 23 bytes; type 0x02 = log, 12 + len bytes;
 *   - the parser waits until the whole frame is in the buffer, then checks the CRC;
 *   - bad CRC or unknown type: drop exactly ONE byte and search again (resync);
 *   - IMU and log frames have separate seq counters; only IMU seq is gap-checked;
 *   - frames lost in a gap = (seq - previous - 1) mod 65536, so 65535 -> 0 is no gap.
 */
#ifndef IMU_PARSER_H
#define IMU_PARSER_H

#include <stddef.h>
#include <stdint.h>


/* Ring buffer size in bytes. Must be a power of two and > PROTO_MAX_FRAME_SIZE. */
#define IMU_PARSER_BUF_SIZE 4096u

/* One IMU sample, raw counts (the type named in the spec's reader API). */
typedef struct {
    uint16_t seq;
    uint32_t t_us;
    int16_t  a[3], g[3];
} imu_sample_t;

/* One log packet. text is NUL-terminated for convenience; len excludes the NUL. */
typedef struct {
    uint16_t seq;
    uint32_t t_us;
    uint16_t len;                           /* 0..255 */
    char     text[PROTO_LOG_MAX_TEXT + 1];
} imu_log_t;

typedef struct {
    uint64_t imu_frames;      /* IMU frames with a good CRC */
    uint64_t log_frames;      /* log frames with a good CRC */
    uint64_t crc_errors;      /* complete candidate frames whose CRC was wrong */
    uint64_t seq_gaps;        /* number of times IMU seq jumped (events) */
    uint64_t frames_lost;     /* sum of the gap sizes, each taken mod 65536 */
    uint64_t bytes_discarded; /* bytes thrown away that were not part of a good frame */
} imu_parser_stats_t;

typedef enum {
    IMU_EV_NONE = 0, /* nothing complete yet: feed more bytes */
    IMU_EV_SAMPLE,   /* *sample was filled in */
    IMU_EV_LOG       /* *log was filled in */
} imu_event_t;

/* Optional hook: called once for every discarded byte, in stream order. */
typedef void (*imu_discard_fn)(void *ctx, uint8_t byte);

typedef struct {
    uint8_t  buf[IMU_PARSER_BUF_SIZE];
    size_t   head;      /* index of the oldest byte */
    size_t   count;     /* bytes stored */
    int      have_seq;  /* 0 until the first IMU frame has been seen */
    uint16_t last_seq;  /* seq of the last good IMU frame */
    imu_parser_stats_t stats;
    imu_discard_fn on_discard;
    void    *discard_ctx;
} imu_parser_t;

/* Empty the buffer and zero the counters. Call once before anything else. */
void imu_parser_init(imu_parser_t *p);

/* Bytes that imu_parser_feed can still take / bytes waiting to be parsed. */
size_t imu_parser_free_space(const imu_parser_t *p);
size_t imu_parser_pending(const imu_parser_t *p);

/*
 * Copy up to len bytes into the ring buffer. Returns how many were taken
 * (less than len only when the buffer is full: call imu_parser_next until it
 * returns IMU_EV_NONE, then feed the rest).
 */
size_t imu_parser_feed(imu_parser_t *p, const uint8_t *data, size_t len);

/*
 * Pull the next complete frame out of the buffer. Either pointer may be NULL
 * if the caller does not want that kind (the frame is still counted).
 */
imu_event_t imu_parser_next(imu_parser_t *p, imu_sample_t *sample, imu_log_t *log);

/* The counters. Valid until the parser is re-initialised. */
const imu_parser_stats_t *imu_parser_stats(const imu_parser_t *p);

/* Install (or clear with NULL) the discarded-byte hook. */
void imu_parser_set_discard(imu_parser_t *p, imu_discard_fn fn, void *ctx);

#endif
