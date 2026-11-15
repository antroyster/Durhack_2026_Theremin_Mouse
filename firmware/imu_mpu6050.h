/* imu_mpu6050.h - MPU-6050 accelerometer + gyroscope on I2C. */
#ifndef IMU_MPU6050_H
#define IMU_MPU6050_H

#include <stdbool.h>
#include <stdint.h>

/* Boot time only. Returns true if WHO_AM_I answers 0x68; *who gets what was read (0 = no answer). */
bool mpu6050_init(uint8_t *who);

/* Read ax, ay, az, gx, gy, gz raw counts. About 0.45 ms; has a timeout, so it
 * cannot hang the tick interrupt. Returns false (and zeros) on a bus error. */
bool mpu6050_read(int16_t a[3], int16_t g[3]);

/* Wake the chip and write its settings again (recovery after it reset itself). */
bool mpu6050_configure(void);

#endif
