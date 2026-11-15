// test_fx.cpp - checks fx.hpp (waves, delay, reverb, amount, pedalboard) with known inputs.
//   make test
#include "fx.hpp"
extern "C" {
#include "../tests/check.h"
}

using namespace fx;

static double energy(const std::vector<float> &v, int from, int to) {
    double e = 0;
    for (int i = from; i < to && i < (int)v.size(); i++) e += (double)v[i] * v[i];
    return e;
}

int main() {
    {   // 1. Waves: the saw is 2 ph - 1, the square is +1 then -1, shape 2.5 is half saw, half square.
        check_range("saw at ph = 0.25 (ideal -0.5)", wave(2, 0.25f), -0.5001, -0.4999);
        check_range("saw at ph = 0.75 (ideal +0.5)", wave(2, 0.75f), 0.4999, 0.5001);
        check_eq("square at ph = 0.25", wave(3, 0.25f), 1);
        check_eq("square at ph = 0.75", wave(3, 0.75f), -1);
        check_range("triangle at ph = 0.5 (peak +1)", wave(1, 0.5f), 0.9999, 1.0001);
        check_range("shape 2.5 at ph = 0.25: (saw + square) / 2 = 0.25", wave(2.5f, 0.25f), 0.2499, 0.2501);
        check_range("sound saw away from the jump = drawn saw", wave(2, 0.25f, 0.5f, 0, 0.01f), -0.5001, -0.4999);
        check_range("sound saw at the jump is smoothed (|v| < 1)", std::fabs(wave(2, 0.001f, 0.5f, 0, 0.01f)), 0, 0.99);
    }
    {   // 2. Delay of 100 samples, feedback 0.5: an impulse comes back at 100 (height 1) and 200 (height 0.5).
        Delay d; std::vector<float> w;
        for (int i = 0; i < 400; i++) w.push_back(d.process(i == 0 ? 1.0f : 0.0f, 100, 0.5f));
        check_range("delay: first echo at sample 100, height", w[100], 0.999, 1.001);
        check_range("delay: second echo at sample 200, height = fb", w[200], 0.499, 0.501);
        check_range("delay: nothing between the echoes", std::fabs(w[150]), 0, 1e-6);
    }
    {   // 3. Reverb: an impulse gives a tail that is there after 0.1 s and has died down by 1.5 s.
        Reverb r; std::vector<float> w; bool finite = true;
        for (int i = 0; i < 2 * SR; i++) { float y = r.process(i == 0 ? 1.0f : 0.0f, 0.7f, 0.3f); finite = finite && std::isfinite(y); w.push_back(y); }
        double early = energy(w, SR / 10, SR / 10 + SR / 5), late = energy(w, 3 * SR / 2, 3 * SR / 2 + SR / 5);
        check_eq("reverb: all output finite (1 = yes)", finite, 1);
        check_range("reverb: tail energy 0.1..0.3 s", early, 1e-5, 1e9);
        check_range("reverb: tail 1.5..1.7 s / tail 0.1..0.3 s (decays)", late / early, 0, 0.1);
        Reverb big; std::vector<float> wb;
        for (int i = 0; i < 2 * SR; i++) wb.push_back(big.process(i == 0 ? 1.0f : 0.0f, 1.0f, 0.3f));
        check_range("reverb: bigger room = longer tail (late energy ratio)", energy(wb, 3 * SR / 2, 3 * SR / 2 + SR / 5) / std::max(late, 1e-30), 2, 1e30);
    }
    {   // 4. Amount 0 = exactly the clean tone, whatever the preset says.
        Engine clean, heavy;
        heavy.target = default_presets()[2];                         // "space": delay + big reverb
        clean.settle(440, 0); heavy.settle(440, 0);
        double maxd = 0, peak = 0;
        for (int i = 0; i < SR; i++) {
            float a = clean.next(440), b = heavy.next(440);
            maxd = std::max(maxd, (double)std::fabs(a - b)); peak = std::max(peak, (double)std::fabs(a));
        }
        check_range("amount 0: heavy preset == clean tone (max difference)", maxd, 0, 1e-6);
        check_range("clean tone peak = level 0.25", peak, 0.24, 0.26);
        // Amount 1 with the saw preset: the sound is a saw, not a sine.
        Engine saw; saw.target = default_presets()[0]; saw.target.reverb_mix = 0; saw.settle(441, 1);
        std::vector<float> w; for (int i = 0; i < 1000; i++) w.push_back(saw.next(441));
        // 441 Hz = 100 samples per cycle; a quarter of the way through a cycle the saw is -0.5 * 0.25.
        double err = 0; for (int i = 210; i < 290; i++) err = std::max(err, std::fabs(w[i] - 0.25 * (2.0 * (((i + 1) % 100) / 100.0) - 1)));
        check_range("amount 1, saw preset: output follows the saw ramp", err, 0, 0.01);
        // Echo preset at amount 1: 350 ms after a note starts, the echo has arrived (louder than the dry tone alone).
        Engine echo; echo.target = default_presets()[1]; echo.target.reverb_mix = 0; echo.target.shape = 0; echo.settle(440, 1);
        std::vector<float> e; for (int i = 0; i < SR; i++) e.push_back(echo.next(i < SR / 10 ? 440 : 0));
        int d = (int)(0.35f * SR);
        check_range("echo preset: sound comes back 350 ms after the note", energy(e, d + 2000, d + 4000), 0.5, 1e9);
        check_range("echo preset: silence just before the echo", energy(e, d - 3000, d - 1000), 0, 1e-3);
    }
    {   // 5. The pedalboard: a click switches its preset on, a second click goes back to clean.
        Board b;
        b.on_buttons(0x01, true);  check_eq("left click: preset 1 on", b.active, 1);
        b.on_buttons(0x00, true);  check_eq("left released: still preset 1", b.active, 1);
        b.on_buttons(0x02, true);  check_eq("right click: preset 2 on", b.active, 2);
        b.on_buttons(0x00, true);
        b.on_buttons(0x02, true);  check_eq("right click again: back to clean (0)", b.active, 0);
        b.on_buttons(0x00, true);
        b.on_buttons(0x01, false); check_eq("mouse mode: a click switches nothing", b.active, 0);
        b.on_buttons(0x00, false);
        b.btn_preset[0] = 4;
        b.on_buttons(0x01, true);  check_eq("left set to preset 4: click gives 4", b.active, 4);
        check_range("joystick at centre: bend 0 (the preset, unbent)", Board::bend_from_joy(2048, 2048, 1), 0, 0);
        check_range("joystick inside the dead zone: still 0", Board::bend_from_joy(2200, 2048, 1), 0, 0);
        check_range("depth 1, full up: bend +1", Board::bend_from_joy(4095, 2048, 1), 0.9999, 1.0001);
        check_range("depth 0.5, full down: bend -0.5", Board::bend_from_joy(0, 2048, 0.5f), -0.5001, -0.4999);
        check_range("depth 0: the joystick does nothing", Board::bend_from_joy(4095, 2048, 0), 0, 0);
        check_range("no joystick reading yet: 0", Board::bend_from_joy(-1, -1, 1), 0, 0);
    }
    {   // 5b. The bend (skew m): square -> pulse of width m, triangle peak moves to m, m = 0.5 changes nothing.
        check_eq("m 0.8: square still high at ph 0.7", wave(3, 0.7f, 0.8f), 1);
        check_eq("m 0.8: square low at ph 0.85", wave(3, 0.85f, 0.8f), -1);
        check_range("m 0.9: triangle peak moved to ph 0.9", wave(1, 0.9f, 0.9f), 0.9999, 1.0001);
        double md = 0; for (int i = 0; i < 100; i++) { float ph = i / 100.0f; md = std::max(md, (double)std::fabs(wave(0.7f, ph, 0.5f) - wave(0.7f, ph))); }
        check_range("m 0.5: wave identical to no bend (max difference)", md, 0, 1e-6);
        // In the engine: square preset at 441 Hz (100 samples a cycle). Up: pulse of width m.
        auto duty = [](float b) {
            Engine e; e.target = default_presets()[3]; e.target.reverb_mix = 0; e.settle(441, 1, b);
            int hi = 0, n = 0; for (int i = 0; i < 4000; i++) { float y = e.next(441); if (i >= 2000) { n++; hi += y > 0; } }
            return (double)hi / n;
        };
        check_range("square preset, centre (bend 0): high half the cycle", duty(0), 0.48, 0.52);
        check_range("square preset, stick up (bend 2/3 -> m 0.8): high 80% of the cycle", duty(2.0f / 3), 0.77, 0.83);
        // Up and down must sound different: harmonics (energy above the note) up > centre > down.
        auto harm = [](float b) {
            Engine e; e.target = default_presets()[3]; e.target.reverb_mix = 0; e.settle(441, 1, b);
            double tot = 0, c = 0, s2 = 0; for (int i = 0; i < 4410; i++) e.next(441);
            for (int i = 0; i < 4410; i++) { double y = e.next(441); tot += y * y; c += y * std::cos(TWO_PI * 441 * i / SR); s2 += y * std::sin(TWO_PI * 441 * i / SR); }
            double fund = 2 * (c * c + s2 * s2) / 4410;                 // energy in the 441 Hz part: A^2 N / 2, A = 2 |c, s| / N
            return std::sqrt(std::max(0.0, tot - fund) / fund);
        };
        double hd = harm(-0.5f), hc = harm(0), hu = harm(0.5f);
        check_range("square: harmonics at centre (ideal 0.48)", hc, 0.40, 0.52);
        check_range("square, stick up: brighter than centre", hu - hc, 0.1, 10);
        check_range("square, stick down: softer than centre", hc - hd, 0.1, 10);
        check_range("square, full down at depth 1: a pure sine (harmonics)", harm(-1), 0, 0.01);
        // The same on the saw preset: down must soften it (blending towards an out-of-step sine would not).
        auto harm_saw = [](float b) {
            Engine e; e.target = default_presets()[0]; e.target.reverb_mix = 0; e.settle(441, 1, b);
            double tot = 0, c = 0, s2 = 0; for (int i = 0; i < 4410; i++) e.next(441);
            for (int i = 0; i < 4410; i++) { double y = e.next(441); tot += y * y; c += y * std::cos(TWO_PI * 441 * i / SR); s2 += y * std::sin(TWO_PI * 441 * i / SR); }
            double fund = 2 * (c * c + s2 * s2) / 4410;
            return std::sqrt(std::max(0.0, tot - fund) / fund);
        };
        check_range("saw, stick down: softer than centre", harm_saw(0) - harm_saw(-0.5f), 0.1, 10);
        check_range("saw, full down at depth 1: a pure sine (harmonics)", harm_saw(-1), 0, 0.01);
        // Switched off (amount 0) the bend is ignored: still exactly the clean sine.
        Engine c0, c1; c1.target = default_presets()[3]; c0.settle(440, 0); c1.settle(440, 0, 0.9f);
        double d = 0; for (int i = 0; i < 4000; i++) d = std::max(d, (double)std::fabs(c0.next(440) - c1.next(440)));
        check_range("pedal off: the joystick bend is ignored (max difference)", d, 0, 1e-6);
    }
    {   // 6. Save and load give the same board back.
        Board a; a.presets[2].delay_ms = 777; a.presets[0].shape = 2.7f; a.btn_preset[0] = 3; a.btn_preset[1] = 0;
        a.presets[1].joy_depth = 0.35f;
        const char *path = "build/test_fx.cfg";
        check_eq("fx save (1 = ok)", save(a, path), 1);
        Board b; check_eq("fx load (1 = ok)", load(b, path), 1);
        check_range("loaded preset 3 delay_ms", b.presets[2].delay_ms, 777, 777);
        check_range("loaded preset 1 shape", b.presets[0].shape, 2.69, 2.71);
        check_eq("loaded left button preset", b.btn_preset[0], 3);
        check_eq("loaded right button preset", b.btn_preset[1], 0);
        check_range("loaded preset 2 joy_depth", b.presets[1].joy_depth, 0.3499, 0.3501);
        // Older files: "fx_preset" with 7 numbers (first version) or 9 (with joy_min / joy_max).
        FILE *f = std::fopen("build/test_fx_old.cfg", "w");
        std::fprintf(f, "fx_preset 1 2.5 400 0.3 0.4 0.6 0.2 0.3\nfx_preset 2 1 350 0.45 0.5 0.7 0.3 0.15 0.1 0.9\nfx_buttons 2 1\n"); std::fclose(f);
        Board c;
        check_eq("old-format file loads (1 = ok)", load(c, "build/test_fx_old.cfg"), 1);
        check_range("old 7-number line: preset 1 reverb_mix read", c.presets[0].reverb_mix, 0.2999, 0.3001);
        check_range("old 9-number line: preset 2 reverb_mix read", c.presets[1].reverb_mix, 0.1499, 0.1501);
        check_range("old 9-number line: old joy_min NOT taken as joy_depth", c.presets[1].joy_depth, 0.4999, 0.5001);
        check_eq("old file: buttons read", c.btn_preset[0], 2);
    }
    return check_done();
}
