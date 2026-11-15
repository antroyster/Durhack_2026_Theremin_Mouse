// fx.hpp - guitar-pedal effects for the theremin tone. Pure maths, so test_fx.cpp can check it.
//
//   note Hz --> oscillator --> delay --> reverb --> soft limiter --> speaker
//               (shape)        (echo)    (room)
//
// A click switches a preset on; you hear it exactly as set. (Amount A glides 0 -> 1 in ~25 ms
// so it does not click; A = 0 is the clean sine. Shape and both mixes are scaled by A.)
// The joystick bends the wave around the preset, b = joy_depth * u (u = stick, -1..+1):
//   b > 0 (up):   skew m = 0.5 + 0.45 b       -> brighter (square -> narrow pulse)
//   b < 0 (down): blend each wave towards its own fundamental by |b|  -> softer, pure sine at -1
// (Skewing both ways would sound the same both ways: a wave skewed to m is the time mirror of
// one skewed to 1 - m, and the ear cannot tell a wave from its mirror image.)
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fx {

constexpr int SR = 44100;                          // audio samples per second
constexpr int NPRESET = 4;
constexpr float TWO_PI = 6.2831853f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// One pedal preset: every number the effects menu edits.
struct Preset {
    float shape = 0;          // 0 sine, 1 triangle, 2 saw, 3 square; in between blends neighbours
    float delay_ms = 350;     // time between echoes
    float delay_fb = 0.35f;   // each echo is this fraction of the one before
    float delay_mix = 0;      // loudness of the echoes
    float reverb_size = 0.7f; // longer tail
    float reverb_damp = 0.3f; // 0 = bright, 1 = dull
    float reverb_mix = 0;     // loudness of the room
    float joy_depth = 0.5f;   // how far a full joystick push bends the wave (0 = not at all)
};

struct FieldInfo { const char *name; float Preset::*field; float step, lo, hi; };
inline const std::vector<FieldInfo> &field_table() {
    static const std::vector<FieldInfo> t = {
        {"shape", &Preset::shape, 0.1f, 0, 3},                {"delay_ms", &Preset::delay_ms, 25, 10, 1900},
        {"delay_fb", &Preset::delay_fb, 0.05f, 0, 0.95f},     {"delay_mix", &Preset::delay_mix, 0.05f, 0, 1},
        {"reverb_size", &Preset::reverb_size, 0.05f, 0, 1},   {"reverb_damp", &Preset::reverb_damp, 0.05f, 0, 1},
        {"reverb_mix", &Preset::reverb_mix, 0.05f, 0, 1},     {"joy_depth", &Preset::joy_depth, 0.05f, 0, 1},
    };
    return t;
}

inline std::array<Preset, NPRESET> default_presets() {
    std::array<Preset, NPRESET> p{};
    p[0].shape = 2.0f; p[0].reverb_mix = 0.2f;                                                     // 1 saw lead
    p[1].shape = 1.0f; p[1].delay_ms = 350; p[1].delay_fb = 0.45f; p[1].delay_mix = 0.5f; p[1].reverb_mix = 0.15f;   // 2 echo
    p[2].shape = 0.5f; p[2].delay_ms = 500; p[2].delay_fb = 0.3f; p[2].delay_mix = 0.25f;
    p[2].reverb_size = 0.9f; p[2].reverb_damp = 0.4f; p[2].reverb_mix = 0.7f;                      // 3 space
    p[3].shape = 3.0f; p[3].reverb_mix = 0.1f;                                                     // 4 square
    return p;
}

// ---- Oscillator. ph = how far through one cycle, 0..1.
inline float basic_wave(int k, float ph) {
    switch (k) {
        case 0:  return std::sin(TWO_PI * ph);                 // sine
        case 1:  return 1.0f - 4.0f * std::fabs(ph - 0.5f);    // triangle: -1 at 0, +1 at 0.5
        case 2:  return 2.0f * ph - 1.0f;                      // saw: -1 -> +1
        default: return ph < 0.5f ? 1.0f : -1.0f;              // square
    }
}
// The fundamental of each basic wave as a unit sine in step with it (from the Fourier series):
// sine sin, triangle -cos, saw -sin, square +sin. Blending towards THIS sine removes harmonics;
// a plain sin() would partly cancel the saw's own fundamental and make it buzzier.
inline float fundamental(int k, float ph) {
    return k == 1 ? -std::cos(TWO_PI * ph) : (k == 2 ? -std::sin(TWO_PI * ph) : std::sin(TWO_PI * ph));
}
// Skew: the first half of the cycle is squeezed into 0..m, the second stretched over m..1.
//   ph' = 0.5 ph / m  (ph < m),   0.5 + 0.5 (ph - m) / (1 - m)  (ph >= m);   m = 0.5: no change.
inline float skew_phase(float ph, float m) {
    m = clampf(m, 0.02f, 0.98f);
    return ph < m ? 0.5f * ph / m : 0.5f + 0.5f * (ph - m) / (1 - m);
}
// PolyBLEP: rounds off a jump of height 2 over one sample either side, so the saw and square do
// not alias (fold high harmonics back as whistles). dt = Hz / SR.
inline float poly_blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1; }
    if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
    return 0;
}
// Shape s (0..3: i = floor(s), f = s - i, (1 - f) wave_i + f wave_i+1), skewed to m and softened
// by soft. dt > 0 smooths the jumps at ph = 0 (saw, square) and ph = m (square): the sound.
// dt = 0 draws the wave exactly: the picture.
inline float wave(float s, float ph, float m = 0.5f, float soft = 0, float dt = 0) {
    s = clampf(s, 0, 3);
    m = clampf(m, 0.02f, 0.98f);
    int i = (int)s; float f = s - i;
    float phs = skew_phase(ph, m);
    auto one = [&](int k) {
        float v = basic_wave(k, phs);
        if (k == 2) v -= poly_blep(ph, dt);
        if (k == 3) { float p2 = ph - m; if (p2 < 0) p2 += 1; v += poly_blep(ph, dt) - poly_blep(p2, dt); }
        return (1 - soft) * v + soft * fundamental(k, ph);
    };
    if (i >= 3) return one(3);
    return (1 - f) * one(i) + f * one(i + 1);
}
// The wave for a joystick bend b (-1..+1), as described at the top of the file.
inline float bent_wave(float s, float ph, float b, float dt = 0) {
    return wave(s, ph, 0.5f + 0.45f * (b > 0 ? b : 0), b < 0 ? -b : 0, dt);
}

// ---- Delay: a 2 s ring buffer. Echoes at d, 2d, 3d ... samples with heights 1, fb, fb^2 ...
class Delay {
public:
    Delay() : buf_(2 * SR, 0.0f) {}
    float process(float x, float d_samples, float fb) {           // returns the echo signal
        int n = (int)buf_.size();
        d_samples = clampf(d_samples, 1, (float)(n - 2));
        float rp = (float)w_ - d_samples;                          // read point, d samples back
        if (rp < 0) rp += n;
        int i0 = (int)rp, i1 = (i0 + 1) % n; float fr = rp - i0;
        float wet = buf_[i0] * (1 - fr) + buf_[i1] * fr;          // interpolated: smooth time changes
        buf_[w_] = x + fb * wet;
        w_ = (w_ + 1) % n;
        return wet;
    }
private:
    std::vector<float> buf_;
    int w_ = 0;
};

// ---- Reverb (Schroeder / Freeverb style): four feedback combs in parallel (the room's echoes,
// at unrelated spacings so they blur into a tail), then two all-pass filters in series (denser
// echoes, no colouring).   comb: y[n] = x[n - D] + g lowpass(y[n - D]),  g = 0.70 + 0.28 size.
class Reverb {
public:
    Reverb() {
        static const int cd[4] = {1557, 1617, 1491, 1422}, ad[2] = {556, 441};
        for (int i = 0; i < 4; i++) comb_[i].assign(cd[i], 0.0f);
        for (int i = 0; i < 2; i++) ap_[i].assign(ad[i], 0.0f);
    }
    float process(float x, float size, float damp) {              // returns the room signal
        float g = 0.70f + 0.28f * size;
        float in = x * (1 - g) * 2.0f;                             // a held note stays about as loud
        float sum = 0;
        for (int i = 0; i < 4; i++) {
            float y = comb_[i][ci_[i]];
            filt_[i] = y * (1 - damp) + filt_[i] * damp;
            comb_[i][ci_[i]] = in + g * filt_[i];
            ci_[i] = (ci_[i] + 1) % (int)comb_[i].size();
            sum += y;
        }
        float out = sum * 0.25f;
        for (int i = 0; i < 2; i++) {
            float b = ap_[i][ai_[i]], o = -out + b;
            ap_[i][ai_[i]] = out + b * 0.5f;
            ai_[i] = (ai_[i] + 1) % (int)ap_[i].size();
            out = o;
        }
        return out;
    }
private:
    std::vector<float> comb_[4], ap_[2];
    int ci_[4] = {0, 0, 0, 0}, ai_[2] = {0, 0};
    float filt_[4] = {0, 0, 0, 0};
};

// Soft limiter: a straight line up to 0.5, then a smooth bend that never passes 1.
inline float soft_limit(float x) {
    float a = std::fabs(x);
    if (a <= 0.5f) return x;
    float y = 0.5f + 0.5f * std::tanh((a - 0.5f) / 0.5f);
    return x < 0 ? -y : y;
}

// ---- The whole chain, one sample at a time, on the audio thread. Frequency, loudness, amount,
// bend and delay time all glide towards their targets, so nothing clicks.
class Engine {
public:
    Preset target;              // set by the program under the audio lock
    float target_amount = 0;    // 0 = clean, 1 = preset on
    float target_bend = 0;      // joystick bend, -1..+1
    float level = 0.25f;        // loudness while a note plays

    float next(float hz_target) {                                  // hz_target 0 = silent
        amp_ += ((hz_target > 0 ? level : 0.0f) - amp_) * 0.002f;
        if (hz_target > 0) hz_ += (hz_target - hz_) * 0.005f;
        amount_ += (target_amount - amount_) * 0.001f;
        bend_ += (target_bend - bend_) * 0.002f;                    // the stick updates only 5 times a second
        d_samp_ += (target.delay_ms * 0.001f * SR - d_samp_) * 0.0005f;
        float dt = hz_ / SR;
        phase_ += dt;
        if (phase_ >= 1) phase_ -= 1;
        float dry = amp_ * bent_wave(amount_ * target.shape, phase_, amount_ * bend_, dt);   // clean = no bend
        float y = dry + amount_ * target.delay_mix * delay_.process(dry, d_samp_, target.delay_fb);
        y += amount_ * target.reverb_mix * reverb_.process(y, target.reverb_size, target.reverb_damp);
        return soft_limit(y);
    }
    // Jump straight to a state (tests).
    void settle(float hz, float amount, float bend = 0) {
        hz_ = hz; amount_ = target_amount = amount; bend_ = target_bend = bend;
        amp_ = hz > 0 ? level : 0; d_samp_ = target.delay_ms * 0.001f * SR;
    }
private:
    Delay delay_;
    Reverb reverb_;
    float phase_ = 0, hz_ = 220, amp_ = 0, amount_ = 0, bend_ = 0, d_samp_ = 0.35f * SR;
};

// ---- The pedalboard: which click switches which preset, and the joystick bend.
struct Board {
    std::array<Preset, NPRESET> presets = default_presets();
    int btn_preset[2] = {1, 2};   // left, right click -> preset 1..4 (0 = nothing)
    int active = 0;               // preset on now, 1..4; 0 = clean
    uint8_t last_buttons = 0;

    // buttons: bit0 left, bit1 right (the Pico's "evt buttons=0x.." byte). A press switches its
    // preset on, a second press back to clean. Not enabled (mouse mode): followed, nothing switches.
    void on_buttons(uint8_t buttons, bool enabled) {
        uint8_t pressed = buttons & ~last_buttons;                 // rising edges
        last_buttons = buttons;
        if (!enabled) return;
        for (int b = 0; b < 2; b++) {
            int p = btn_preset[b];
            if ((pressed & (1u << b)) && p >= 1 && p <= NPRESET) active = (active == p) ? 0 : p;
        }
    }
    // Stick position u, -1 (down) .. +1 (up): (raw - v0) / (4095 - v0) above the centre,
    // (raw - v0) / v0 below, |u| < 0.1 = 0 (the Pico's scroll rule). No reading yet: 0.
    static float joy_u(int raw, int v0) {
        if (raw < 0 || v0 <= 0 || v0 >= 4095) return 0;
        float u = raw >= v0 ? (float)(raw - v0) / (4095 - v0) : (float)(raw - v0) / v0;
        if (std::fabs(u) < 0.1f) u = 0;
        return clampf(u, -1, 1);
    }
    static float bend_from_joy(int raw, int v0, float depth) { return depth * joy_u(raw, v0); }
};

// ---- Save / load as text: "fx_preset2 i <numbers in field_table order>", "fx_buttons L R".
// Older "fx_preset i" lines: the first 7 numbers are read, the rest (old joy_min / joy_max) dropped.
inline bool save(const Board &b, const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    for (int i = 0; i < NPRESET; i++) {
        std::fprintf(f, "fx_preset2 %d", i + 1);
        for (auto &fi : field_table()) std::fprintf(f, " %g", b.presets[i].*(fi.field));
        std::fprintf(f, "\n");
    }
    std::fprintf(f, "fx_buttons %d %d\n", b.btn_preset[0], b.btn_preset[1]);
    std::fclose(f);
    return true;
}
inline bool load(Board &b, const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[512];
    while (std::fgets(line, sizeof line, f)) {
        char key[32]; int used = 0, i, n, l, r;
        if (std::sscanf(line, "%31s%n", key, &used) != 1) continue;
        std::string k = key; const char *rest = line + used;
        if (k == "fx_preset" || k == "fx_preset2") {
            if (std::sscanf(rest, "%d%n", &i, &n) != 1 || i < 1 || i > NPRESET) continue;
            rest += n;
            int count = 0, max_fields = (k == "fx_preset") ? 7 : 1000;
            for (auto &fi : field_table()) {
                float x;
                if (count++ >= max_fields || std::sscanf(rest, "%f%n", &x, &n) != 1) break;   // short line: keep defaults
                rest += n;
                b.presets[i - 1].*(fi.field) = clampf(x, fi.lo, fi.hi);
            }
        } else if (k == "fx_buttons" && std::sscanf(rest, "%d %d", &l, &r) == 2) {
            if (l >= 0 && l <= NPRESET) b.btn_preset[0] = l;
            if (r >= 0 && r <= NPRESET) b.btn_preset[1] = r;
        }
    }
    std::fclose(f);
    return true;
}

} // namespace fx
