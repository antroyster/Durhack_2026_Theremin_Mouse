/* scaling.h - raw IMU counts to physical units (done on the host). Pure C. */
#ifndef SCALING_H
#define SCALING_H

#include <stdint.h>

/* MPU-6050 sensitivity for a full-scale range. Returns 0 for an invalid range. */
float scale_accel_lsb_per_g(int range_g);     /* 2, 4, 8, 16   -> 16384, 8192, 4096, 2048 */
float scale_gyro_lsb_per_dps(int range_dps);  /* 250, 500, 1000, 2000 -> 131, 65.5, 32.8, 16.4 */

/* a = raw / S_a [g] ; w = raw / S_g [deg/s] */
float scale_accel_g(int16_t raw, float lsb_per_g);
float scale_gyro_dps(int16_t raw, float lsb_per_dps);

#endif
