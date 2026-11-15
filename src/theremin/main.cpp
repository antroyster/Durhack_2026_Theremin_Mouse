// theremin - live 3D view of the Theremin Mouse IMU: table-mode cursor, tilt/height notes,
// guitar-pedal effects and recordable gestures.
//
//   build/theremin [port] [--cfg FILE] [--replay FILE.csv] [--live SECONDS]
//
//   Pico --USB CDC--> imu_reader --> motion::Tracker --> 3D view (freeglut/OpenGL)
//     ^                                   |  |  '------> tone + effects (fx.hpp, SDL2 audio)
//     '------- "@M dx dy" cursor ---------'  '---------> gestures
//
// The cursor goes back to the Pico, which adds it to its USB mouse report, so the PC still sees
// one ordinary mouse. The Pico also reports its buttons ("evt buttons=0x..") and joystick
// ("joy raw=.. v0=..") as log lines; in theremin mode they drive the effects.
#include <GL/freeglut.h>
#include <SDL2/SDL.h>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <glob.h>
#include <string>
#include <unistd.h>

#include "fx.hpp"
#include "motion.hpp"
#define _Static_assert static_assert   // protocol.h is C; this is C++'s spelling of the same check
extern "C" {
#include "imu_reader.h"
}

using namespace motion;

// ---- State ---------------------------------------------------------------------------------
static Tracker g_t;
static std::string g_port, g_cfg = "theremin.cfg", g_fx_cfg, g_msg = "Lay the device on the table and press C";
static std::string g_status = "(waiting for the Pico)", g_health = "";
static int g_fd = -1, g_sel = 0, g_joy_raw = -1, g_joy_v0 = -1;
static bool g_cursor_on = false, g_sound_on = false;
static bool g_lock_pos = true;                // 3D view: rotation only, box fixed at the axis centre (P)
static bool g_theremin_mode = false;          // T: mouse mode (cursor) / theremin mode (sound, pedal)
static uint32_t g_last_t_us = 0, g_seen_gestures = 0;
static bool g_have_t = false;
static long g_frames = 0, g_sent_x = 0, g_sent_y = 0;
static float g_rate_hz = 0;                   // measured sample rate
static std::atomic<float> g_hz{0};            // note for the audio thread, 0 = silent

static fx::Board g_board;
static fx::Engine g_engine;                   // audio thread; changed only under SDL_LockAudio
static bool g_audio_ok = false;
static int g_menu = 0;                        // bottom-left panel: 0 TUNING, 1 EFFECTS (Tab)
static int g_fx_sel = 0, g_fx_edit = 0;       // effects menu: selected row, preset being edited
static int g_btn_now = 0;                     // last button byte: bit0 left, bit1 right, bit2 middle
static long g_btn_presses[3] = {0, 0, 0};     // presses per button, so a quick tap still shows
static bool g_btn_seen = false;               // any button event yet?

static const char *FILTER_NAMES[3] = {"complementary", "Mahony", "Madgwick"};
static const char *CURSOR_NAMES[3] = {"strokes (acceleration, table only)", "air mouse (rotation rate)", "tilt = speed"};
static const char *SOURCE_NAMES[3] = {"height", "tilt of the green axis (red nose = volume)", "tilt of the red nose (green axis = volume)"};

// ---- Small helpers -------------------------------------------------------------------------
static std::string fmt(const char *f, ...) {
    char b[256];
    va_list ap; va_start(ap, f); std::vsnprintf(b, sizeof b, f, ap); va_end(ap);
    return b;
}
static std::string chomp(std::string s) { while (!s.empty() && s.back() == '\n') s.pop_back(); return s; }
static std::string note_name(float hz) {                     // e.g. "C#5"
    static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int midi = (int)std::lround(69 + 12 * std::log2(hz / 440.0f));
    return std::string(names[((midi % 12) + 12) % 12]) + std::to_string(midi / 12 - 1);
}
static int clamp_index(float v, int hi) { return std::max(0, std::min(hi, (int)v)); }

// ---- Audio ---------------------------------------------------------------------------------
static void audio_cb(void *, Uint8 *stream, int len) {       // SDL's audio thread
    float target = g_hz.load();
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < len / 2; i++) out[i] = (int16_t)(32767 * g_engine.next(target));
}
// Joystick bend for preset p: only in theremin mode (in mouse mode the stick just scrolls).
static float fx_bend(const fx::Preset &p) {
    return g_theremin_mode ? fx::Board::bend_from_joy(g_joy_raw, g_joy_v0, p.joy_depth) : 0.0f;
}
// Hand the active preset, the bend and the volume to the audio thread. Off = amount 0 = the clean
// tone. Volume 0.5 = level 0.25, the loudness before volume control existed.
static void update_fx() {
    if (!g_audio_ok) return;
    SDL_LockAudio();
    g_engine.level = 0.5f * g_t.volume;
    if (g_board.active) g_engine.target = g_board.presets[g_board.active - 1];
    g_engine.target_amount = g_board.active ? 1.0f : 0.0f;
    g_engine.target_bend = g_board.active ? fx_bend(g_board.presets[g_board.active - 1]) : 0.0f;
    SDL_UnlockAudio();
}

// ---- IMU input -----------------------------------------------------------------------------
static std::string find_port() {
    glob_t g; std::string r;
    if (glob("/dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00", 0, nullptr, &g) == 0 && g.gl_pathc > 0) r = g.gl_pathv[0];
    globfree(&g);
    return r;
}

static void on_buttons(unsigned btn) {                       // "evt buttons=0x.." from the Pico
    for (int i = 0; i < 3; i++) if ((btn & ~g_btn_now & 0x7) & (1u << i)) g_btn_presses[i]++;
    g_btn_now = (int)btn; g_btn_seen = true;
    int before = g_board.active;
    g_board.on_buttons((uint8_t)btn, g_theremin_mode);
    if (g_board.active != before)
        g_msg = g_board.active ? fmt("Pedal: preset %d ON (joystick bends the wave)", g_board.active) : "Pedal: off, clean tone";
}
static void on_log(void *, const imu_log_t *log) {
    int raw, v0; unsigned btn;
    if (std::sscanf(log->text, "joy raw=%d v0=%d", &raw, &v0) == 2) { g_joy_raw = raw; g_joy_v0 = v0; }
    else if (std::sscanf(log->text, "evt buttons=0x%x", &btn) == 1) on_buttons(btn);
    else if (std::strncmp(log->text, "imuhealth", 9) == 0) g_health = chomp(log->text);
    else if (std::strncmp(log->text, "hello", 5) == 0) g_status = chomp(log->text);
}

static void feed(const imu_sample_t &s) {
    float dt = 0.005f;
    if (g_have_t) dt = (uint32_t)(s.t_us - g_last_t_us) * 1e-6f;   // unsigned: survives the 71-minute wrap
    g_last_t_us = s.t_us; g_have_t = true;
    g_t.update(s.a, s.g, dt);
    g_frames++;
    if (dt > 0) g_rate_hz += 0.02f * (1.0f / dt - g_rate_hz);
    if (g_t.gesture_count != g_seen_gestures) {
        g_seen_gestures = g_t.gesture_count;
        g_msg = fmt("Gesture %d recognised (distance %.2f)", g_t.last_gesture + 1, g_t.last_gesture_dist);
        if (g_t.last_gesture == 0) g_sound_on = !g_sound_on;      // gesture 1 switches the sound
    }
}

static void poll_imu() {
    if (g_fd < 0) {                                              // (re)connect about once a second
        static int wait = 0;
        if (wait-- > 0) return;
        wait = 60;
        std::string p = g_port.empty() ? find_port() : g_port;
        if (p.empty() || (g_fd = imu_open(p.c_str())) < 0) { g_fd = -1; return; }
        imu_set_log_handler(g_fd, on_log, nullptr);
        g_have_t = false;
    }
    imu_sample_t s; int r, dx = 0, dy = 0;
    while ((r = imu_read(g_fd, &s, 0)) == 1) { feed(s); dx += g_t.cursor_dx; dy += g_t.cursor_dy; }
    if (r < 0) { imu_close(g_fd); g_fd = -1; g_status = "(Pico unplugged: waiting)"; return; }
    if (g_cursor_on && (dx || dy)) {
        std::string m = fmt("@M %d %d\n", dx, dy);
        if (write(g_fd, m.c_str(), m.size()) == (ssize_t)m.size()) { g_sent_x += dx; g_sent_y += dy; }
    }
    g_hz = g_sound_on ? g_t.note_hz : 0.0f;    // IMU lost: note_hz holds the last note; no data yet: 0
    update_fx();
}

// ---- Drawing -------------------------------------------------------------------------------
static void text(float x, float y, const std::string &s, float r = 1, float g = 1, float b = 1) {
    glColor3f(r, g, b); glRasterPos2f(x, y);
    glutBitmapString(GLUT_BITMAP_9_BY_15, (const unsigned char *)s.c_str());
}
static void rect(float x, float y, float w, float h, GLenum mode) {
    glBegin(mode); glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h); glEnd();
}
// Text lines going down the screen, 18 px apart.
struct Lines {
    float x, y;
    void operator()(const std::string &s, float r, float g, float b) { text(x, y, s, r, g, b); y -= 18; }
};

static void device_box(float sx, float sy, float sz) {       // each face its own colour
    static const float c[6][3] = {{.9f,.2f,.2f},{.5f,.1f,.1f},{.2f,.9f,.2f},{.1f,.5f,.1f},{.3f,.5f,1},{.15f,.25f,.5f}};
    static const float n[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    float h[3] = {sx / 2, sy / 2, sz / 2};
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glColor3fv(c[f]);
        int a = f / 2, u = (a + 1) % 3, v = (a + 2) % 3;
        for (int k = 0; k < 4; k++) {
            float p[3]; p[a] = n[f][a] * h[a];
            p[u] = ((k == 0 || k == 3) ? -1 : 1) * h[u];
            p[v] = ((k < 2) ? -1 : 1) * h[v];
            glVertex3fv(p);
        }
    }
    glEnd();
}
static void axes(float len) {                                // x red, y green, z blue
    glColor3f(1, 0.3f, 0.3f); glVertex3f(0, 0, 0); glVertex3f(len, 0, 0);
    glColor3f(0.3f, 1, 0.3f); glVertex3f(0, 0, 0); glVertex3f(0, len, 0);
    glColor3f(0.4f, 0.6f, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, len);
}

// 3D scene, world z up, seen from the user's side: table grid, axes, note ladder, device.
static void draw_scene(int W, int H, bool table) {
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluPerspective(45, (double)W / H, 0.05, 20);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(0.9, -1.4, 1.0, 0, 0, 0.3, 0, 0, 1);

    glBegin(GL_LINES);                                       // 10 cm grid, green in table mode
    for (int i = -6; i <= 6; i++) {
        if (table) glColor3f(0.15f, 0.55f, 0.25f); else glColor3f(0.25f, 0.27f, 0.32f);
        glVertex3f(i * 0.1f, -0.6f, 0); glVertex3f(i * 0.1f, 0.6f, 0);
        glVertex3f(-0.6f, i * 0.1f, 0); glVertex3f(0.6f, i * 0.1f, 0);
    }
    axes(0.25f);
    glEnd();

    float step = g_t.p.note_step_m, h0 = 0;                  // the note ladder: one rung per height gate
    glBegin(GL_LINES);
    for (int i = 0; i <= (int)g_t.p.note_count; i++) {
        bool cur = (i == g_t.note);
        glColor3f(cur ? 1.0f : 0.45f, cur ? 0.85f : 0.4f, cur ? 0.2f : 0.15f);
        glVertex3f(-0.6f, 0.6f, h0 + i * step); glVertex3f(-0.45f, 0.6f, h0 + i * step);
    }
    glEnd();

    glPushMatrix();
    if (g_lock_pos) {
        // Rotation only, about a fixed point. The farthest corner is sqrt(0.08^2 + 0.05^2 + 0.015^2)
        // = 0.0955 m from the centre, so a centre 0.10 m up never goes through the floor.
        glTranslatef(0, 0, 0.10f);
    } else {
        Vector3f p = g_t.pos;                                // horizontal drift is kept on screen
        p.x() = std::fmod(p.x(), 0.5f); p.y() = std::fmod(p.y(), 0.5f);
        glColor3f(0.9f, 0.8f, 0.3f);
        glBegin(GL_LINES); glVertex3f(p.x(), p.y(), 0); glVertex3f(p.x(), p.y(), p.z()); glEnd();   // height stalk
        glTranslatef(p.x(), p.y(), p.z() + 0.02f);
    }
    Eigen::Matrix4f M = Eigen::Matrix4f::Identity();
    M.block<3, 3>(0, 0) = g_t.q.toRotationMatrix();          // sensor -> world: exactly the tracker's q
    glMultMatrixf(M.data());
    device_box(0.16f, 0.10f, 0.03f);
    glBegin(GL_LINES); axes(0.18f); glEnd();
    glPopMatrix();
}

// Status lines, top left.
static void draw_status(float H, bool table) {
    Lines line{12, H - 22};
    std::string m = fmt("[%s MODE (T)]   device: %s%s", g_theremin_mode ? "THEREMIN" : "MOUSE", table ? "TABLE (mouse)" : "AIR (theremin / gestures)",
                        g_t.still ? "   still" : (g_t.calm ? "   calm" : "   moving"));
    if (table) line(m, 0.3f, 1, 0.4f); else line(m, 1, 0.8f, 0.2f);
    line(fmt("tilt from table %5.1f deg (limit %.1f)   height %+6.3f m (limit %.3f)   table %s", g_t.tilt_deg, g_t.p.table_tilt_deg,
             g_t.pos.z(), g_t.p.table_h_m, g_t.table_calibrated ? "calibrated" : "NOT calibrated"), 1, 1, 1);
    Eigen::Matrix3f R = g_t.q.toRotationMatrix();            // R = Rz(yaw) Ry(pitch) Rx(roll)
    float yaw = std::atan2(R(1, 0), R(0, 0)) * DEG, pitch = -std::asin(std::max(-1.0f, std::min(1.0f, R(2, 0)))) * DEG;
    float roll = std::atan2(R(2, 1), R(2, 2)) * DEG;
    line(fmt("yaw %6.1f  pitch %6.1f  roll %6.1f deg    lin acc %+5.2f %+5.2f %+5.2f m/s^2    vel %+5.2f %+5.2f %+5.2f m/s", yaw, pitch, roll,
             g_t.lin.x(), g_t.lin.y(), g_t.lin.z(), g_t.vel.x(), g_t.vel.y(), g_t.vel.z()), 0.8f, 0.8f, 0.8f);
    std::string note = fmt("note %d of %d: %s  %.1f Hz   ", g_t.note + 1, (int)g_t.p.note_count, note_name(g_t.note_hz).c_str(), g_t.note_hz);
    if (g_t.p.note_source != 0) {
        float span = 180.0f / std::max(1, (int)g_t.p.note_count);
        bool nose = g_t.p.note_source == 2;
        note += fmt("by %s %+5.1f deg (note %+.0f..%+.0f)   vol %3.0f%% (%s %+5.1f%s)   snaps %u   sound %s",
                    nose ? "red nose" : "green tilt", g_t.note_tilt_deg(), -90 + g_t.note * span, -90 + (g_t.note + 1) * span,
                    g_t.volume * 100, nose ? "green tilt" : "red nose", g_t.vol_tilt_deg(), g_t.p.vol_on == 0 ? " (off)" : (g_t.p.vol_invert != 0 ? " inv" : ""),
                    g_t.snap_count, g_sound_on ? "ON" : "off (S)");
    } else {
        note += fmt("to next gate %+4.0f%%   %s   jerks %u   sound %s", g_t.note_progress * 100,
                    g_t.note_locked ? "LOCKED (jerk)" : "free", g_t.lock_count, g_sound_on ? "ON" : "off (S)");
    }
    line(note.substr(0, 255), 1, 0.85f, 0.3f);
    line(fmt("cursor to mouse: %s   sent x %ld  y %ld counts     joystick %d (centre %d): scroll wheel is done by the Pico",
             g_cursor_on ? "ON" : "off (M)", g_sent_x, g_sent_y, g_joy_raw, g_joy_v0), 0.6f, 0.9f, 1);
    std::string gs = "gestures stored:";
    for (int s = 0; s < GEST_SLOTS; s++) gs += fmt(" %d:%zu", s + 1, g_t.gestures[s].size());
    if (g_t.record_slot >= 0) gs += fmt("   >>> RECORDING slot %d: hold still, make the movement, hold still", g_t.record_slot + 1);
    line(gs.substr(0, 255), 0.9f, 0.6f, 1);
    line(fmt("> %s", g_msg.c_str()), 1, 1, 0.5f);
}

// TUNING menu, bottom left: 13 rows around the selected one.
static void draw_tuning() {
    const auto &pt = param_table();
    int rows = 13, n = (int)pt.size(), first = std::max(0, std::min(g_sel - rows / 2, n - rows));
    Lines line{12, 18.0f * rows + 48};
    line(fmt("TUNING (%d of %d)   (Tab: effects)", g_sel + 1, n), 0.8f, 0.8f, 0.8f);
    for (int i = first; i < first + rows && i < n; i++) {
        std::string nm = pt[i].name;
        const char *note = nm == "filter" ? FILTER_NAMES[clamp_index(g_t.p.filter, 2)]
                         : nm == "cursor_method" ? CURSOR_NAMES[clamp_index(g_t.p.cursor_method, 2)]
                         : nm == "note_source" ? SOURCE_NAMES[clamp_index(g_t.p.note_source, 2)] : "";
        std::string s = fmt("%s %-18s %g  %s", i == g_sel ? ">" : " ", pt[i].name, g_t.p.*(pt[i].field), note);
        if (i == g_sel) line(s, 1, 1, 0.3f); else line(s, 0.6f, 0.6f, 0.6f);
    }
}

// EFFECTS menu, bottom left. Rows: 0 = preset to edit, then its fields, then the two buttons.
static void draw_effects() {
    const auto &ft = fx::field_table();
    const fx::Preset &ep = g_board.presets[g_fx_edit];
    const fx::Preset &rp = g_board.active ? g_board.presets[g_board.active - 1] : ep;   // bend shown: playing preset
    float bend = fx_bend(rp);
    float u = g_theremin_mode ? fx::Board::joy_u(g_joy_raw, g_joy_v0) : 0;
    std::string bar(21, '-'); bar[10] = '+'; bar[(int)std::lround((u + 1) * 10)] = '|';   // stick, down .. up
    Lines line{12, 18.0f * 13 + 48};
    line(fmt("EFFECTS (Tab: tuning)   pedal: %s   joystick bend [%s] %+.2f %s%s",
             g_board.active ? ("preset " + std::to_string(g_board.active) + " ON").c_str() : "off (clean)", bar.c_str(), bend,
             bend > 0 ? "(brighter)" : (bend < 0 ? "(softer)" : "(= preset)"),
             g_theremin_mode ? "" : "   [MOUSE MODE: joystick and clicks ignored, T]"), 0.8f, 0.8f, 0.8f);
    int nrows = 3 + (int)ft.size();
    for (int i = 0; i < nrows; i++) {
        std::string nm, val, note;
        if (i == 0) {
            nm = "edit preset"; val = std::to_string(g_fx_edit + 1);
            note = g_board.active == g_fx_edit + 1 ? "(playing now)" : "";
        } else if (i <= (int)ft.size()) {
            const auto &fi = ft[i - 1];
            nm = std::string("  ") + fi.name; val = fmt("%g", ep.*(fi.field));
            if (std::string(fi.name) == "shape") {
                static const char *w[4] = {"sine", "triangle", "saw", "square"};
                int k = std::min(3, (int)ep.shape);
                note = ep.shape == k ? w[k] : std::string(w[k]) + " -> " + w[std::min(3, k + 1)];
            }
            if (std::string(fi.name) == "joy_depth") note = ep.joy_depth == 0 ? "joystick does nothing" : "how far the joystick bends the wave";
        } else {
            int bi = i - 1 - (int)ft.size();
            nm = bi == 0 ? "left click  ->" : "right click ->";
            val = g_board.btn_preset[bi] ? "preset " + std::to_string(g_board.btn_preset[bi]) : "nothing";
        }
        std::string s = fmt("%s %-18s %-10s %s", i == g_fx_sel ? ">" : " ", nm.c_str(), val.c_str(), note.c_str());
        if (i == g_fx_sel) line(s, 1, 1, 0.3f); else line(s, 0.6f, 0.6f, 0.6f);
    }

    // Wave picture: two cycles of what is playing now, bend included (blue), and of the edited
    // preset at centre (yellow).
    float px = 470, py = 70, pw = 300, ph = 150;
    glColor3f(0.25f, 0.27f, 0.32f);
    rect(px, py, pw, ph, GL_LINE_LOOP);
    glBegin(GL_LINES); glVertex2f(px, py + ph / 2); glVertex2f(px + pw, py + ph / 2); glEnd();
    float now_shape = g_board.active ? g_board.presets[g_board.active - 1].shape : 0;
    float now_bend = g_board.active ? bend : 0.0f;
    for (int pass = 0; pass < 2; pass++) {
        float s = pass == 0 ? now_shape : ep.shape, bd = pass == 0 ? now_bend : 0.0f;
        if (pass == 0) glColor3f(0.35f, 0.45f, 0.6f); else glColor3f(1, 0.85f, 0.3f);
        glBegin(GL_LINE_STRIP);
        for (int k = 0; k <= 300; k++) {
            float ph2 = k == 300 ? 1.0f - 1e-4f : std::fmod(2.0f * k / 300.0f, 1.0f);
            glVertex2f(px + pw * k / 300.0f, py + ph / 2 + 0.42f * ph * fx::bent_wave(s, ph2, bd));
        }
        glEnd();
    }
    text(px, py + ph + 8, fmt("preset %d at centre (yellow)   playing now, bend included (blue)", g_fx_edit + 1), 0.7f, 0.7f, 0.7f);
    text(px, py - 16, fmt("delay %.0f ms x%.2f mix %.2f   reverb size %.2f mix %.2f", ep.delay_ms, ep.delay_fb, ep.delay_mix,
                          ep.reverb_size, ep.reverb_mix), 0.6f, 0.6f, 0.6f);
}

// Mouse buttons L M R, right side: green = held now, number below = presses so far.
static void draw_buttons(int W, int H) {
    static const char *label[3] = {"L", "M", "R"};
    static const int bit[3] = {0, 2, 1};                     // left, middle (joystick press), right
    float bx = W - 440.0f, by = H - 360.0f, bs = 26;
    text(bx, by + 8, "BUTTONS", 0.8f, 0.8f, 0.8f);
    for (int i = 0; i < 3; i++) {
        float x = bx + 90 + i * 60;
        bool on = (g_btn_now >> bit[i]) & 1;
        if (on) { glColor3f(0.2f, 0.9f, 0.3f); rect(x, by, bs, bs, GL_QUADS); }
        glColor3f(on ? 0.2f : 0.45f, on ? 0.9f : 0.45f, on ? 0.3f : 0.45f);
        rect(x, by, bs, bs, GL_LINE_LOOP);
        text(x + 9, by + 8, label[i], on ? 0 : 0.8f, on ? 0 : 0.8f, on ? 0 : 0.8f);
        text(x + 4, by - 16, fmt("%ld", g_btn_presses[bit[i]]), 0.6f, 0.6f, 0.6f);
    }
    text(bx + 280, by + 8, g_btn_seen ? fmt("0x%02x", g_btn_now) : "no events yet", 0.6f, 0.6f, 0.6f);
}

// Raw data, top right.
static void draw_raw(int W, int H) {
    const Tracker &t = g_t;
    Lines line{W - 440.0f, H - 150.0f};
    line(fmt("RAW DATA   %.1f Hz   filter: %s", g_rate_hz, FILTER_NAMES[clamp_index(t.p.filter, 2)]), 0.8f, 0.8f, 0.8f);
    line(fmt("accel counts  %7.0f %7.0f %7.0f", t.raw_a.x(), t.raw_a.y(), t.raw_a.z()), 1, 0.7f, 0.7f);
    line(fmt("gyro  counts  %7.0f %7.0f %7.0f", t.raw_g.x(), t.raw_g.y(), t.raw_g.z()), 0.7f, 1, 0.7f);
    line(fmt("accel [g]     %+7.3f %+7.3f %+7.3f  |a| %.3f", t.cal_a.x(), t.cal_a.y(), t.cal_a.z(), t.cal_a.norm()), 1, 0.7f, 0.7f);
    line(fmt("gyro [deg/s]  %+7.1f %+7.1f %+7.1f  (bias removed)", t.cal_w.x() * DEG, t.cal_w.y() * DEG, t.cal_w.z() * DEG), 0.7f, 1, 0.7f);
    line(fmt("gyro bias     %+7.2f %+7.2f %+7.2f deg/s", t.gyro_bias.x() * DEG, t.gyro_bias.y() * DEG, t.gyro_bias.z() * DEG), 0.6f, 0.8f, 0.6f);
    line(fmt("accel offset  %+7.3f %+7.3f %+7.3f g", t.acc_off.x(), t.acc_off.y(), t.acc_off.z()), 0.8f, 0.6f, 0.6f);
    line(fmt("accel scale   %7.3f %7.3f %7.3f   faces %d/6", t.acc_scale.x(), t.acc_scale.y(), t.acc_scale.z(), t.face_count), 0.8f, 0.6f, 0.6f);
    line(fmt("q (w x y z)   %+.3f %+.3f %+.3f %+.3f", t.q.w(), t.q.x(), t.q.y(), t.q.z()), 0.7f, 0.7f, 1);
    line(g_health.substr(0, 255), 0.5f, 0.7f, 0.5f);
}

static void display() {
    int W = glutGet(GLUT_WINDOW_WIDTH), H = glutGet(GLUT_WINDOW_HEIGHT);
    bool table = g_t.mode == Mode::TABLE;
    glClearColor(0.07f, 0.08f, 0.10f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    draw_scene(W, H, table);

    glDisable(GL_DEPTH_TEST);                                // 2D overlay
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); gluOrtho2D(0, W, 0, H);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    draw_status(H, table);
    if (g_menu == 1) draw_effects(); else draw_tuning();
    draw_buttons(W, H);
    draw_raw(W, H);
    if (!g_t.data_ok && g_frames > 0) {
        text(W / 2 - 330.0f, H / 2.0f, "IMU NOT ANSWERING: the Pico is sending zeros. Check the IMU's VCC, GND, SDA (GPIO8), SCL (GPIO9) wires.", 1, 0.3f, 0.3f);
        text(W / 2 - 330.0f, H / 2.0f - 20, g_t.note_hz > 0
             ? fmt("Holding the last note: %s  %.0f Hz (the joystick and effects still work)", note_name(g_t.note_hz).c_str(), g_t.note_hz)
             : "No note to hold yet: the IMU has not sent real data since the program started", 1, 0.6f, 0.3f);
    }
    text(12, 26, fmt("%s   frames %ld", g_status.c_str(), g_frames), 0.5f, 0.7f, 0.5f);
    text(12, 8, "T mouse/theremin  Tab effects  C table  Z zero height  B gyro bias  A accel face (x6)  O reset view  P lock view  M cursor  S sound  1-9 record  X clear  Arrows tune  W save  Q quit", 0.5f, 0.5f, 0.5f);
    glutSwapBuffers();
}

// ---- Keyboard ------------------------------------------------------------------------------
static void key(unsigned char k, int, int) {
    static const char *faces[6] = {"+X up", "-X up", "+Y up", "-Y up", "+Z up", "-Z up"};
    switch (k) {
        case 'c': case 'C':
            g_msg = g_t.calibrate_table() ? "Table plane stored: this tilt and height are now 'on the table'"
                                          : "Not stored: the device must lie still on the table";
            break;
        case 'b': case 'B': g_msg = g_t.calibrate_gyro() ? "Gyro bias stored from the last 0.2 s" : "Not stored: hold the device still"; break;
        case 'a': case 'A': {
            int f = g_t.calibrate_face();
            if (f < 0) g_msg = "Not captured: rest the device still on one face";
            else if (g_t.face_count == 6) g_msg = "All six faces captured: accelerometer offset and scale stored (W saves them)";
            else g_msg = fmt("Captured face %s (%d of 6). Turn it onto another face and press A", faces[f], g_t.face_count);
            break;
        }
        case 'o': case 'O': g_t.reset_orientation(); g_t.zero_height(); g_msg = "Orientation restarted from the accelerometer"; break;
        case 'z': case 'Z': g_t.zero_height(); g_msg = "Height set to 0 here"; break;
        case 'm': case 'M': g_cursor_on = !g_cursor_on; g_msg = g_cursor_on ? "Cursor ON: table movements now move the real mouse pointer" : "Cursor off"; break;
        case 's': case 'S': g_sound_on = !g_sound_on; g_msg = g_sound_on ? "Sound ON: lift the device to play" : "Sound off"; break;
        case 'p': case 'P': g_lock_pos = !g_lock_pos; g_msg = g_lock_pos ? "View locked to the axis centre: rotation only" : "View free: box drawn at the tracked position"; break;
        case 'x': case 'X': for (auto &g : g_t.gestures) g.clear(); g_msg = "All gestures cleared"; break;
        case 'w': case 'W': g_msg = (save(g_t, g_cfg) && fx::save(g_board, g_fx_cfg)) ? "Saved to " + g_cfg + " and " + g_fx_cfg : "Could not write " + g_cfg; break;
        case 't': case 'T':
            g_theremin_mode = !g_theremin_mode;
            g_sound_on = g_theremin_mode; g_cursor_on = !g_theremin_mode;
            g_msg = g_theremin_mode ? "THEREMIN MODE: sound on, cursor off; left/right click switch presets, joystick bends the wave"
                                    : "MOUSE MODE: cursor on, sound off; clicks are plain mouse clicks";
            break;
        case '\t': g_menu = 1 - g_menu; break;
        case 'q': case 'Q': case 27: g_hz = 0; if (g_fd >= 0) imu_close(g_fd); std::exit(0);
        default:
            if (k >= '1' && k <= '9') {
                g_t.record_slot = k - '1';
                g_msg = fmt("Recording gesture %d: hold still, make the movement once, hold still", k - '0');
            }
    }
}

// Arrow keys: up/down choose a row, left/right change it.
static void special(int k, int, int) {
    bool fx_menu = g_menu == 1;
    const auto &ft = fx::field_table();
    const auto &pt = param_table();
    int &sel = fx_menu ? g_fx_sel : g_sel, n = fx_menu ? 3 + (int)ft.size() : (int)pt.size();
    if (k == GLUT_KEY_UP) sel = (sel + n - 1) % n;
    if (k == GLUT_KEY_DOWN) sel = (sel + 1) % n;
    if (k != GLUT_KEY_LEFT && k != GLUT_KEY_RIGHT) return;
    int d = k == GLUT_KEY_RIGHT ? 1 : -1;
    if (!fx_menu) {
        float &v = g_t.p.*(pt[sel].field);
        v = clampf(v + d * pt[sel].step, pt[sel].lo, pt[sel].hi);
    } else if (sel == 0) {
        g_fx_edit = (g_fx_edit + fx::NPRESET + d) % fx::NPRESET;
    } else if (sel <= (int)ft.size()) {
        const auto &fi = ft[sel - 1];
        float &v = g_board.presets[g_fx_edit].*(fi.field);
        v = clampf(std::round((v + d * fi.step) / fi.step) * fi.step, fi.lo, fi.hi);   // stay on the step grid
    } else {
        int &p = g_board.btn_preset[sel - 1 - (int)ft.size()];
        p = (p + fx::NPRESET + 1 + d) % (fx::NPRESET + 1);             // 0 (nothing), 1..4
    }
}

static void tick(int) { poll_imu(); glutPostRedisplay(); glutTimerFunc(16, tick, 0); }

// ---- Headless modes ------------------------------------------------------------------------
// --replay: run a recorded CSV (seq,t_us,ax,ay,az,gx,gy,gz) through the tracker and summarise it.
static int replay(const char *path) {
    FILE *f = std::fopen(path, "r");
    if (!f) { std::perror(path); return 1; }
    char l[256]; long n = 0, still = 0, tab = 0, sx = 0, sy = 0; float hmax = 0, tmax = 0;
    while (std::fgets(l, sizeof l, f)) {
        unsigned seq, t; int a[3], g[3];
        if (std::sscanf(l, "%u,%u,%d,%d,%d,%d,%d,%d", &seq, &t, &a[0], &a[1], &a[2], &g[0], &g[1], &g[2]) != 8) continue;
        imu_sample_t s; s.seq = (uint16_t)seq; s.t_us = t;
        for (int i = 0; i < 3; i++) { s.a[i] = (int16_t)a[i]; s.g[i] = (int16_t)g[i]; }
        feed(s);
        if (n == 400 && !g_t.table_calibrated) g_t.calibrate_table();   // after 2 s, as if C was pressed
        n++; still += g_t.still; tab += g_t.mode == Mode::TABLE; sx += g_t.cursor_dx; sy += g_t.cursor_dy;
        hmax = std::max(hmax, std::fabs(g_t.pos.z())); tmax = std::max(tmax, g_t.tilt_deg);
    }
    std::fclose(f);
    std::printf("REPLAY samples=%ld still=%ld table=%ld cursor_x=%ld cursor_y=%ld max_abs_height_m=%.4f max_tilt_deg=%.2f "
                "g_ref=%.4f bias_dps=%.2f,%.2f,%.2f gestures=%u\n", n, still, tab, sx, sy, hmax, tmax, g_t.g_ref,
                g_t.gyro_bias.x() * DEG, g_t.gyro_bias.y() * DEG, g_t.gyro_bias.z() * DEG, g_t.gesture_count);
    return 0;
}

// --live: the same input path as the window, one line per second.
static int live(int seconds) {
    for (int i = 0; i < seconds * 60; i++) {
        poll_imu(); usleep(16000);
        if (i % 60 != 59) continue;
        Vector3f up = g_t.q.conjugate() * Vector3f::UnitZ();
        std::printf("LIVE t=%ds frames=%ld %s yaw=%.1f tilt_from_flat=%.1f bias_dps=%.2f,%.2f,%.2f g_ref=%.3f h=%.3f\n", i / 60 + 1, g_frames,
                    g_t.still ? "still" : (g_t.calm ? "calm" : "moving"), g_t.heading() * DEG, std::acos(std::min(1.0f, up.z())) * DEG,
                    g_t.gyro_bias.x() * DEG, g_t.gyro_bias.y() * DEG, g_t.gyro_bias.z() * DEG, g_t.g_ref, g_t.pos.z());
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *replay_file = nullptr; int live_s = 0;
    glutInit(&argc, argv);
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--cfg") && i + 1 < argc) g_cfg = argv[++i];
        else if (!std::strcmp(argv[i], "--replay") && i + 1 < argc) replay_file = argv[++i];
        else if (!std::strcmp(argv[i], "--live") && i + 1 < argc) live_s = std::atoi(argv[++i]);
        else g_port = argv[i];
    }
    load(g_t, g_cfg);
    bool ends_cfg = g_cfg.size() > 4 && g_cfg.compare(g_cfg.size() - 4, 4, ".cfg") == 0;
    g_fx_cfg = ends_cfg ? g_cfg.substr(0, g_cfg.size() - 4) + "_fx.cfg" : g_cfg + "_fx";   // theremin_fx.cfg
    fx::load(g_board, g_fx_cfg);
    if (replay_file) return replay(replay_file);
    if (live_s > 0) return live(live_s);

    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");           // otherwise SDL swallows Ctrl-C
    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want{}; want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = audio_cb;
        if (SDL_OpenAudio(&want, nullptr) == 0) { g_audio_ok = true; SDL_PauseAudio(0); }
    }
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(1200, 800);
    glutCreateWindow("Theremin Mouse - IMU in 3D");
    glutDisplayFunc(display);
    glutKeyboardFunc(key);
    glutSpecialFunc(special);
    glutTimerFunc(16, tick, 0);
    glutMainLoop();
    return 0;
}
