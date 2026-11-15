/* M4 L0: scaling against MPU-6050 datasheet values, and the IMU sine model. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "check.h"
#include "scaling.h"
#include "sim_data.h"

int main(void) {
    /* Datasheet sensitivities (MPU-6050 product specification, sections 6.1 and 6.2). */
    check_eq("M4 L0 accel sensitivity at +/-2 g [LSB/g]", scale_accel_lsb_per_g(2), 16384);
    check_eq("M4 L0 accel sensitivity at +/-4 g [LSB/g]", scale_accel_lsb_per_g(4), 8192);
    check_eq("M4 L0 accel sensitivity at +/-8 g [LSB/g]", scale_accel_lsb_per_g(8), 4096);
    check_eq("M4 L0 accel sensitivity at +/-16 g [LSB/g]", scale_accel_lsb_per_g(16), 2048);
    check_eq("M4 L0 gyro sensitivity at +/-250 dps [LSB/dps]", scale_gyro_lsb_per_dps(250), 131);
    check_range("M4 L0 gyro sensitivity at +/-2000 dps [LSB/dps]", scale_gyro_lsb_per_dps(2000), 16.39, 16.41);
    check_eq("M4 L0 invalid range gives 0", scale_accel_lsb_per_g(3), 0);

    float sa = scale_accel_lsb_per_g(2), sg = scale_gyro_lsb_per_dps(250);
    check_eq("M4 L0 raw 16384 at +/-2 g [g]", scale_accel_g(16384, sa), 1.0);
    check_eq("M4 L0 raw -16384 at +/-2 g [g]", scale_accel_g(-16384, sa), -1.0);
    check_eq("M4 L0 raw 8192 at +/-2 g [g]", scale_accel_g(8192, sa), 0.5);
    check_range("M4 L0 raw 32767 at +/-2 g [g]", scale_accel_g(32767, sa), 1.999, 2.0);
    check_eq("M4 L0 raw 131 at +/-250 dps [dps]", scale_gyro_dps(131, sg), 1.0);
    check_range("M4 L0 raw 32767 at +/-250 dps [dps]", scale_gyro_dps(32767, sg), 250.0, 250.2);

    /* Sine model at known instants. */
    int16_t a[3], g[3];
    sim_imu_raw(250000u, a, g); /* t = 0.25 s: sine peak */
    check_range("M4 L0 sim ax at t = 0.25 s [counts]", a[0], 8191, 8192);
    check_eq("M4 L0 sim ay [counts]", a[1], 0);
    check_eq("M4 L0 sim az [counts]", a[2], 16384);
    check_eq("M4 L0 sim gz at t = 0.25 s [counts]", g[2], 11790);
    check_range("M4 L0 sim ax at t = 0.25 s [g]", scale_accel_g(a[0], sa), 0.499, 0.501);
    check_eq("M4 L0 sim gz at t = 0.25 s [dps]", scale_gyro_dps(g[2], sg), 90.0);
    sim_imu_raw(750000u, a, g); /* t = 0.75 s: sine trough */
    check_range("M4 L0 sim ax at t = 0.75 s [counts]", a[0], -8192, -8191);
    check_eq("M4 L0 sim gz at t = 0.75 s [counts]", g[2], -11790);
    sim_imu_raw(3000000u, a, g); /* whole seconds: zero crossing */
    check_eq("M4 L0 sim ax at t = 3 s [counts]", a[0], 0);

    /* Every 200 Hz sample over 10 s matches an independent double-precision model within 1 LSB. */
    int worst = 0;
    long gz_sum = 0;
    for (uint32_t n = 0; n < 2000; n++) {
        uint32_t t_us = n * 5000u;
        sim_imu_raw(t_us, a, g);
        double model = 8192.0 * sin(2.0 * 3.14159265358979323846 * (double)(t_us % 1000000u) / 1e6);
        int err = abs(a[0] - (int)lround(model));
        if (err > worst) worst = err;
        gz_sum += g[2];
    }
    check_range("M4 L0 sim ax vs double model over 10 s: worst error [LSB]", worst, 0, 1);
    check_eq("M4 L0 sim gz square wave over 10 s: sum (turns cancel)", (double)gz_sum, 0);

    return check_done();
}
