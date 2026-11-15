/* imu_frame.h - packs frames for the CDC stream (see protocol.h). Pure C. */
#ifndef IMU_FRAME_H
#define IMU_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

/* Write a 23-byte IMU frame into out[]. Returns PROTO_IMU_FRAME_SIZE. */
size_t proto_pack_imu(uint8_t out[PROTO_IMU_FRAME_SIZE], uint16_t seq, uint32_t t_us,
                      const int16_t a[3], const int16_t g[3]);

/*
 * Write a log frame (12 + len bytes) into out[]. Text longer than
 * PROTO_LOG_MAX_TEXT is cut. Returns the frame size, or 0 if cap is too small.
 */
size_t proto_pack_log(uint8_t *out, size_t cap, uint16_t seq, uint32_t t_us,
                      const char *text, size_t len);

#endif
