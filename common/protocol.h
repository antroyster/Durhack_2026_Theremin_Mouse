/*
 * protocol.h - packet format shared by the Pico firmware and the PC reader.
 *
 * Change only with Ant's approval.
 *
 * Source of truth: docs/phase1-spec.md, section "IMU packet".
 * All multi-byte fields are little-endian (low byte first on the wire).
 *
 * IMU frame (type 0x01), 23 bytes:
 *
 *   offset  0    2     3     5      9          15         21
 *          +----+-----+-----+------+----------+----------+-----+
 *          |sync|type | seq | t_us | ax ay az | gx gy gz | crc |
 *          | 2  | 1   | 2   | 4    | 3 x 2    | 3 x 2    | 2   |
 *          +----+-----+-----+------+----------+----------+-----+
 *                |<------------ CRC covers bytes 2..20 ------->|
 *
 * Log frame (type 0x02), 12 + len bytes:
 *
 *   offset  0    2     3     5      9     10        10+len
 *          +----+-----+-----+------+-----+---------+-----+
 *          |sync|type | seq | t_us | len | text    | crc |
 *          | 2  | 1   | 2   | 4    | 1   | len     | 2   |
 *          +----+-----+-----+------+-----+---------+-----+
 *                |<--- CRC covers bytes 2..(9+len) -->|
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Frame start marker. */
#define PROTO_SYNC0 0xAAu
#define PROTO_SYNC1 0x55u

/* Type codes (byte 2). */
#define PROTO_TYPE_IMU 0x01u /* raw IMU sample */
#define PROTO_TYPE_LOG 0x02u /* debug text */

/* Header: common to every frame type. */
#define PROTO_OFF_SYNC    0u
#define PROTO_OFF_TYPE    2u
#define PROTO_OFF_SEQ     3u
#define PROTO_OFF_T_US    5u
#define PROTO_HEADER_SIZE 9u
#define PROTO_SYNC_SIZE   2u
#define PROTO_CRC_SIZE    2u

/* IMU frame payload. */
#define PROTO_IMU_OFF_AX     9u
#define PROTO_IMU_OFF_AY     11u
#define PROTO_IMU_OFF_AZ     13u
#define PROTO_IMU_OFF_GX     15u
#define PROTO_IMU_OFF_GY     17u
#define PROTO_IMU_OFF_GZ     19u
#define PROTO_IMU_OFF_CRC    21u
#define PROTO_IMU_FRAME_SIZE 23u
#define PROTO_IMU_CRC_START  2u  /* first byte covered by the CRC */
#define PROTO_IMU_CRC_LEN    19u /* bytes 2..20 inclusive */

/* Log frame payload. */
#define PROTO_LOG_OFF_LEN        9u
#define PROTO_LOG_OFF_TEXT       10u
#define PROTO_LOG_MAX_TEXT       255u
#define PROTO_LOG_FRAME_SIZE(n)  (PROTO_LOG_OFF_TEXT + (n) + PROTO_CRC_SIZE)
#define PROTO_MAX_FRAME_SIZE     PROTO_LOG_FRAME_SIZE(PROTO_LOG_MAX_TEXT)

/* IMU sample rate on the wire. */
#define PROTO_IMU_RATE_HZ 200u

/*
 * The IMU frame as a packed struct. It matches the wire layout byte for byte
 * on a little-endian CPU (the RP2040 and x86/ARM Linux PCs both are).
 */
typedef struct __attribute__((packed)) {
    uint8_t  sync[2]; /* PROTO_SYNC0, PROTO_SYNC1 */
    uint8_t  type;    /* PROTO_TYPE_IMU */
    uint16_t seq;     /* +1 per IMU frame; a gap means dropped frames */
    uint32_t t_us;    /* microseconds since boot */
    int16_t  a[3];    /* ax, ay, az raw accelerometer counts */
    int16_t  g[3];    /* gx, gy, gz raw gyroscope counts */
    uint16_t crc;     /* proto_crc16 over bytes 2..20 */
} proto_imu_frame_t;

#ifdef __cplusplus
static_assert(sizeof(proto_imu_frame_t) == PROTO_IMU_FRAME_SIZE,
              "proto_imu_frame_t must be 23 bytes");
#else
_Static_assert(sizeof(proto_imu_frame_t) == PROTO_IMU_FRAME_SIZE,
               "proto_imu_frame_t must be 23 bytes");
#endif

/*
 * CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no bit
 * reflection, no final XOR. Check value: "123456789" -> 0x29B1.
 */
uint16_t proto_crc16(const uint8_t *data, size_t len);

#endif /* PROTOCOL_H */
