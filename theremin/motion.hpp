// motion.hpp - raw IMU samples -> orientation, table/air mode, cursor counts, height,
// theremin note, volume and gestures. Pure maths (Eigen only), so test_motion.cpp can check it.
//
// Frames: sensor S = the chip's own x, y, z.  World W = z up, x/y level, yaw arbitrary.
//         q rotates S into W:  v_W = q * v_S.     Units: m/s^2, rad/s, m, s.
#pragma once

#include <Eigen/Dense>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace motion {

using Eigen::Quaternionf;
using Eigen::Vector3f;

constexpr float G0 = 9.80665f;                                       // m/s^2 in one g
constexpr float ACC_LSB = 1.0f / 16384.0f;                           // g per count (+/-2 g)
constexpr float GYR_LSB = (1.0f / 131.0f) * 3.14159265f / 180.0f;    // rad/s per count (+/-250 deg/s)
constexpr float DEG = 180.0f / 3.14159265f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float acos_deg(float c) { return std::acos(c > 1 ? 1 : (c < -1 ? -1 : c)) * DEG; }
inline float asin_deg(float s) { return std::asin(s > 1 ? 1 : (s < -1 ? -1 : s)) * DEG; }

// Every tuning number. The program lists them on screen and saves them.
struct Params {
    // Stillness: over the last 0.2 s, max - min of each axis stays below these.
    float still_gyro_dps = 2.5f, still_acc_g = 0.04f;
    // Orientation filter: 0 complementary (tilt_gain), 1 Mahony (kp, ki), 2 Madgwick (beta).
    float filter = 2, tilt_gain = 1.5f, mahony_kp = 2.0f, mahony_ki = 0.05f, madgwick_beta = 0.1f;
    float acc_reject_g = 0.15f;    // ignore the accelerometer when |a| is this far from 1 g
    float auto_bias = 1;           // 1 = re-learn the gyro bias whenever still
    // Rest snap: still this long and lying flat (within rest_snap_deg) -> q = calibrated q. 0 = off.
    float rest_snap_s = 2.0f, rest_snap_deg = 3.0f;
    // Table mode: within this tilt and height of the calibrated plane, for this long.
    float table_tilt_deg = 6.0f, table_h_m = 0.04f, table_dwell_s = 0.30f;
    // Cursor: 0 strokes (acceleration, table only), 1 air mouse (rotation rate), 2 tilt = speed.
    float cursor_method = 0;
    float cursor_gyro_gain = 900, gyro_dead_dps = 1.5f;    // method 1: counts per radian, dead zone
    float cursor_tilt_gain = 40, tilt_dead_deg = 4.0f;     // method 2: counts/s per degree, dead zone
    float cursor_smooth = 0.3f;                            // methods 1, 2: 1 = none, 0.05 = heavy
    float cursor_gain_x = 30000, cursor_gain_y = 30000;    // method 0: counts per metre
    float cursor_invert_x = 0, cursor_invert_y = 0;        // 1 flips
    // Linear acceleration and velocity.
    float acc_deadband_g = 0.015f;  // smaller than this counts as 0
    float acc_bias_s = 4.0f;        // offsets slower than this are removed
    float calm_acc_g = 0.06f, calm_gyro_dps = 40.0f;   // hand-held tremor only: damp velocity hard
    float vel_leak_s = 3.0f;        // velocity decays with this time constant
    // Theremin note. note_source 1 = tilt of the y (green) axis: -90..+90 deg over note_count steps.
    //                note_source 2 = tilt of the x (red) axis, the nose, the same way.
    //                note_source 0 = height: one step per note_step_m since the last change.
    float note_source = 1, note_tilt_hyst_deg = 1.5f;
    float note_by_height = 1, note_step_m = 0.04f, note_count = 15;
    float note_lock_acc_g = 0.6f, note_lock_hold_s = 0.3f;  // a vertical jerk locks the note this long
    float note_safe_logs = 20;      // on a jerk, position returns to the mean of this many samples
    float note_base_midi = 57;      // lowest note (57 = A3, 220 Hz)
    float note_glide = 0;           // 1 = continuous pitch
    // Volume, tilt notes only: the other axis turns it up (+) or down (-) like a knob,
    // vol_rate per second per 10 deg beyond vol_dead_deg. Level again and it stays.
    float vol_on = 1, vol_dead_deg = 5.0f, vol_rate = 0.25f;
    float vol_invert = 1;           // 1 = the other way round: + turns it down, - turns it up
    float gesture_max_dist = 0.75f; // 0 = identical, about 1.4 = unrelated
};

// name, field, arrow-key step, and the range the arrow keys keep it in.
struct ParamInfo { const char *name; float Params::*field; float step, lo = 0, hi = 1e9f; };
inline const std::vector<ParamInfo> &param_table() {
    using P = Params;
    static const std::vector<ParamInfo> t = {
        {"filter", &P::filter, 1, 0, 2},                 {"madgwick_beta", &P::madgwick_beta, 0.02f},
        {"mahony_kp", &P::mahony_kp, 0.25f},             {"mahony_ki", &P::mahony_ki, 0.01f},
        {"tilt_gain", &P::tilt_gain, 0.25f},             {"acc_reject_g", &P::acc_reject_g, 0.01f},
        {"auto_bias", &P::auto_bias, 1},                 {"cursor_method", &P::cursor_method, 1, 0, 2},
        {"rest_snap_s", &P::rest_snap_s, 0.5f},          {"rest_snap_deg", &P::rest_snap_deg, 0.5f},
        {"note_source", &P::note_source, 1, 0, 2},       {"note_tilt_hyst_deg", &P::note_tilt_hyst_deg, 0.5f},
        {"cursor_gyro_gain", &P::cursor_gyro_gain, 50},  {"gyro_dead_dps", &P::gyro_dead_dps, 0.25f},
        {"cursor_tilt_gain", &P::cursor_tilt_gain, 5},   {"tilt_dead_deg", &P::tilt_dead_deg, 0.5f},
        {"cursor_smooth", &P::cursor_smooth, 0.05f, 0.05f, 1},
        {"table_tilt_deg", &P::table_tilt_deg, 0.5f},    {"table_h_m", &P::table_h_m, 0.005f},
        {"table_dwell_s", &P::table_dwell_s, 0.05f},     {"cursor_gain_x", &P::cursor_gain_x, 2000},
        {"cursor_gain_y", &P::cursor_gain_y, 2000},      {"cursor_invert_x", &P::cursor_invert_x, 1},
        {"cursor_invert_y", &P::cursor_invert_y, 1},     {"acc_deadband_g", &P::acc_deadband_g, 0.005f},
        {"vel_leak_s", &P::vel_leak_s, 0.25f, 0.1f},     {"acc_bias_s", &P::acc_bias_s, 0.25f, 0.1f},
        {"calm_acc_g", &P::calm_acc_g, 0.01f},           {"calm_gyro_dps", &P::calm_gyro_dps, 5},
        {"still_gyro_dps", &P::still_gyro_dps, 0.5f},    {"still_acc_g", &P::still_acc_g, 0.005f},
        {"note_by_height", &P::note_by_height, 1},       {"note_step_m", &P::note_step_m, 0.005f, 0.005f},
        {"note_lock_acc_g", &P::note_lock_acc_g, 0.05f}, {"note_lock_hold_s", &P::note_lock_hold_s, 0.05f},
        {"note_safe_logs", &P::note_safe_logs, 5},
        {"note_count", &P::note_count, 1},               {"note_base_midi", &P::note_base_midi, 1},
        {"note_glide", &P::note_glide, 1},               {"gesture_max_dist", &P::gesture_max_dist, 0.05f},
        {"vol_on", &P::vol_on, 1, 0, 1},                 {"vol_dead_deg", &P::vol_dead_deg, 0.5f},
        {"vol_rate", &P::vol_rate, 0.05f},               {"vol_invert", &P::vol_invert, 1, 0, 1},
    };
    return t;
}

// ---- Gestures: a movement resampled to GEST_N points of 6 numbers, scaled to mean square 1
// (so a gesture matches whether made gently or hard).
constexpr int GEST_N = 32, GEST_SLOTS = 9;
using GestureVec = std::array<float, GEST_N * 6>;

inline bool gesture_normalise(const std::vector<std::array<float, 6>> &rec, GestureVec &out) {
    const int n = (int)rec.size();
    if (n < 8) return false;
    double sq = 0;
    for (int i = 0; i < GEST_N; i++) {
        float pos = (float)i * (n - 1) / (GEST_N - 1);
        int i0 = (int)pos, i1 = (i0 + 1 < n) ? i0 + 1 : i0;
        float f = pos - i0;
        for (int k = 0; k < 6; k++) {
            float v = rec[i0][k] * (1 - f) + rec[i1][k] * f;
            out[i * 6 + k] = v;
            sq += (double)v * v;
        }
    }
    float rms = (float)std::sqrt(sq / GEST_N);
    if (rms < 1e-3f) return false;
    for (float &v : out) v /= rms;
    return true;
}
// d = sqrt(mean over points of |a_i - b_i|^2):  0 = same, sqrt(2) = unrelated.
inline float gesture_distance(const GestureVec &a, const GestureVec &b) {
    double s = 0;
    for (size_t i = 0; i < a.size(); i++) { double d = a[i] - b[i]; s += d * d; }
    return (float)std::sqrt(s / GEST_N);
}

enum class Mode { AIR, TABLE };

class Tracker {
public:
    Params p;

    // ---- outputs, valid after each update() ----
    Quaternionf q = Quaternionf::Identity();   // sensor -> world
    Vector3f vel = Vector3f::Zero();           // world velocity [m/s]
    Vector3f pos = Vector3f::Zero();           // world position [m]; z = height above the table
    Vector3f lin = Vector3f::Zero();           // world linear acceleration, gravity removed [m/s^2]
    Vector3f gyro_bias = Vector3f::Zero();     // [rad/s]
    Mode mode = Mode::AIR;
    bool still = false, calm = false;          // calm = hand-held with only tremor
    bool data_ok = false;                      // false while samples are all zero (IMU not answering)
    Vector3f raw_a = Vector3f::Zero(), raw_g = Vector3f::Zero();   // last raw counts
    Vector3f cal_a = Vector3f::Zero(), cal_w = Vector3f::Zero();   // calibrated: g, rad/s (bias removed)
    Vector3f acc_off = Vector3f::Zero(), acc_scale = Vector3f::Ones();   // a = (raw - off) / scale [g]
    Vector3f mahony_int = Vector3f::Zero();    // Mahony's integral term [rad/s]
    int face_count = 0;                        // six-face calibration: faces captured so far
    bool table_calibrated = false;
    Vector3f table_up = Vector3f::UnitZ();     // "up" in the sensor frame when on the table
    float tilt_deg = 0;                        // angle from the table plane
    float tilt_y_deg = 0;                      // signed angle of the y (green) axis above the table, -90..+90
    float tilt_x_deg = 0;                      // the same for the x (red) axis, the nose
    uint32_t snap_count = 0;                   // rest snaps that changed q
    float g_ref = 1.0f;                        // |a| at rest [g] (this chip's own 1 g)
    int cursor_dx = 0, cursor_dy = 0;          // whole counts to send for this sample
    int note = 0;                              // index on the note ladder, 0 = lowest
    float note_hz = 0;                         // 0 until the first real sample
    bool note_locked = false;                  // a jerk was seen: the note is held
    float note_progress = 0;                   // -1..+1 towards the next step down / up
    float volume = 0.5f;                       // 0..1, turned by the tilt across the note axis
    uint32_t lock_count = 0;                   // jerks that locked the note
    int last_gesture = -1;                     // slot recognised most recently
    float last_gesture_dist = 0;
    uint32_t gesture_count = 0;                // +1 per recognition, so the UI can react once
    int record_slot = -1;                      // >= 0: the next movement is stored in this slot
    std::array<std::vector<GestureVec>, GEST_SLOTS> gestures;

    // One sample: int16 counts from the stream, dt in seconds.
    void update(const int16_t a_raw[3], const int16_t g_raw[3], float dt) {
        if (dt <= 0 || dt > 0.05f) dt = 0.005f;
        raw_a = Vector3f(a_raw[0], a_raw[1], a_raw[2]);
        raw_g = Vector3f(g_raw[0], g_raw[1], g_raw[2]);
        cursor_dx = cursor_dy = 0;
        // A working chip never reads six exact zeros. If it does, everything (the note too)
        // holds its last value, so the sound carries on at the last pitch.
        data_ok = !(a_raw[0] == 0 && a_raw[1] == 0 && a_raw[2] == 0 && g_raw[0] == 0 && g_raw[1] == 0 && g_raw[2] == 0);
        if (!data_ok) { still = false; win_.clear(); return; }
        Vector3f a = (raw_a * ACC_LSB - acc_off).cwiseQuotient(acc_scale);   // g
        Vector3f w_raw = raw_g * GYR_LSB;                                    // rad/s

        bool was_still = still;
        Vector3f w = update_stillness(a, w_raw);
        cal_a = a; cal_w = w;
        if (!init_) { q = Quaternionf::FromTwoVectors(a.normalized(), Vector3f::UnitZ()); init_ = true; }
        orientation_update(a, w, dt);
        rest_snap(dt);
        Vector3f up_s = update_tilt();
        update_linear(a, w, dt);
        integrate_position(dt);
        update_mode(dt);
        update_cursor(w, up_s, dt);
        update_note(dt);
        update_volume(dt);
        update_gestures(w, was_still);
    }

    // Device resting on the table: store the table plane. False if it is moving.
    bool calibrate_table() {
        if (!still || !init_) return false;
        table_up = win_sum_a().normalized();
        table_calibrated = true;
        zero_height();
        mode = Mode::TABLE;
        return true;
    }
    void zero_height() { pos.setZero(); vel.setZero(); anchor_h_ = 0; note = 0; safe_.clear(); }

    // Device still: the mean gyro reading IS the bias.
    bool calibrate_gyro() {
        if (win_.size() < 40 || !window_is_quiet()) return false;
        Vector3f wm = Vector3f::Zero();
        for (auto &s : win_) wm += s.w;
        gyro_bias = wm / (float)win_.size();
        mahony_int.setZero();
        return true;
    }

    // Six-face accelerometer calibration: rest on each face and call once per face. Each axis
    // gets a +1 g and a -1 g reading:  offset = (plus + minus) / 2,  scale = (plus - minus) / 2.
    // Returns the face captured (0..5), or -1 if moving.
    int calibrate_face() {
        if (win_.size() < 40 || !window_is_quiet()) return -1;
        Vector3f am = Vector3f::Zero();
        for (auto &s : win_) am += s.a.cwiseProduct(acc_scale) + acc_off;   // undo the current calibration
        am /= (float)win_.size();
        int axis; am.cwiseAbs().maxCoeff(&axis);
        int face = axis * 2 + (am[axis] < 0 ? 1 : 0);
        face_val_[face] = am[axis];
        face_mask_ |= 1u << face;
        face_count = __builtin_popcount(face_mask_);
        if (face_mask_ == 0x3F) {
            for (int i = 0; i < 3; i++) {
                acc_off[i] = (face_val_[2 * i] + face_val_[2 * i + 1]) / 2;
                acc_scale[i] = (face_val_[2 * i] - face_val_[2 * i + 1]) / 2;
            }
            face_mask_ = 0; face_count = 6;
            g_ref = 1.0f; win_.clear();
        }
        return face;
    }
    void reset_orientation() { init_ = false; mahony_int.setZero(); }
    float heading() const { Vector3f x = q * Vector3f::UnitX(); return std::atan2(x.y(), x.x()); }   // yaw [rad]
    // Tilt notes: one axis plays the note, the one across it sets the volume.
    float note_tilt_deg() const { return p.note_source == 2 ? tilt_x_deg : tilt_y_deg; }
    float vol_tilt_deg() const { return p.note_source == 2 ? tilt_y_deg : tilt_x_deg; }

private:
    struct Win { Vector3f a, w; };
    std::deque<Win> win_;                      // the last 40 samples (0.2 s)
    bool init_ = false, have_bias_ = false, recording_ = false;
    float plane_time_ = 0, acc_x_ = 0, acc_y_ = 0, yaw0_ = 0, still_time_ = 0, calm_level_ = 1.0f;
    float anchor_h_ = 0, lock_quiet_ = 0;      // height at the last note change; time since the last jerk
    std::deque<Vector3f> safe_;                // recent positions logged while no jerk was happening
    Vector3f rate_f_ = Vector3f::Zero();       // smoothed rate, cursor methods 1 and 2
    Vector3f lin_bias_ = Vector3f::Zero();     // slow offset of the linear acceleration [g]
    float face_val_[6] = {0, 0, 0, 0, 0, 0};
    unsigned face_mask_ = 0;
    std::vector<std::array<float, 6>> rec_;

    Vector3f win_sum_a() const { Vector3f s = Vector3f::Zero(); for (auto &x : win_) s += x.a; return s; }

    // 1. Stillness from the spread of the last 40 samples. A spread test works before the gyro
    //    bias is known. While still, the gyro reads its bias and the accelerometer reads 1 g.
    //    Returns the gyro with the bias removed.
    Vector3f update_stillness(const Vector3f &a, const Vector3f &w_raw) {
        win_.push_back({a, w_raw});
        if (win_.size() > 40) win_.pop_front();
        still = window_is_still();
        if (still) {
            Vector3f wm = Vector3f::Zero(); float am = 0;
            for (auto &s : win_) { wm += s.w; am += s.a.norm(); }
            wm /= (float)win_.size(); am /= (float)win_.size();
            if (p.auto_bias != 0) gyro_bias += 0.05f * (wm - gyro_bias);
            g_ref += 0.05f * (am - g_ref);
            if (!have_bias_) { if (p.auto_bias != 0) gyro_bias = wm; g_ref = am; have_bias_ = true; }
        }
        return w_raw - gyro_bias;
    }

    // 2. Orientation, by the filter in p.filter. a [g], w [rad/s, bias removed].
    //    Estimated up in the sensor frame v = q^-1 z; the accelerometer measures it directly.
    void orientation_update(const Vector3f &a, Vector3f w, float dt) {
        bool acc_ok = std::fabs(a.norm() - g_ref) < p.acc_reject_g && a.norm() > 0.1f;
        Vector3f an = a.normalized();
        int f = (int)p.filter;
        if (f == 1) {
            // Mahony: error e = a x v; the gyro is nudged by Kp e, and Ki integrates e into a bias.
            if (acc_ok) {
                Vector3f e = an.cross(q.conjugate() * Vector3f::UnitZ());
                mahony_int += p.mahony_ki * e * dt;
                w += p.mahony_kp * e + mahony_int;
            }
            integrate(w, dt);
        } else if (f == 2) {
            // Madgwick: q' = 1/2 q (x) w - beta grad(f) / |grad(f)|,  f(q) = v(q) - a.
            Eigen::Vector4f qd = 0.5f * quat_mul(Eigen::Vector4f(q.w(), q.x(), q.y(), q.z()), Eigen::Vector4f(0, w.x(), w.y(), w.z()));
            if (acc_ok) {
                float q0 = q.w(), q1 = q.x(), q2 = q.y(), q3 = q.z();
                Vector3f fq(2 * (q1 * q3 - q0 * q2) - an.x(), 2 * (q0 * q1 + q2 * q3) - an.y(), 2 * (0.5f - q1 * q1 - q2 * q2) - an.z());
                Eigen::Matrix<float, 3, 4> J;
                J << -2 * q2, 2 * q3, -2 * q0, 2 * q1,
                      2 * q1, 2 * q0,  2 * q3, 2 * q2,
                      0,     -4 * q1, -4 * q2, 0;
                Eigen::Vector4f grad = J.transpose() * fq;
                float n = grad.norm();
                if (n > 1e-9f) qd -= p.madgwick_beta * grad / n;
            }
            Eigen::Vector4f qn = Eigen::Vector4f(q.w(), q.x(), q.y(), q.z()) + qd * dt;
            q = Quaternionf(qn[0], qn[1], qn[2], qn[3]);
        } else {
            // Complementary: turn with the gyro, then tip towards measured gravity.
            integrate(w, dt);
            if (acc_ok) {
                Vector3f err = (q * an).cross(Vector3f::UnitZ());   // axis * sin(angle) that brings it to z
                float e = err.norm();
                if (e > 1e-6f) q = Quaternionf(Eigen::AngleAxisf(p.tilt_gain * dt * e, err / e)) * q;
            }
        }
        q.normalize();
    }
    void integrate(const Vector3f &w, float dt) {                    // q <- q * exp(w dt)
        float ang = w.norm() * dt;
        if (ang > 1e-9f) q = q * Quaternionf(Eigen::AngleAxisf(ang, w.normalized()));
    }
    static Eigen::Vector4f quat_mul(const Eigen::Vector4f &a, const Eigen::Vector4f &b) {   // (w, x, y, z)
        return Eigen::Vector4f(a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
                               a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
                               a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
                               a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]);
    }

    // 2b. Rest snap: resting flat on the table for rest_snap_s, the true orientation IS the
    //     calibrated one, q_cal = FromTwoVectors(table_up, z) (the start-up rule, so yaw returns to
    //     its start-up reference). Checked with measured gravity, so a drifted q cannot hide it.
    void rest_snap(float dt) {
        still_time_ = still ? still_time_ + dt : 0;
        if (p.rest_snap_s <= 0 || !table_calibrated || still_time_ < p.rest_snap_s) return;
        if (acos_deg(win_sum_a().normalized().dot(table_up)) >= p.rest_snap_deg) return;
        Quaternionf q_cal = Quaternionf::FromTwoVectors(table_up, Vector3f::UnitZ());
        if (q.angularDistance(q_cal) > 1e-4f) snap_count++;
        q = q_cal;
        mahony_int.setZero();
    }

    // 3. Tilt from the table plane, and the signed tilt of the y and x axes:
    //    n = axis flattened onto the table plane,  theta = asin(n . up).  Returns up in the sensor frame.
    Vector3f update_tilt() {
        Vector3f up_s = q.conjugate() * Vector3f::UnitZ();
        tilt_deg = acos_deg(up_s.dot(table_up));
        Vector3f n_y = Vector3f::UnitY() - Vector3f::UnitY().dot(table_up) * table_up;
        Vector3f n_x = Vector3f::UnitX() - Vector3f::UnitX().dot(table_up) * table_up;
        if (n_y.norm() > 1e-3f) tilt_y_deg = asin_deg(n_y.normalized().dot(up_s));
        if (n_x.norm() > 1e-3f) tilt_x_deg = asin_deg(n_x.normalized().dot(up_s));
        return up_s;
    }

    // 4. Linear acceleration. On the table gravity is removed with the calibrated table_up, so a
    //    small error in q cannot leak gravity into the cursor. A slow offset (the axes never read
    //    exactly 1 g, the tilt is never perfect) is learned and removed: quickly when still or calm
    //    (tremor only), slowly otherwise. A real movement pushes then brakes, so it passes.
    void update_linear(const Vector3f &a, const Vector3f &w, float dt) {
        Vector3f lin_g = (mode == Mode::TABLE) ? Vector3f(q * (a - g_ref * table_up))
                                               : Vector3f(q * a - g_ref * Vector3f::UnitZ());
        float resid = (lin_g - lin_bias_).norm();
        calm_level_ = std::max(resid, calm_level_ * std::exp(-dt / 0.1f));   // peak, fading over 0.1 s
        calm = calm_level_ < p.calm_acc_g && w.norm() * DEG < p.calm_gyro_dps;
        float kb = still ? 0.05f : dt / (calm ? 0.4f : p.acc_bias_s);
        lin_bias_ += kb * (lin_g - lin_bias_);
        lin_g -= lin_bias_;
        for (int i = 0; i < 3; i++) if (std::fabs(lin_g[i]) < p.acc_deadband_g) lin_g[i] = 0;
        lin = lin_g * G0;
    }

    // 5. v += a dt with a leak; v = 0 whenever still (zero-velocity update stops the drift).
    //    Nothing is integrated before the first rest, and nothing goes below the table.
    void integrate_position(float dt) {
        if (still || !have_bias_) {
            vel.setZero();
            if (mode == Mode::TABLE) pos.z() = 0;
        } else {
            vel += lin * dt;
            vel *= std::exp(-dt / (calm ? 0.15f : p.vel_leak_s));
            pos += vel * dt;
        }
        if (pos.z() < 0 && mode == Mode::AIR) { pos.z() = 0; if (vel.z() < 0) vel.z() = 0; }
    }

    // 6. Table or air?
    void update_mode(float dt) {
        bool on_plane = table_calibrated && tilt_deg < p.table_tilt_deg && std::fabs(pos.z()) < p.table_h_m;
        if (on_plane) plane_time_ += dt; else plane_time_ = 0;
        if (mode == Mode::AIR && plane_time_ >= p.table_dwell_s) { mode = Mode::TABLE; pos.z() = 0; vel.z() = 0; }
        if (mode == Mode::TABLE && !on_plane) mode = Mode::AIR;
    }

    // 7. Cursor counts by p.cursor_method. Fractions are kept for the next sample.
    void update_cursor(const Vector3f &w, const Vector3f &up_s, float dt) {
        float cx = 0, cy = 0;
        int method = (int)p.cursor_method;
        if (method == 0) {
            // Strokes: velocity along the device's own x and y, table mode only. HID +y = down.
            if (mode == Mode::TABLE && !still) {
                Vector3f v_s = q.conjugate() * vel;
                cx = v_s.x() * dt * p.cursor_gain_x;
                cy = -v_s.y() * dt * p.cursor_gain_y;
            }
        } else if (method == 1) {
            // Air mouse: turning about world z moves x, tipping about device y moves y.
            Vector3f r((q * w).z(), w.y(), 0);
            for (int i = 0; i < 2; i++) if (std::fabs(r[i]) * DEG < p.gyro_dead_dps) r[i] = 0;
            rate_f_ += p.cursor_smooth * (r - rate_f_);                 // low-pass against tremor
            cx = -rate_f_.x() * dt * p.cursor_gyro_gain;               // anticlockwise from above = left
            cy = rate_f_.y() * dt * p.cursor_gyro_gain;
        } else {
            // Tilt = speed: lean right to go right, forward to go up; zero = the table plane.
            Vector3f d = (up_s - table_up) * DEG;                       // small angles: degrees of lean
            Vector3f r(-d.x(), d.y(), 0);
            for (int i = 0; i < 2; i++) r[i] = (std::fabs(r[i]) < p.tilt_dead_deg) ? 0 : r[i] - std::copysign(p.tilt_dead_deg, r[i]);
            rate_f_ += p.cursor_smooth * (r - rate_f_);
            cx = rate_f_.x() * dt * p.cursor_tilt_gain;
            cy = rate_f_.y() * dt * p.cursor_tilt_gain;
        }
        acc_x_ += cx * (p.cursor_invert_x != 0 ? -1 : 1);
        acc_y_ += cy * (p.cursor_invert_y != 0 ? -1 : 1);
        cursor_dx = (int)acc_x_; acc_x_ -= cursor_dx;
        cursor_dy = (int)acc_y_; acc_y_ -= cursor_dy;
    }

    // 8. Theremin note.
    //    Tilt:   x = (theta + 90) / (180 / N) on the ladder, note = floor(x), with hysteresis.
    //    Height: anchor = height at the last change, d = height - anchor; d >= gate: up one,
    //            d <= -gate: down one. A vertical jerk locks the note and puts the position back to
    //            the mean of the last note_safe_logs samples. On the table: the lowest note.
    void update_note(float dt) {
        int n_notes = std::max(1, (int)p.note_count);
        float span = 180.0f / n_notes;                                  // degrees per note (tilt)
        if (p.note_source != 0) {
            float x = (note_tilt_deg() + 90.0f) / span, h = p.note_tilt_hyst_deg / span;
            if (x < note - h || x > note + 1 + h) note = std::max(0, std::min(n_notes - 1, (int)std::floor(x)));
            note_locked = false; anchor_h_ = 0; safe_.clear();
        } else if (mode == Mode::TABLE) {
            note = 0; anchor_h_ = 0; note_locked = false; safe_.clear();
        } else {
            update_height_note(n_notes, dt);
        }
        if (note > n_notes - 1) note = n_notes - 1;
        if (p.note_source != 0) {
            note_progress = 2.0f * ((note_tilt_deg() + 90.0f) / span - note - 0.5f);
            note_progress = std::max(-1.0f, std::min(1.0f, note_progress));
        } else {
            note_progress = (pos.z() - anchor_h_) / std::max(0.005f, p.note_step_m);
            if (mode == Mode::TABLE) note_progress = 0;
        }
        static const int penta[5] = {0, 2, 4, 7, 9};                    // major pentatonic: always in tune
        float semis = 12.0f * (note / 5) + penta[note % 5];
        if (p.note_glide != 0 && !note_locked) semis += 2.4f * std::max(-1.0f, std::min(1.0f, note_progress));
        note_hz = 440.0f * std::pow(2.0f, (p.note_base_midi - 69 + semis) / 12.0f);
    }
    void update_height_note(int n_notes, float dt) {
        if (std::fabs(lin.z()) > p.note_lock_acc_g * G0) {             // a jerk
            if (!note_locked) { note_locked = true; lock_count++; }
            lock_quiet_ = 0;
        } else if (note_locked && (lock_quiet_ += dt) >= p.note_lock_hold_s) {
            note_locked = false;
        }
        if (note_locked) {
            if (!safe_.empty()) {                                       // back to where it safely was
                Vector3f m = Vector3f::Zero();
                for (auto &v : safe_) m += v;
                pos = m / (float)safe_.size();
            }
            vel.setZero();
            return;
        }
        safe_.push_back(pos);
        while ((int)safe_.size() > std::max(1, (int)p.note_safe_logs)) safe_.pop_front();
        if (p.note_by_height == 0) return;
        float gate = std::max(0.005f, p.note_step_m);
        while (pos.z() - anchor_h_ >= gate && note < n_notes - 1) { note++; anchor_h_ += gate; }
        while (pos.z() - anchor_h_ <= -gate && note > 0) { note--; anchor_h_ -= gate; }
        // At either end of the ladder the anchor follows, so turning back responds at once.
        if (note == n_notes - 1 && pos.z() > anchor_h_) anchor_h_ = pos.z();
        if (note == 0 && pos.z() < anchor_h_) anchor_h_ = pos.z();
    }

    // 9. Volume, like a knob: theta = vol_tilt_deg(), e = |theta| - dead zone. Outside it
    //    dv/dt = s * vol_rate * sign(theta) * e / 10 deg, v kept in 0..1, s = -1 if vol_invert
    //    else +1. Height notes: v is fixed.
    void update_volume(float dt) {
        if (p.vol_on == 0 || p.note_source == 0) return;
        float th = vol_tilt_deg(), e = std::fabs(th) - p.vol_dead_deg, s = p.vol_invert != 0 ? -1.0f : 1.0f;
        if (e > 0) volume = clampf(volume + s * p.vol_rate * std::copysign(e, th) / 10.0f * dt, 0, 1);
    }

    // 10. Gestures: one movement = from "starts moving" to "still again", recorded with the
    //    heading at the start removed (so it does not matter which way you face).
    void update_gestures(const Vector3f &w, bool was_still) {
        if (was_still && !still) { rec_.clear(); yaw0_ = heading(); recording_ = true; }
        if (recording_ && !still) {
            Vector3f l = Eigen::AngleAxisf(-yaw0_, Vector3f::UnitZ()) * lin;
            if (rec_.size() < 800) rec_.push_back({l.x(), l.y(), l.z(), w.x() * 3, w.y() * 3, w.z() * 3});
        }
        if (!was_still && still && recording_) { recording_ = false; finish_movement(); }
    }
    void finish_movement() {
        if (rec_.size() > 40) rec_.resize(rec_.size() - 40); else rec_.clear();   // drop the still window
        if (rec_.size() < 50) return;                                   // under 0.25 s: a knock
        GestureVec g;
        if (!gesture_normalise(rec_, g)) return;
        if (record_slot >= 0 && record_slot < GEST_SLOTS) { gestures[record_slot].push_back(g); record_slot = -1; return; }
        if (mode != Mode::AIR) return;                                  // table movements are strokes
        int best = -1; float bd = 1e9f;
        for (int s = 0; s < GEST_SLOTS; s++)
            for (auto &t : gestures[s]) { float d = gesture_distance(g, t); if (d < bd) { bd = d; best = s; } }
        if (best >= 0 && bd <= p.gesture_max_dist) { last_gesture = best; last_gesture_dist = bd; gesture_count++; }
        else if (best >= 0) { last_gesture = -1; last_gesture_dist = bd; }
    }

    // Spread of the window (max - min per axis): usable before any calibration.
    bool window_is_quiet() const {
        Vector3f amin = win_[0].a, amax = amin, wmin = win_[0].w, wmax = wmin;
        for (auto &s : win_) {
            amin = amin.cwiseMin(s.a); amax = amax.cwiseMax(s.a);
            wmin = wmin.cwiseMin(s.w); wmax = wmax.cwiseMax(s.w);
        }
        return (amax - amin).maxCoeff() < p.still_acc_g && (wmax - wmin).maxCoeff() * DEG < p.still_gyro_dps;
    }
    // Quiet, and (a steady turn or push has no spread either) once the bias and 1 g are known,
    // the means must agree with them too.
    bool window_is_still() const {
        if (win_.size() < 40 || !window_is_quiet()) return false;
        Vector3f wm = Vector3f::Zero(); float am = 0;
        for (auto &s : win_) { wm += s.w; am += s.a.norm(); }
        wm /= (float)win_.size(); am /= (float)win_.size();
        if (have_bias_ && (wm - gyro_bias).norm() * DEG >= p.still_gyro_dps) return false;
        return std::fabs(am - (have_bias_ ? g_ref : 1.0f)) < (have_bias_ ? p.still_acc_g : 0.15f);
    }
};

// ---- Save / load: tuning numbers, calibration and gestures, as text.
inline bool save(const Tracker &t, const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto &pi : param_table()) std::fprintf(f, "%s %g\n", pi.name, t.p.*(pi.field));
    std::fprintf(f, "acc_cal %g %g %g %g %g %g\n", t.acc_off.x(), t.acc_off.y(), t.acc_off.z(), t.acc_scale.x(), t.acc_scale.y(), t.acc_scale.z());
    std::fprintf(f, "gyro_bias %g %g %g\n", t.gyro_bias.x(), t.gyro_bias.y(), t.gyro_bias.z());
    if (t.table_calibrated) std::fprintf(f, "table_up %g %g %g\n", t.table_up.x(), t.table_up.y(), t.table_up.z());
    for (int s = 0; s < GEST_SLOTS; s++)
        for (auto &g : t.gestures[s]) {
            std::fprintf(f, "gesture %d", s);
            for (float v : g) std::fprintf(f, " %.4f", v);
            std::fprintf(f, "\n");
        }
    std::fclose(f);
    return true;
}

inline bool load(Tracker &t, const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char key[64];
    float v[6];
    while (std::fscanf(f, "%63s", key) == 1) {
        std::string k = key;
        if (k == "table_up") {
            if (std::fscanf(f, "%f %f %f", &v[0], &v[1], &v[2]) == 3) { t.table_up = Vector3f(v[0], v[1], v[2]).normalized(); t.table_calibrated = true; }
        } else if (k == "acc_cal") {
            if (std::fscanf(f, "%f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6 && v[3] > 0.5f && v[4] > 0.5f && v[5] > 0.5f) {
                t.acc_off = Vector3f(v[0], v[1], v[2]); t.acc_scale = Vector3f(v[3], v[4], v[5]);
            }
        } else if (k == "gyro_bias") {
            if (std::fscanf(f, "%f %f %f", &v[0], &v[1], &v[2]) == 3) t.gyro_bias = Vector3f(v[0], v[1], v[2]);
        } else if (k == "gesture") {
            int s; GestureVec g; bool ok = std::fscanf(f, "%d", &s) == 1;
            for (float &x : g) ok = ok && std::fscanf(f, "%f", &x) == 1;
            if (ok && s >= 0 && s < GEST_SLOTS) t.gestures[s].push_back(g);
        } else {
            if (std::fscanf(f, "%f", &v[0]) != 1) break;
            for (auto &pi : param_table()) if (k == pi.name) t.p.*(pi.field) = v[0];
        }
    }
    std::fclose(f);
    return true;
}

} // namespace motion
