/* M5 L0: the CRC and the frame layout. (The parser tests are in test_m5_parser.c.) */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "imu_frame.h"
#include "protocol.h"

int main(void) {
    check_eq("M5 L0 CRC-16/CCITT-FALSE of \"123456789\"", proto_crc16((const uint8_t *)"123456789", 9), 0x29B1);
    check_eq("M5 L0 CRC of nothing is the initial value", proto_crc16((const uint8_t *)"", 0), 0xFFFF);

    /* Pack one frame with recognisable values and check every byte offset in the spec table. */
    const int16_t a[3] = { 0x0102, -2, 0x4000 };
    const int16_t g[3] = { 0x1112, 0x2122, -32768 };
    uint8_t f[PROTO_IMU_FRAME_SIZE];
    check_eq("M5 L0 IMU frame size [bytes]", (double)proto_pack_imu(f, 0xBEEF, 0xA1B2C3D4u, a, g), 23);
    check_eq("M5 L0 byte 0 sync", f[0], 0xAA);
    check_eq("M5 L0 byte 1 sync", f[1], 0x55);
    check_eq("M5 L0 byte 2 type", f[2], 0x01);
    check_eq("M5 L0 bytes 3-4 seq, low byte first", f[3] | (f[4] << 8), 0xBEEF);
    check_eq("M5 L0 byte 3 is the LOW byte of seq", f[3], 0xEF);
    check_eq("M5 L0 bytes 5-8 t_us, low byte first",
             (double)((uint32_t)f[5] | ((uint32_t)f[6] << 8) | ((uint32_t)f[7] << 16) | ((uint32_t)f[8] << 24)),
             (double)0xA1B2C3D4u);
    check_eq("M5 L0 bytes 9-10 ax", (int16_t)(f[9] | (f[10] << 8)), 0x0102);
    check_eq("M5 L0 bytes 11-12 ay (negative)", (int16_t)(f[11] | (f[12] << 8)), -2);
    check_eq("M5 L0 bytes 13-14 az", (int16_t)(f[13] | (f[14] << 8)), 0x4000);
    check_eq("M5 L0 bytes 15-16 gx", (int16_t)(f[15] | (f[16] << 8)), 0x1112);
    check_eq("M5 L0 bytes 17-18 gy", (int16_t)(f[17] | (f[18] << 8)), 0x2122);
    check_eq("M5 L0 bytes 19-20 gz (most negative)", (int16_t)(f[19] | (f[20] << 8)), -32768);
    check_eq("M5 L0 bytes 21-22 CRC over bytes 2..20", f[21] | (f[22] << 8), proto_crc16(&f[2], 19));

    /* The packed struct in protocol.h overlays the same bytes on a little-endian PC. */
    proto_imu_frame_t s;
    memcpy(&s, f, sizeof s);
    check_eq("M5 L0 packed struct: seq", s.seq, 0xBEEF);
    check_eq("M5 L0 packed struct: az", s.a[2], 0x4000);
    check_eq("M5 L0 packed struct: crc", s.crc, proto_crc16(&f[2], 19));

    /* A single flipped bit anywhere in bytes 2..22 is always caught. */
    int missed = 0;
    for (int bit = 2 * 8; bit < 23 * 8; bit++) {
        uint8_t bad[PROTO_IMU_FRAME_SIZE];
        memcpy(bad, f, sizeof bad);
        bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        if (proto_crc16(&bad[2], 19) == (uint16_t)(bad[21] | (bad[22] << 8))) missed++;
    }
    check_eq("M5 L0 single-bit errors not caught by the CRC (of 168)", missed, 0);

    /* Log frame: header, length byte, text, CRC over everything after the sync. */
    uint8_t lf[PROTO_MAX_FRAME_SIZE];
    size_t n = proto_pack_log(lf, sizeof lf, 7, 1000, "hello", 5);
    check_eq("M5 L0 log frame size for 5 chars [bytes]", (double)n, 17);
    check_eq("M5 L0 log byte 2 type", lf[2], 0x02);
    check_eq("M5 L0 log byte 9 length", lf[9], 5);
    check_eq("M5 L0 log text starts at byte 10", memcmp(&lf[10], "hello", 5), 0);
    check_eq("M5 L0 log CRC over bytes 2..14", lf[15] | (lf[16] << 8), proto_crc16(&lf[2], 13));
    check_eq("M5 L0 log frame refuses a buffer that is too small", (double)proto_pack_log(lf, 16, 7, 1000, "hello", 5), 0);
    check_eq("M5 L0 largest frame [bytes]", PROTO_MAX_FRAME_SIZE, 267);

    return check_done();
}
