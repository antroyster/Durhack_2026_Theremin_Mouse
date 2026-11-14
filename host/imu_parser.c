/* imu_parser.c - see imu_parser.h. Pure C: no syscalls, no globals. */
#include "imu_parser.h"

#include <string.h>

#define MASK (IMU_PARSER_BUF_SIZE - 1u)

_Static_assert((IMU_PARSER_BUF_SIZE & MASK) == 0, "buffer size must be a power of two");
_Static_assert(IMU_PARSER_BUF_SIZE > PROTO_MAX_FRAME_SIZE, "buffer must hold the largest frame");

/* Little-endian readers: low byte first, whatever the CPU's byte order. */
static uint16_t get_u16(const uint8_t *b) {
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

static uint32_t get_u32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* The i-th waiting byte (0 = oldest). The mask makes the index wrap round. */
static uint8_t peek(const imu_parser_t *p, size_t i) {
    return p->buf[(p->head + i) & MASK];
}

/* Throw away the oldest byte: this is the "skip one byte and resync" step. */
static void discard_one(imu_parser_t *p) {
    uint8_t b = peek(p, 0);
    p->head = (p->head + 1u) & MASK;
    p->count--;
    p->stats.bytes_discarded++;
    if (p->on_discard) p->on_discard(p->discard_ctx, b);
}

void imu_parser_init(imu_parser_t *p) {
    memset(p, 0, sizeof *p);
}

size_t imu_parser_free_space(const imu_parser_t *p) {
    return IMU_PARSER_BUF_SIZE - p->count;
}

size_t imu_parser_pending(const imu_parser_t *p) {
    return p->count;
}

const imu_parser_stats_t *imu_parser_stats(const imu_parser_t *p) {
    return &p->stats;
}

void imu_parser_set_discard(imu_parser_t *p, imu_discard_fn fn, void *ctx) {
    p->on_discard = fn;
    p->discard_ctx = ctx;
}

size_t imu_parser_feed(imu_parser_t *p, const uint8_t *data, size_t len) {
    size_t space = imu_parser_free_space(p);
    if (len > space) len = space;
    size_t tail = (p->head + p->count) & MASK;    /* first free slot */
    size_t first = IMU_PARSER_BUF_SIZE - tail;    /* room before the end of the array */
    if (first > len) first = len;
    memcpy(&p->buf[tail], data, first);
    memcpy(&p->buf[0], data + first, len - first); /* the part that wraps to the start */
    p->count += len;
    return len;
}

imu_event_t imu_parser_next(imu_parser_t *p, imu_sample_t *sample, imu_log_t *log) {
    uint8_t f[PROTO_MAX_FRAME_SIZE]; /* the candidate frame, copied out of the ring */

    for (;;) {
        /* 1. Sync search: the oldest byte must be 0xAA, the next 0x55. */
        if (p->count < 1u) return IMU_EV_NONE;
        if (peek(p, 0) != PROTO_SYNC0) { discard_one(p); continue; }
        if (p->count < 2u) return IMU_EV_NONE;
        if (peek(p, 1) != PROTO_SYNC1) { discard_one(p); continue; }

        /* 2. The type byte gives the frame length. */
        if (p->count < 3u) return IMU_EV_NONE;
        uint8_t type = peek(p, PROTO_OFF_TYPE);
        size_t need;
        if (type == PROTO_TYPE_IMU) {
            need = PROTO_IMU_FRAME_SIZE;
        } else if (type == PROTO_TYPE_LOG) {
            if (p->count <= PROTO_LOG_OFF_LEN) return IMU_EV_NONE;
            need = PROTO_LOG_FRAME_SIZE((size_t)peek(p, PROTO_LOG_OFF_LEN));
        } else {
            discard_one(p); /* P8: unknown type is not a frame */
            continue;
        }

        /* 3. Wait for the whole frame. */
        if (p->count < need) return IMU_EV_NONE;
        for (size_t i = 0; i < need; i++) f[i] = peek(p, i);

        /* 4. CRC over everything between the sync and the CRC itself. */
        size_t crc_off = need - PROTO_CRC_SIZE;
        uint16_t want = proto_crc16(&f[PROTO_SYNC_SIZE], crc_off - PROTO_SYNC_SIZE);
        if (get_u16(&f[crc_off]) != want) {
            p->stats.crc_errors++;
            discard_one(p); /* only the 0xAA: a real frame may start inside this one */
            continue;
        }

        /* 5. Good frame: take it out of the ring and decode it. */
        p->head = (p->head + need) & MASK;
        p->count -= need;
        uint16_t seq = get_u16(&f[PROTO_OFF_SEQ]);
        uint32_t t_us = get_u32(&f[PROTO_OFF_T_US]);

        if (type == PROTO_TYPE_IMU) {
            p->stats.imu_frames++;
            if (p->have_seq) {
                /* uint16_t arithmetic wraps mod 65536, so 65535 -> 0 gives 0 lost. */
                uint16_t lost = (uint16_t)(seq - p->last_seq - 1u);
                if (lost != 0u) {
                    p->stats.seq_gaps++;
                    p->stats.frames_lost += lost;
                }
            }
            p->have_seq = 1;
            p->last_seq = seq;
            if (sample) {
                sample->seq = seq;
                sample->t_us = t_us;
                for (int i = 0; i < 3; i++) {
                    sample->a[i] = (int16_t)get_u16(&f[PROTO_IMU_OFF_AX + 2 * i]);
                    sample->g[i] = (int16_t)get_u16(&f[PROTO_IMU_OFF_GX + 2 * i]);
                }
            }
            return IMU_EV_SAMPLE;
        }

        p->stats.log_frames++;
        if (log) {
            size_t len = f[PROTO_LOG_OFF_LEN];
            log->seq = seq;
            log->t_us = t_us;
            log->len = (uint16_t)len;
            memcpy(log->text, &f[PROTO_LOG_OFF_TEXT], len);
            log->text[len] = '\0';
        }
        return IMU_EV_LOG;
    }
}
