#include "imu_frame.h"

#include <string.h>

/* Little-endian writers: low byte first, whatever the CPU's byte order. */
static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

static void put_header(uint8_t *out, uint8_t type, uint16_t seq, uint32_t t_us) {
    out[PROTO_OFF_SYNC] = PROTO_SYNC0;
    out[PROTO_OFF_SYNC + 1] = PROTO_SYNC1;
    out[PROTO_OFF_TYPE] = type;
    put_u16(&out[PROTO_OFF_SEQ], seq);
    put_u32(&out[PROTO_OFF_T_US], t_us);
}

size_t proto_pack_imu(uint8_t out[PROTO_IMU_FRAME_SIZE], uint16_t seq, uint32_t t_us,
                      const int16_t a[3], const int16_t g[3]) {
    put_header(out, PROTO_TYPE_IMU, seq, t_us);
    for (int i = 0; i < 3; i++) {
        put_u16(&out[PROTO_IMU_OFF_AX + 2 * i], (uint16_t)a[i]);
        put_u16(&out[PROTO_IMU_OFF_GX + 2 * i], (uint16_t)g[i]);
    }
    put_u16(&out[PROTO_IMU_OFF_CRC],
            proto_crc16(&out[PROTO_IMU_CRC_START], PROTO_IMU_CRC_LEN));
    return PROTO_IMU_FRAME_SIZE;
}

size_t proto_pack_log(uint8_t *out, size_t cap, uint16_t seq, uint32_t t_us,
                      const char *text, size_t len) {
    if (len > PROTO_LOG_MAX_TEXT) len = PROTO_LOG_MAX_TEXT;
    size_t total = PROTO_LOG_FRAME_SIZE(len);
    if (cap < total) return 0;

    put_header(out, PROTO_TYPE_LOG, seq, t_us);
    out[PROTO_LOG_OFF_LEN] = (uint8_t)len;
    memcpy(&out[PROTO_LOG_OFF_TEXT], text, len);
    /* CRC covers everything after the sync: type .. last text byte. */
    size_t crc_len = PROTO_LOG_OFF_TEXT + len - PROTO_SYNC_SIZE;
    put_u16(&out[PROTO_LOG_OFF_TEXT + len], proto_crc16(&out[PROTO_SYNC_SIZE], crc_len));
    return total;
}
