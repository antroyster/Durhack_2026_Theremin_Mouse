/*
 * imu_mpu6050.c - MPU-6050 over I2C0 at 400 kHz (register map from the
 * InvenSense MPU-6000/6050 register map, as used in mucking_about/mpu_6050).
 *
 * One sample = 14 bytes starting at ACCEL_XOUT_H, each value big-endian
 * (HIGH byte first - the opposite of our packet format):
 *
 *   0x3B  ax_h ax_l  ay_h ay_l  az_h az_l  temp_h temp_l  gx_h gx_l  gy_h gy_l  gz_h gz_l
 */
#include "imu_mpu6050.h"

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define REG_SMPLRT_DIV   0x19 /* output rate = 1 kHz / (1 + div) when the filter is on */
#define REG_CONFIG       0x1A /* digital low-pass filter */
#define REG_GYRO_CONFIG  0x1B /* bits 4:3 = full-scale range */
#define REG_ACCEL_CONFIG 0x1C /* bits 4:3 = full-scale range */
#define REG_ACCEL_XOUT_H 0x3B
#define REG_PWR_MGMT_1   0x6B
#define REG_WHO_AM_I     0x75
#define WHO_AM_I_VALUE   0x68

#define I2C_TIMEOUT_US   2000u /* a whole 14-byte read takes about 450 us at 400 kHz */

static bool reg_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_write_timeout_us(CFG_IMU_I2C_PORT, CFG_IMU_I2C_ADDR, buf, 2, false, I2C_TIMEOUT_US) == 2;
}

static bool reg_read(uint8_t reg, uint8_t *dst, size_t len) {
    /* Write the register address with no STOP, then read: a "repeated start". */
    if (i2c_write_timeout_us(CFG_IMU_I2C_PORT, CFG_IMU_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(CFG_IMU_I2C_PORT, CFG_IMU_I2C_ADDR, dst, len, false, I2C_TIMEOUT_US) == (int)len;
}

/* Range setting bits for GYRO_CONFIG / ACCEL_CONFIG: 0..3 in bits 4:3. */
static uint8_t accel_fs_bits(void) {
    switch (CFG_IMU_ACCEL_RANGE_G) { case 4: return 1u << 3; case 8: return 2u << 3; case 16: return 3u << 3; default: return 0; }
}
static uint8_t gyro_fs_bits(void) {
    switch (CFG_IMU_GYRO_RANGE_DPS) { case 500: return 1u << 3; case 1000: return 2u << 3; case 2000: return 3u << 3; default: return 0; }
}

bool mpu6050_init(uint8_t *who) {
    i2c_init(CFG_IMU_I2C_PORT, CFG_IMU_I2C_HZ);
    gpio_set_function(CFG_PIN_IMU_SDA, GPIO_FUNC_I2C);
    gpio_set_function(CFG_PIN_IMU_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(CFG_PIN_IMU_SDA); /* I2C lines are open-drain: something must pull them high */
    gpio_pull_up(CFG_PIN_IMU_SCL);

    *who = 0;
    if (!reg_read(REG_WHO_AM_I, who, 1) || *who != WHO_AM_I_VALUE) return false;

    bool ok = reg_write(REG_PWR_MGMT_1, 0x01);                 /* wake up; clock from the X gyro (more stable) */
    ok = ok && reg_write(REG_CONFIG, 0x03);                    /* low-pass 44 Hz; gyro rate becomes 1 kHz */
    ok = ok && reg_write(REG_SMPLRT_DIV, (uint8_t)(1000u / CFG_IMU_RATE_HZ - 1u)); /* 1 kHz / (1 + 4) = 200 Hz */
    ok = ok && reg_write(REG_GYRO_CONFIG, gyro_fs_bits());
    ok = ok && reg_write(REG_ACCEL_CONFIG, accel_fs_bits());
    return ok;
}

bool mpu6050_read(int16_t a[3], int16_t g[3]) {
    uint8_t b[14];
    if (!reg_read(REG_ACCEL_XOUT_H, b, sizeof b)) {
        for (int i = 0; i < 3; i++) { a[i] = 0; g[i] = 0; }
        return false;
    }
    for (int i = 0; i < 3; i++) {
        a[i] = (int16_t)((uint16_t)b[2 * i] << 8 | b[2 * i + 1]);         /* bytes 0..5 */
        g[i] = (int16_t)((uint16_t)b[8 + 2 * i] << 8 | b[8 + 2 * i + 1]); /* bytes 8..13, after the temperature */
    }
    return true;
}
