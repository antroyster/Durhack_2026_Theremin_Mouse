// test_motion.cpp - checks motion.hpp with made-up IMU data whose true answer is known.
//   make test
#include <random>

#include "motion.hpp"
extern "C" {
#include "../tests/check.h"
}

using namespace motion;

// A fake IMU: give it the true orientation, world acceleration and body
// rotation rate; it produces the raw counts a real MPU-6050 would, with the
// real board's noise and a gyro bias of (4, 1.6, -3.9) deg/s.
struct FakeImu {
    std::mt19937 rng{1};
    Quaternionf R = Quaternionf::Identity();   // sensor -> world, the truth
    Vector3f bias = Vector3f(4.0f, 1.6f, -3.9f) / DEG;
    Vector3f acc_scale = Vector3f::Ones();     // each accel axis reads this fraction of the truth ...
    Vector3f acc_offset = Vector3f::Zero();    // ... plus this many g
    // The real chip clips at full scale (+/-2 g, +/-250 deg/s).
    static int16_t sat(float v) { return (int16_t)std::lround(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }
    void step(Tracker &t, Vector3f acc_world, Vector3f w_body, float dt = 0.005f) {
        std::normal_distribution<float> na(0, 0.002f), ng(0, 0.2f / DEG);
        float ang = w_body.norm() * dt;
        if (ang > 0) R = R * Quaternionf(Eigen::AngleAxisf(ang, w_body.normalized()));
        Vector3f a = (R.conjugate() * (acc_world / G0 + Vector3f::UnitZ())).cwiseProduct(acc_scale) + acc_offset;
        Vector3f w = w_body + bias;
        int16_t ar[3], gr[3];
        for (int i = 0; i < 3; i++) {
            ar[i] = sat((a[i] + na(rng)) / ACC_LSB);
            gr[i] = sat((w[i] + ng(rng)) / GYR_LSB);
        }
        t.update(ar, gr, dt);
    }
    void rest(Tracker &t, float s) { for (int i = 0; i < s * 200; i++) step(t, Vector3f::Zero(), Vector3f::Zero()); }
    // One smooth push-and-brake, a(t) = A sin(2 pi t / T): distance = A T^2 / (2 pi).
    void stroke(Tracker &t, Vector3f A, float T, long *sx = nullptr, long *sy = nullptr) {
        int n = (int)(T * 200);
        for (int i = 0; i < n; i++) {
            step(t, A * std::sin(6.2831853f * (i + 0.5f) / n), Vector3f::Zero());
            if (sx) *sx += t.cursor_dx;
            if (sy) *sy += t.cursor_dy;
        }
    }
    void twist(Tracker &t, Vector3f axis, float deg, float T) {  // turn by deg and come back
        int n = (int)(T * 200);
        float peak = deg / DEG * 3.14159265f / (T / 2) / 2;     // half-sine of rate, area = deg
        for (int i = 0; i < n; i++) step(t, Vector3f::Zero(), axis * (peak * std::sin(6.2831853f * (i + 0.5f) / n)));
    }
};

static int FILT = 0;   // orientation filter under test

static void run_all() {
    {   // 1. At rest, tilted 20 degrees: the tilt and the gyro bias are found.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.R = Quaternionf(Eigen::AngleAxisf(20 / DEG, Vector3f::UnitX()));
        imu.rest(t, 3);
        check_eq("still detected at rest (1 = yes)", t.still, 1);
        check_range("tilt of a device held at 20 deg [deg]", t.tilt_deg, 19.5, 20.5);
        check_range("gyro bias x found [deg/s] (true 4.0)", t.gyro_bias.x() * DEG, 3.8, 4.2);
        check_range("gyro bias z found [deg/s] (true -3.9)", t.gyro_bias.z() * DEG, -4.1, -3.7);
    }
    {   // 2. Flat, turn 90 degrees about z in 1 s: heading changes by 90.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.rest(t, 2);
        float h0 = t.heading();
        for (int i = 0; i < 200; i++) imu.step(t, Vector3f::Zero(), Vector3f(0, 0, 90 / DEG));
        check_range("90 deg turn about z: heading change [deg]", (t.heading() - h0) * DEG, 88, 92);
    }
    {   // 3. On the table: a 4.5 cm stroke to the right moves the cursor right, and nothing else.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.R = Quaternionf(Eigen::AngleAxisf(3 / DEG, Vector3f::UnitY()));  // table not quite level
        imu.rest(t, 2);
        check_eq("table calibration accepted (1 = yes)", t.calibrate_table(), 1);
        imu.rest(t, 1);
        long sx = 0, sy = 0, rx = 0, ry = 0;
        for (int i = 0; i < 400; i++) { imu.step(t, Vector3f::Zero(), Vector3f::Zero()); rx += t.cursor_dx; ry += t.cursor_dy; }
        check_eq("resting on the table: cursor counts x in 2 s", rx, 0);
        check_eq("resting on the table: cursor counts y in 2 s", ry, 0);
        Vector3f right = imu.R * Vector3f::UnitX();                           // along the device x axis
        imu.stroke(t, right * 3.1416f, 0.3f, &sx, &sy);
        for (int i = 0; i < 100; i++) { imu.step(t, Vector3f::Zero(), Vector3f::Zero()); sx += t.cursor_dx; sy += t.cursor_dy; }
        check_eq("still in table mode after the stroke (1 = yes)", t.mode == Mode::TABLE, 1);
        check_range("4.5 cm right: cursor x [counts] (ideal 1350 at 30000/m)", sx, 800, 1400);
        check_range("4.5 cm right: cursor y [counts]", sy, -60, 60);
        sx = sy = 0;
        Vector3f fwd = imu.R * Vector3f::UnitY();
        imu.stroke(t, fwd * 3.1416f, 0.3f, &sx, &sy);
        for (int i = 0; i < 100; i++) { imu.step(t, Vector3f::Zero(), Vector3f::Zero()); sx += t.cursor_dx; sy += t.cursor_dy; }
        check_range("4.5 cm away from you: cursor y [counts] (up = negative)", sy, -1400, -800);
        check_range("4.5 cm away from you: cursor x [counts]", sx, -60, 60);

        // 4. Lift 25 cm: leaves table mode, height and note follow. (Jerk lock off: it has its own test, 13.)
        t.p.note_lock_acc_g = 5; t.p.note_source = 0;   // this test checks the height notes
        sx = sy = 0;
        imu.stroke(t, Vector3f(0, 0, 6.2832f), 0.5f, &sx, &sy);
        imu.rest(t, 0.5f);
        check_eq("lifted 25 cm: mode is AIR (1 = yes)", t.mode == Mode::AIR, 1);
        check_range("lifted 25 cm: height estimate [m]", t.pos.z(), 0.17, 0.27);
        // Gates have hysteresis: a small settle-back at the top of the lift does not drop the note again.
        check_range("note = gates passed: floor(h / 0.04), or one more", t.note, (int)(t.pos.z() / 0.04f), (int)(t.pos.z() / 0.04f) + 1);
        check_range("note frequency [Hz] (ladder starts at 220)", t.note_hz, 220, 1400);
        check_range("cursor counts sent during the lift (100 = 3 mm of stroke)", (double)(std::labs(sx) + std::labs(sy)), 0, 100);
        imu.rest(t, 2);
        check_range("held still in the air for 2 s: height does not drift [m]", t.pos.z(), 0.17, 0.27);

        // 5. Tilted 30 degrees while low is not "on the table".
        imu.stroke(t, Vector3f(0, 0, -6.2832f), 0.5f);
        for (int i = 0; i < 100; i++) imu.step(t, Vector3f::Zero(), Vector3f(30 / DEG / 0.5f, 0, 0));
        imu.rest(t, 1);
        check_eq("back at table height but tilted 30 deg: mode is AIR (1 = yes)", t.mode == Mode::AIR, 1);
    }
    {   // 6. Gestures in the air: record two, recognise them again at another strength, reject a third.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.rest(t, 2);
        t.record_slot = 0; imu.stroke(t, Vector3f(6, 0, 0), 0.6f); imu.rest(t, 1);          // slot 1: swipe right
        t.record_slot = 1; imu.twist(t, Vector3f::UnitX(), 40, 0.8f); imu.rest(t, 1);       // slot 2: roll and back
        check_eq("gestures stored in slot 1", t.gestures[0].size(), 1);
        check_eq("gestures stored in slot 2", t.gestures[1].size(), 1);
        imu.stroke(t, Vector3f(8, 0, 0), 0.6f); imu.rest(t, 1);
        check_eq("harder swipe right recognised as slot 1 (index 0)", t.last_gesture, 0);
        check_range("its distance", t.last_gesture_dist, 0, 0.5);
        imu.twist(t, Vector3f::UnitX(), 30, 0.8f); imu.rest(t, 1);
        check_eq("smaller roll recognised as slot 2 (index 1)", t.last_gesture, 1);
        check_range("its distance", t.last_gesture_dist, 0, 0.5);
        uint32_t n = t.gesture_count;
        imu.stroke(t, Vector3f(0, 0, 6), 0.6f); imu.rest(t, 1);
        check_eq("an unrecorded movement (straight up) is not recognised (-1)", t.last_gesture, -1);
        check_eq("recognition counter unchanged by it", t.gesture_count, n);
        // Facing another way: the same swipe along the device's own x axis still matches.
        for (int i = 0; i < 200; i++) imu.step(t, Vector3f::Zero(), Vector3f(0, 0, 120 / DEG));
        imu.rest(t, 1);
        t.last_gesture = -1;
        imu.stroke(t, imu.R * Vector3f(6, 0, 0), 0.6f); imu.rest(t, 1);
        check_eq("swipe after turning 120 deg recognised as slot 1 (index 0)", t.last_gesture, 0);

        // 7. Save and load round trip.
        t.p.cursor_gain_x = 12345; t.table_up = Vector3f(0.1f, 0, 0.995f).normalized(); t.table_calibrated = true;
        check_eq("save and load succeed (1 = yes)", save(t, "build/test.cfg"), 1);
        Tracker u; load(u, "build/test.cfg");
        check_eq("load: cursor_gain_x", u.p.cursor_gain_x, 12345);
        check_eq("load: gestures in slot 2", u.gestures[1].size(), 1);
        check_range("load: gesture data identical (distance)",
                    u.gestures[0].empty() || t.gestures[0].empty() ? 9 : gesture_distance(u.gestures[0][0], t.gestures[0][0]), 0, 0.001);
        check_eq("load: table plane restored (1 = yes)", u.table_calibrated, 1);
    }
    {   // 8. Hand-held, never perfectly still, on a chip whose z axis reads 7% low (like this board):
        //    the height must not sink. This was the "falls through the floor" bug.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.acc_scale = Vector3f(1.0f, 1.0f, 0.93f);
        t.p.note_lock_acc_g = 5;                                             // jerk lock off: this tests the drift alone
        imu.rest(t, 2);
        t.calibrate_table();
        imu.stroke(t, Vector3f(0, 0, 6.2832f), 0.5f);                        // lift 25 cm
        float lo = 1e9f, hi = -1e9f; long still = 0;
        for (int i = 0; i < 4000; i++) {                                     // 20 s of hand tremor, tilting to 35 deg and back
            float s = i * 0.005f;
            Vector3f tremor(0.25f * std::sin(50 * s), 0.25f * std::sin(43 * s + 1), 0.25f * std::sin(57 * s + 2));
            Vector3f wob(0.35f * std::sin(0.9f * s) + 0.1f * std::sin(37 * s), 0.1f * std::sin(41 * s), 0);
            imu.step(t, tremor, wob);
            if (i > 400) { lo = std::min(lo, t.pos.z()); hi = std::max(hi, t.pos.z()); }
            still += t.still;
        }
        check_eq("hand-held 20 s: samples judged still (tremor prevents rest)", still, 0);
        check_range("hand-held 20 s: lowest height [m] (true 0.25; before the fix it ran off by metres)", lo, 0.12, 0.30);
        check_range("hand-held 20 s: wander, highest - lowest [m]", hi - lo, 0, 0.05);
        // And whatever happens, the device cannot be below the table.
        lo = 1e9f;
        int n = 120;
        for (int i = 0; i < n; i++) {                                        // "pushed" 69 cm down from 25 cm
            imu.step(t, Vector3f(0, 0, -12.0f * std::sin(6.2831853f * (i + 0.5f) / n)), Vector3f::Zero());
            lo = std::min(lo, t.pos.z());
        }
        check_range("pushed 69 cm down from 25 cm: lowest height reached [m] (floor = 0)", lo, 0, 0.001);
    }
    {   // 9. Six-face accelerometer calibration on a chip with offset and scale errors.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.acc_scale = Vector3f(1.03f, 0.97f, 0.93f); imu.acc_offset = Vector3f(0.02f, -0.03f, 0.05f);
        const Vector3f axes[3] = {Vector3f::UnitX(), Vector3f::UnitY(), Vector3f::UnitZ()};
        int faces = 0;
        for (int i = 0; i < 3; i++) for (int sgn = -1; sgn <= 1; sgn += 2) {
            imu.R = Quaternionf::FromTwoVectors(axes[i] * (float)sgn, Vector3f::UnitZ());   // this face up
            imu.rest(t, 1);
            if (t.calibrate_face() >= 0) faces++;
        }
        check_eq("six faces captured", faces, 6);
        check_range("accel offset z found [g] (true 0.05)", t.acc_off.z(), 0.045, 0.055);
        check_range("accel scale z found (true 0.93)", t.acc_scale.z(), 0.925, 0.935);
        check_range("accel scale x found (true 1.03)", t.acc_scale.x(), 1.025, 1.035);
        imu.R = Quaternionf(Eigen::AngleAxisf(40 / DEG, Vector3f(1, 1, 0).normalized()));
        imu.rest(t, 2);
        check_range("after calibration: |a| at a 40 deg tilt [g]", t.cal_a.norm(), 0.995, 1.005);
    }
    {   // 10. Cursor method 1 (air mouse): turn 20 deg clockwise seen from above = pointer right.
        Tracker t; t.p.filter = (float)FILT; t.p.cursor_method = 1; FakeImu imu;
        imu.rest(t, 2);
        long sx = 0, sy = 0;
        for (int i = 0; i < 400; i++) { imu.step(t, Vector3f::Zero(), Vector3f::Zero()); sx += t.cursor_dx; sy += t.cursor_dy; }
        check_eq("air mouse at rest for 2 s: counts moved", std::labs(sx) + std::labs(sy), 0);
        for (int i = 0; i < 100; i++) { imu.step(t, Vector3f::Zero(), Vector3f(0, 0, -40 / DEG)); sx += t.cursor_dx; sy += t.cursor_dy; }
        for (int i = 0; i < 100; i++) { imu.step(t, Vector3f::Zero(), Vector3f::Zero()); sx += t.cursor_dx; sy += t.cursor_dy; }
        check_range("air mouse: 20 deg clockwise, x [counts] (ideal 314 at 900 per rad)", sx, 280, 330);
        check_range("air mouse: 20 deg clockwise, y [counts]", sy, -15, 15);
    }
    {   // 11. Cursor method 2 (tilt = speed): lean 14 deg to the right for 1 s.
        Tracker t; t.p.filter = (float)FILT; t.p.cursor_method = 2; FakeImu imu;
        imu.rest(t, 2); t.calibrate_table();
        for (int i = 0; i < 100; i++) imu.step(t, Vector3f::Zero(), Vector3f(0, 14 / DEG / 0.5f, 0));   // roll right about +y
        imu.rest(t, 1);
        long sx = 0, sy = 0;
        for (int i = 0; i < 200; i++) { imu.step(t, Vector3f::Zero(), Vector3f::Zero()); sx += t.cursor_dx; sy += t.cursor_dy; }
        check_range("tilt mouse: 14 deg right for 1 s, x [counts] (ideal (14-4)*40 = 400)", sx, 340, 440);
        check_range("tilt mouse: 14 deg right for 1 s, y [counts]", sy, -20, 20);
    }
    {   // 12. All-zero samples (IMU not answering) are refused and do not disturb the estimate.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.R = Quaternionf(Eigen::AngleAxisf(20 / DEG, Vector3f::UnitX()));
        imu.rest(t, 2);
        float hz_before = t.note_hz;
        int16_t z[3] = {0, 0, 0};
        for (int i = 0; i < 400; i++) t.update(z, z, 0.005f);
        check_eq("zero samples: data_ok", t.data_ok, 0);
        check_range("before the zeros: a note was playing [Hz]", hz_before, 200, 2000);
        check_range("zero samples for 2 s: last note held [Hz]", t.note_hz, hz_before, hz_before);
        Tracker fresh; for (int i = 0; i < 400; i++) fresh.update(z, z, 0.005f);
        check_eq("zeros from the start: no note to hold (0 Hz = silent)", fresh.note_hz, 0);
        check_range("zero samples for 2 s: tilt unchanged [deg]", t.tilt_deg, 19.5, 20.5);
    }
    {   // 13. Note gates are relative, and a jerk locks the note and restores the position.
        Tracker t; t.p.filter = (float)FILT; t.p.note_source = 0; FakeImu imu;
        imu.rest(t, 2); t.calibrate_table();
        check_eq("on the table: note is the lowest (0)", t.note, 0);
        imu.stroke(t, Vector3f(0, 0, 4.0f), 0.5f); imu.rest(t, 0.5f);       // gentle lift of 16 cm (peak 0.41 g)
        int n1 = t.note; float h1 = t.pos.z();
        check_range("gentle 16 cm lift: notes climbed", n1, 2, 4);
        check_eq("gentle lift: jerks seen", t.lock_count, 0);
        imu.stroke(t, Vector3f(0, 0, -1.6f), 0.5f); imu.rest(t, 0.5f);      // 6.4 cm back down
        check_range("6 cm back down: notes dropped by", n1 - t.note, 1, 2);
        int n2 = t.note; float h2 = t.pos.z();
        imu.stroke(t, Vector3f(0, 0, 14.0f), 0.4f);                          // a jerk: 36 cm at 1.4 g
        check_eq("jerk upward: note is locked (1 = yes)", t.note_locked, 1);
        check_eq("jerk upward: note unchanged", t.note, n2);
        check_range("jerk upward: height put back to the safe average [m]", t.pos.z() - h2, -0.02, 0.02);
        imu.rest(t, 1);
        check_eq("1 s after the jerk: lock released (0 = free)", t.note_locked, 0);
        check_eq("1 s after the jerk: note still unchanged", t.note, n2);
        t.p.note_by_height = 0;
        imu.stroke(t, Vector3f(0, 0, 4.0f), 0.5f); imu.rest(t, 0.5f);
        check_eq("note_by_height = 0: a lift does not change the note", t.note, n2);
        (void)h1;
    }
    {   // 14. Rest snap: resting flat on the table for rest_snap_s puts q back to the calibrated orientation.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.R = Quaternionf(Eigen::AngleAxisf(3 / DEG, Vector3f::UnitY()));  // table not quite level
        imu.rest(t, 3); t.calibrate_table();
        Quaternionf q_cal = Quaternionf::FromTwoVectors(t.table_up, Vector3f::UnitZ());
        imu.twist(t, Vector3f::UnitZ(), 30, 0.5f);                   // a movement: the still timer restarts
        t.q = Quaternionf(Eigen::AngleAxisf(25 / DEG, Vector3f::UnitZ())) * t.q;   // fake 25 deg of yaw drift
        imu.rest(t, 1.0f);
        check_range("drifted, resting 1 s (< rest_snap_s): not snapped yet [deg]", t.q.angularDistance(q_cal) * DEG, 20, 30);
        imu.rest(t, 1.5f);
        check_range("resting 2.5 s: q back to the calibrated orientation [deg]", t.q.angularDistance(q_cal) * DEG, 0, 0.5);
        check_range("rest snap counted", t.snap_count, 1, 1000);
        uint32_t n = t.snap_count;
        for (int i = 0; i < 200; i++) imu.step(t, Vector3f::Zero(), Vector3f(40 / DEG / 1.0f, 0, 0));   // tip onto its side, 40 deg
        imu.rest(t, 4);
        check_range("resting tipped 40 deg for 4 s: tilt kept, no snap [deg]", t.tilt_deg, 38, 42);
        check_eq("resting tipped: no new snaps", t.snap_count - n, 0);
        t.p.rest_snap_s = 0;
        for (int i = 0; i < 200; i++) imu.step(t, Vector3f::Zero(), Vector3f(-40 / DEG / 1.0f, 0, 0));   // back flat
        t.q = Quaternionf(Eigen::AngleAxisf(25 / DEG, Vector3f::UnitZ())) * t.q;
        imu.rest(t, 3);
        check_range("rest_snap_s = 0: yaw drift stays [deg]", t.q.angularDistance(q_cal) * DEG, 20, 30);
    }
    {   // 15. Tilt notes: green (y) axis at -90..+90 deg -> lowest..highest of 15 notes, 12 deg each.
        Tracker t; t.p.filter = (float)FILT; FakeImu imu;
        imu.rest(t, 2); t.calibrate_table(); imu.rest(t, 0.2f);
        auto tip = [&](float deg) {                                  // turn about x: +deg raises the +y end
            for (int i = 0; i < 200; i++) imu.step(t, Vector3f::Zero(), Vector3f(deg / DEG, 0, 0));
            imu.rest(t, 0.5f);
        };
        check_range("flat: green tilt [deg]", t.tilt_y_deg, -1, 1);
        check_eq("flat: middle note (7 of 0..14)", t.note, 7);
        float f_mid = t.note_hz;
        tip(80);
        check_range("green end up 80 deg: tilt [deg]", t.tilt_y_deg, 78, 82);
        check_eq("green end up 80 deg: highest note (14)", t.note, 14);
        check_range("higher tilt = higher pitch [Hz ratio]", t.note_hz / f_mid, 1.5, 10);
        tip(-160);
        check_range("green end down 80 deg: tilt [deg]", t.tilt_y_deg, -82, -78);
        check_eq("green end down 80 deg: lowest note (0)", t.note, 0);
        tip(80);
        check_eq("back flat: middle note again", t.note, 7);
        tip(6.5f);                                                   // step edge at +6, hysteresis 1.5
        check_eq("6.5 deg (inside the hysteresis): note held at 7", t.note, 7);
        tip(2.5f);
        check_eq("9 deg (past the hysteresis): note 8", t.note, 8);
    }
}

int main() {
    static const char *names[3] = {"complementary", "Mahony", "Madgwick"};
    for (FILT = 0; FILT < 3; FILT++) { std::printf("---- orientation filter %d: %s ----\n", FILT, names[FILT]); run_all(); }
    return check_done();
}
