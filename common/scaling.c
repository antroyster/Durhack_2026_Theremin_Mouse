#include "scaling.h"

float scale_accel_lsb_per_g(int range_g) {
    switch (range_g) {
        case 2:  return 16384.0f;
        case 4:  return 8192.0f;
        case 8:  return 4096.0f;
        case 16: return 2048.0f;
        default: return 0.0f;
    }
}

float scale_gyro_lsb_per_dps(int range_dps) {
    switch (range_dps) {
        case 250:  return 131.0f;
        case 500:  return 65.5f;
        case 1000: return 32.8f;
        case 2000: return 16.4f;
        default:   return 0.0f;
    }
}

float scale_accel_g(int16_t raw, float lsb_per_g) { return (float)raw / lsb_per_g; }

float scale_gyro_dps(int16_t raw, float lsb_per_dps) { return (float)raw / lsb_per_dps; }
