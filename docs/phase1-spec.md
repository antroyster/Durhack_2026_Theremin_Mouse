# Phase 1 spec – Theremin Mouse

Goal: the Pico enumerates as a composite USB device on ONE cable: a HID mouse (2 buttons, joystick Y = scroll wheel, joystick press = middle click, optical sensor = x/y) and a CDC serial port streaming raw IMU data (ax ay az gx gy gz) at 200 Hz to a POSIX C reader on Linux. Out of scope: gestures, fusion, audio.

## One cable rules
- The cable carries power, flashing, the HID mouse and CDC serial.
- Every build keeps the 1200-baud reboot: tud_cdc_line_coding_cb calls reset_usb_boot(0, 0) when the baud rate is 1200. Trigger: stty -F /dev/serial/by-id/<pico> 1200
- Debug output: plain text on CDC up to milestone 4; from milestone 5 everything on CDC is framed, debug as type 0x02 packets.
- Always use the /dev/serial/by-id/ path, never ttyACMn.
- The HID mouse must work whether or not the reader has the port open.

## Milestones
0. Blink + hello over USB serial – DONE
1. Composite device: HID mouse + CDC serial, two buttons click. Proof: clicks work in any app, debug text still prints, 1200-baud reboot works, lsusb -v shows HID + CDC interfaces.
2. Joystick scroll + press = middle click. Proof: a long web page scrolls smoothly at every tilt; no drift at rest.
3. Optical sensor moves the cursor. Proof: cursor tracks the desk, no jumps on fast swipes.
4. IMU read + scaled text printout. Proof: flat on the desk, az ≈ 1 g and gyro ≈ 0.
5. Binary packets + POSIX reader. Proof: steady 200 Hz, zero CRC errors, seq gaps counted.
6. Hardening. Proof: unplug/replug recovers; 10-minute run loses no frames.

## HID mouse report (4 bytes, boot-protocol style)
- byte 0 buttons: bit0 left, bit1 right, bit2 middle (joystick press); 1 = pressed
- byte 1 x: int8, optical dx, -127..127
- byte 2 y: int8, optical dy, -127..127
- byte 3 wheel: int8, positive = scroll up
- bInterval = 1 ms. Queue a report only when something changed.

## Optical clamp-and-carry
s_k = r_(k-1) + delta_k ; x_k = clamp(s_k, -127, 127) ; r_k = s_k - x_k
(delta_k = counts read this cycle, x_k = value sent, r_k = carry kept for the next report)

## Joystick scroll (rate control)
u = (v - v0) / (vmax - v0) ; u = 0 if |u| < d
a_k = a_(k-1) + ks * u * dt ; w_k = trunc(a_k) ; a_k = a_k - w_k
- v = 12-bit ADC reading (0–4095); v0 = centre, measured at boot
- d = deadzone ≈ 0.1; ks = detents per second at full tilt (start at 20); dt = report period
- w_k = wheel value sent; the fractional part stays in the accumulator

## Buttons
Debounce ≈ 5 ms: accept a new state only after it has been stable for 5 ms.

## IMU packet (little-endian, 23 bytes)
| offset | field | type | notes |
| 0 | sync | 0xAA 0x55 | frame start |
| 2 | type | u8 | 0x01 = IMU, 0x02 = log text |
| 3 | seq | u16 | gap = dropped frames |
| 5 | t_us | u32 | microseconds since boot |
| 9 | ax, ay, az | i16 x3 | raw accelerometer counts |
| 15 | gx, gy, gz | i16 x3 | raw gyroscope counts |
| 21 | crc | u16 | CRC-16/CCITT over bytes 2–20 |
Log packets (type 0x02) use the same header, then a u8 length, then text, then the CRC over everything after the sync.
Sent at 200 Hz, timer driven.

## Scaling (done on the host, not the Pico)
a = raw_a / S_a [g] ; w = raw_g / S_g [deg/s]
S_a and S_g come from the IMU datasheet for the chosen full-scale range.

## Reader (host/, POSIX + libc only)
1. open(path, O_RDWR | O_NOCTTY)
2. tcgetattr, cfmakeraw, tcsetattr (raw mode)
3. poll() with timeout (detects unplug)
4. read() into a ring buffer, find 0xAA 0x55, check the CRC; on failure skip one byte and resync
5. Count frames, CRC errors and seq gaps; print a summary each second; optionally write CSV
6. On unplug (read returns 0 or EIO), close the port and reopen it

API:
typedef struct { uint16_t seq; uint32_t t_us; int16_t a[3], g[3]; } imu_sample_t;
int  imu_open(const char *path);                         /* fd or -1 */
int  imu_read(int fd, imu_sample_t *s, int timeout_ms);  /* 1 sample, 0 timeout, -1 error */
void imu_close(int fd);

---

## Gaps and defaults (decided 2026-11-14)

The sections above left these points open. Each has a default here; the default is the rule until Ant changes it.

### Packets
| # | Gap | Default |
|---|---|---|
| P1 | Which CRC-16/CCITT | CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR. Check value: "123456789" → 0x29B1 |
| P2 | CRC byte order on the wire | Little-endian like every other field (low byte first) |
| P3 | Log packet layout | Header (9 bytes), `len` u8 at offset 9, text at offset 10, CRC at offset 10+len over bytes 2..(9+len). Total 12+len bytes |
| P4 | Log text format | ASCII, 0–255 bytes, no terminator, no trailing newline; the reader prints one line per packet |
| P5 | Is `seq` shared between types | No. Each type has its own counter, so log packets never look like dropped IMU frames |
| P6 | `seq` wrap | Wraps 65535 → 0. Frames lost = (seq − previous − 1) mod 65536 |
| P7 | `t_us` wrap | u32 wraps after about 71.6 minutes; the reader only uses differences mod 2^32 |
| P8 | Unknown type byte | Not a frame: the parser skips one byte and resyncs |
| P9 | Meaning of `t_us` | Taken at the 200 Hz timer tick, before the sensor is read |

### USB
| # | Gap | Default |
|---|---|---|
| U1 | VID/PID | 0xCAFE:0x4005 (the TinyUSB example VID; fine on a bench, not for a product) |
| U2 | Strings | Manufacturer "Ant", product "Theremin Mouse", serial = the Pico's unique flash ID |
| U3 | Interfaces | 0 = CDC control, 1 = CDC data, 2 = HID mouse; joined with an interface association descriptor (device class 0xEF/2/1) |
| U4 | Endpoints | 0x81 CDC notification (interrupt IN), 0x02 / 0x82 CDC data (bulk OUT / IN), 0x83 HID (interrupt IN, bInterval 1) |
| U5 | HID interface | Subclass 1 (boot), protocol 2 (mouse), no report ID; the first 3 report bytes are boot-compatible and byte 3 is the wheel |
| U6 | Power | Bus powered, 250 mA, no remote wakeup |
| U7 | Debug text with no listener | Dropped at once when no program has the port open; debug output never blocks |
| U8 | Flashing after milestone 0 | `picotool -f` no longer works (that was the SDK's interface). Use the 1200-baud reboot, then `picotool load -x`; `bench/flash.sh` does both |

### Timing and maths
| # | Gap | Default |
|---|---|---|
| T1 | Logic tick | 1 kHz repeating timer (1 ms). Buttons, joystick and optical are sampled on it; the IMU on every 5th tick (200 Hz) |
| T2 | Scroll `dt` | The tick period, 1 ms |
| T3 | Scroll on the negative side | u = (v − v0) / (v0 − vmin) for v < v0, with vmin = 0 and vmax = 4095; u is clamped to −1..+1 |
| T4 | Deadzone shape | Hard step as specified: u = 0 for \|u\| < d, unchanged otherwise (no rescaling) |
| T5 | Scroll arithmetic | Integer fixed-point (u in Q15, 64-bit accumulator), so the PC and the Pico give identical results |
| T6 | Scroll direction | Higher ADC reading = scroll up; `CFG_SCROLL_INVERT` in config.h flips it |
| T7 | Joystick centre at boot | Mean of 64 samples. If it is outside 1024..3072 (stick held or unplugged), use 2048 and report the joystick as missing |
| T8 | Debounce | Time-based: a different raw level must hold for 5000 µs before it is accepted. Sampled every 1 ms |
| T9 | "Something changed" for HID | Button byte differs from the last report sent, or x, y or wheel is non-zero |
| T10 | HID endpoint busy | Keep the motion in the carry and send on the next free slot; nothing is discarded |
| T11 | Optical axis direction | HID convention: +x right, +y down. `CFG_OPT_INVERT_X/Y` and `CFG_OPT_SWAP_XY` in config.h fix the mounting |
| T12 | IMU setup | MPU-6050, ±2 g (S_a = 16384 LSB/g), ±250 °/s (S_g = 131 LSB/(°/s)), 200 Hz output, 44 Hz low-pass. Raw sensor axes, no remapping |

### Boot, errors and the LED
| # | Gap | Default |
|---|---|---|
| B1 | Status line | Once a second on CDC: `hello fw=<version> git=<hash> sensors=<SIM or HW> btn=<s> joy=<s> opt=<s> imu=<s>`, where `<s>` is `ok`, `missing` or `off` (not built until its milestone). The same line is the boot log; it repeats so a late listener still sees it |
| B2 | LED when healthy | Toggles once a second (same as milestone 0) |
| B3 | LED error code | N quick blinks, then a 1 s pause, repeating: 2 = joystick, 3 = optical sensor, 4 = IMU. The lowest-numbered fault is shown |
| B4 | Mouse with a sensor missing | A missing sensor contributes zeros; every other function keeps working |
| B5 | Firmware version | `CFG_FW_VERSION` in config.h, starting at "0.1.0"; the git hash is captured when cmake configures |

---

## Testing

Every milestone (0–6) must be testable with simulated data. A milestone is only done when all of its tests pass.

### 1. Code structure
- **Pure logic modules** with no Pico SDK calls, in `common/`, compiled for both the Pico and the PC:

  | Module | Job |
  |---|---|
  | `crc16.c` | `proto_crc16` (CRC-16/CCITT-FALSE) |
  | `debounce.c` | 5 ms time-based debounce |
  | `scroll.c` | joystick normalisation, deadzone and the scroll accumulator |
  | `carry.c` | optical clamp-and-carry |
  | `mouse_report.c` | builds the 4-byte HID report; says "send" only on a change |
  | `imu_frame.c` | packs IMU and log frames |
  | `scaling.c` | raw counts → g and °/s |
  | `sim_data.c` | the scripted sensor data in section 3, as pure functions of time |

- **Sensor interface** `firmware/sensors.h` (buttons, joystick ADC, optical dx/dy, IMU raw) with two backends: `sensors_hw.c` (real sensors) and `sensors_sim.c` (scripted data). Chosen at build time: `cmake -DSENSORS=SIM` or `-DSENSORS=HW`.
- **`firmware/config.h`** holds every pin, the IMU range and data rate, ks, deadzone, debounce time and VID/PID.
- **Boot sequence:** measure the joystick centre (v0), check each sensor's ID register (WHO_AM_I / product ID), send the boot log (B1). If a sensor is missing: blink the LED error code (B3), keep the mouse working (B4), and keep reporting the error (B1).

### 2. Spec fixes
- The CRC is CRC-16/CCITT-FALSE (see P1).

### 3. Simulated data
All sim data is deterministic, so expected results can be calculated.

**Safety rule:** a SIM build does nothing with the mouse until the PC sends the 5 bytes `@RUN` + newline on the CDC port. It then plays one 30 s cycle and goes idle again. This stops a SIM board clicking and pasting on the desktop by itself. The trigger is a sequence, not one letter, because a serial port opened without `stty raw -echo` echoes the Pico's own text back to it; the firmware never prints `@RUN`, so an echo cannot start the script. The bench also grabs the mouse device (`evtest --grab`) so scripted events reach the test and not the desktop. The IMU sim is not gated; it runs from boot.

τ is the time since the `@RUN` command. Buttons, joystick and optical use τ; the IMU uses `t_us`.

| Channel | Script | Expected over one cycle |
|---|---|---|
| Buttons | 14 slots of 2 s starting at τ = 1 s. In each slot: left pressed at +0 ms, right at +500 ms, middle at +1000 ms, each held 100 ms. Every press and release edge bounces (toggles every 0.5 ms for 3 ms, then stable). At +1500 ms one button (left, right, middle in turn) gets a 4 ms glitch | 14 presses and 14 releases per button; glitches give 0 events |
| Joystick | 1–6 s: ramp from centre to full tilt. 7–12 s: half tilt (u = 0.5). 13–18 s: full negative tilt. 19–24 s: u = 0.05 (inside the deadzone). Centre otherwise | Wheel total: ramp ≈ +49.5, half tilt +50, negative −100, drift 0 |
| Optical | Bursts of 300 counts in one 1 ms sample: +x at 2 s, −x at 3 s, +y at 4 s, −y at 5 s. Square path, 200 counts per side at 1 count per 5 ms: +x 8–9 s, +y 9–10 s, −x 10–11 s, −y 11–12 s | Σx = 0 and Σy = 0; Σ\|x\| = 1000 and Σ\|y\| = 1000; no report outside ±127 |
| IMU | With t = t_us / 10^6 s: ax = round(8192·sin(2π·t)) (0.5 g at 1 Hz), ay = 0, az = 16384 (1 g), gx = gy = 0, gz = +11790 in the first half of each second and −11790 in the second half (±90 °/s) | The host recomputes every value from `t_us`; match within ±1 LSB |

### 4. Test levels
- **L0** – unit tests on the PC (no hardware): pure modules + sim data. `make -C tests` runs them, with plain C asserts and no external test framework.
- **L1** – full pipeline on the PC (no Pico): `bench/pico_sim` writes framed packets to a pseudo-terminal (`posix_openpt`); the real reader reads it, including fault injection (noise, split frames, bad CRCs, dropped seq).
- **L2** – real Pico, real USB, SIM sensors: proves USB, HID, CDC, the 1200-baud reboot and the reader with nothing wired.
- **L3** – real Pico, ONE real sensor at a time, checked against its datasheet.
- **L4** – everything real and integrated.
- **L5** – soak and recovery.

Check the mouse objectively with `evtest` (sum BTN_*/REL_X/REL_Y/REL_WHEEL events), not by watching the cursor. Check descriptors with `lsusb -v`. Tools: bash + C, evtest, usbutils (`sudo apt install evtest usbutils`).

Fault-injection rules for L1, so the counts are exact: a "bad CRC" fault flips one bit in bytes 3–22 of a frame (never the sync or type bytes) and must not create a 0xAA 0x55 pair inside the frame; noise bytes never contain 0xAA or 0x55; a "dropped" fault leaves a frame out; the first and last frames of a run are never faulted. Expected reader counts: CRC errors = bad-CRC faults; frames lost = bad-CRC faults + dropped faults.

### 5. Pass criteria per milestone
- **M0:** L2 – "hello" arrives at 1 Hz ±5% over 10 s (scripted check); LED toggles.
- **M1:** L0 – a bouncy press = exactly 1 event, a 4 ms glitch = 0 events, no report unless something changed. L2 – lsusb shows a HID mouse interface (bInterval 1) + CDC ACM; scripted clicks give matching BTN_LEFT/RIGHT/MIDDLE press and release counts in evtest; the 1200-baud reboot reaches BOOTSEL (2e8a:0003) within 3 s. L3 – 10 real presses per button = 10 events each.
- **M2:** L0 – inside the deadzone, 0 detents over 10 s; full tilt for 5 s = 100 ±1 detents (ks = 20); half tilt = 50 ±1; negative tilt symmetric. L2 – the scripted ramp's REL_WHEEL total in evtest equals the predicted value ±1. L3 – print raw ADC centre/min/max; centre drift over 10 s stays inside the deadzone.
- **M3:** L0 – a 300-count burst sums to exactly 300 and no report goes outside ±127; random streams sum exactly. L2 – the square path's REL_X and REL_Y totals both come back to 0. L3 – the ID register matches the datasheet; a 10 cm ruler move ≈ CPI × 3.94 counts ±5%.
- **M4:** L0 – scaling matches datasheet example values. L2 – the text printout follows the sine model. L3 – ID register OK; 6-face test: each axis reads ±1 g ±0.05 g; gyro bias at rest is within the datasheet typical value; a 90° turn integrates to 90° ±10°.
- **M5:** L0 – CRC check value is 0x29B1; framer → parser round-trip of 10,000 random samples is exact; parser fuzzing gives no false frames and correct counters. L1 – 60 s at 200 Hz = 12,000 frames, 0 CRC errors, 0 gaps; with 1% corruption injected, the reader's counts match what was injected. L2 – rate 200 Hz ±1%, t_us step jitter < 100 µs, values match the sine model ±1 LSB, and the mouse still works with the reader both open and closed.
- **M6:** L5 – a 10-minute run = 120,000 frames with 0 gaps; 5 unplug/replugs each reconnect within 2 s; 3 reflashes in a row by 1200-baud reboot; the mouse works the whole time.

### 6. The bench
- One command per level: `./bench/run.sh L0` … `L5`, or `./bench/run.sh M3` to run every test for one milestone. Each test prints PASS or FAIL with the measured number next to the limit, and appends the date, git hash and results to `bench/results.md`.
- `docs/TESTBENCH.md`, in plain English. At the top, a checklist table (milestone × level, ☐/☑). Then one card per test: Goal · Setup · Run · Do · Pass if · If it fails.
- `bench/monitor`: a live terminal view of the buttons, wheel, dx/dy, raw + scaled IMU, frame rate and error counters, for poking at the hardware by hand.
- `bench/flash.sh SIM|HW`: build that variant, reboot the Pico into BOOTSEL and flash it.

### 7. Order of work
1. Spec gap list + defaults → commit.
2. Refactor into pure modules + the sensor interface + config.h, and build the L0 and L1 harnesses → commit.
3. M0 tests, then milestone 1 with all of its tests passing → commit.

After each milestone, LEARNING.md gets an entry on how its tests work and what each one proves.
