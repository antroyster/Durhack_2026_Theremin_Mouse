# Test bench

How to check each part of the Theremin Mouse on its own, then step by step up to the full system. The rules behind these tests are in `docs/phase1-spec.md`, section "Testing".

## Checklist

☑ = passed, ☐ = not passed yet, · = not built yet, – = no test at this level.

| Milestone | L0 PC unit tests | L1 PC pipeline | L2 Pico + SIM | L3 Pico + one real sensor | L4 all real | L5 soak |
|---|---|---|---|---|---|---|
| M0 blink + hello | – | – | ☑ | – | – | – |
| M1 composite + buttons | ☑ | – | ☑ | ☐ | – | – |
| M2 joystick scroll | ☑ | – | · | · | – | – |
| M3 optical cursor | ☑ | – | · | · | – | – |
| M4 IMU text | ☑ | – | · | · | – | – |
| M5 packets + reader | ☑ | ☑ | · | – | · | – |
| M6 hardening | – | – | – | – | – | · |

The L0 tests for M2–M5 pass already because the logic modules exist; those milestones still need their firmware and their L2/L3 tests.

## The levels

| Level | What is real | What it proves |
|---|---|---|
| L0 | nothing (PC only) | each piece of logic gives the right numbers |
| L1 | the reader program | framing, CRC and resync work end to end, with faults injected |
| L2 | Pico + USB, scripted sensors | USB, HID, CDC and the reboot work with nothing wired |
| L3 | Pico + one real sensor | that sensor is wired and read correctly |
| L4 | everything | the parts work together |
| L5 | everything, for a long time | nothing is lost and it recovers from unplugging |

## One-time setup

1. Install the tools:
   ```
   sudo apt install evtest usbutils
   ```
2. Let your user read the mouse's input device without sudo:
   ```
   sudo cp bench/70-theremin-mouse.rules /etc/udev/rules.d/
   sudo udevadm control --reload
   ```
   Then unplug the Pico and plug it back in.

Until both are done, the tests that count mouse events print SKIP with this instruction.

## Commands

| Command | What it does |
|---|---|
| `./bench/run.sh L0` … `L5` | every test at one level |
| `./bench/run.sh M0` … `M6` | every test for one milestone |
| `./bench/flash.sh SIM` | build and flash the scripted-sensor firmware |
| `./bench/flash.sh HW` | build and flash the real-sensor firmware |
| `make -C bench && bench/build/monitor` | live view for poking at the hardware by hand |

Run everything from the repo root (`src/`). Each test prints PASS, FAIL or SKIP with the measured number next to the limit. Every run is appended to `bench/results.md` with the date and git hash. `run.sh` flashes the right firmware (SIM or HW) by itself when a test needs it.

**SIM firmware and your desktop:** the SIM firmware does nothing with the mouse until the bench sends it the command `@RUN`. It then plays one 30 s script of clicks and goes quiet. During that script the bench "grabs" the mouse device, so the clicks reach the test and not your desktop.

---

## Cards

### M0-L2 hello at 1 Hz
- **Goal:** the firmware is alive and its timing is right.
- **Setup:** Pico plugged in, any firmware (SIM or HW).
- **Run:** `./bench/run.sh M0`
- **Do:** look at the LED and answer the y/n question.
- **Pass if:** 10 to 13 `hello` lines arrive in 12 s, the rate is 0.95 to 1.05 Hz, and the LED changes state once a second.
- **If it fails:**
  1. "no Pico serial port found": the board is unplugged or stuck in BOOTSEL. Check `lsusb | grep -i -e cafe -e 2e8a`.
  2. Rate wrong: another program has the port open and is taking some of the lines. Close `cat`, the monitor or any serial terminal.

### M1-L0 debounce and report builder
- **Goal:** a bouncy press is one click, a 4 ms glitch is none, and no report is sent unless something changed.
- **Setup:** PC only.
- **Run:** `make -C tests M=m1`
- **Do:** nothing.
- **Pass if:** bouncy press = 1 event; 4 ms glitch = 0 events; idle = 0 reports; the scripted cycle gives 14 presses and 14 releases per button and exactly 84 reports.
- **If it fails:**
  1. Someone changed `common/debounce.c` or `common/mouse_report.c`: read the failing line, it names the case.
  2. The debounce time in the test (5000 µs) no longer matches the spec.

### M1-L2 descriptors
- **Goal:** the PC sees one HID mouse and one CDC serial port on the one cable.
- **Setup:** Pico plugged in. The bench flashes SIM firmware if needed.
- **Run:** `./bench/run.sh M1` (this card is the four `lsusb:` lines)
- **Do:** nothing.
- **Pass if:** HID interfaces = 1, HID protocol = 2 (mouse), HID bInterval = 1, CDC ACM interfaces = 1.
- **If it fails:**
  1. All four show 0 or "none": the board is not running milestone 1 firmware. Run `./bench/flash.sh SIM`.
  2. One value is wrong: `firmware/usb_descriptors.c` was changed. Compare with `lsusb -v -d cafe:4005`.

### M1-L2 debug text still prints
- **Goal:** adding the mouse did not break the serial port.
- **Setup:** as above.
- **Run:** `./bench/run.sh M1` (the two `hello` lines)
- **Do:** nothing.
- **Pass if:** same numbers as card M0-L2.
- **If it fails:** same causes as card M0-L2.

### M1-L2 scripted clicks
- **Goal:** clicks travel through real USB HID and arrive as the right events, with no extras from bounce or glitches.
- **Setup:** one-time setup done. Pico plugged in, SIM firmware.
- **Run:** `./bench/run.sh M1` (the six `evtest:` lines; takes about 35 s)
- **Do:** nothing. Keep using the PC if you like; the clicks do not reach the desktop.
- **Pass if:** BTN_LEFT, BTN_RIGHT and BTN_MIDDLE each show exactly 14 presses and 14 releases.
- **If it fails:**
  1. SKIP "evtest not installed" or "no permission": do the one-time setup above.
  2. All counts 0: the `@RUN` command did not reach the board. Check nothing else has the serial port open.

### M1-L2 1200-baud reboot
- **Goal:** the board can always be reflashed over the one cable with no button press.
- **Setup:** Pico plugged in, SIM firmware.
- **Run:** `./bench/run.sh M1` (the last two lines)
- **Do:** nothing.
- **Pass if:** the BOOTSEL device (2e8a:0003) appears within 3 s, and the firmware starts again afterwards.
- **If it fails:**
  1. Time shows "none": the firmware did not act on 1200 baud. Check `tud_cdc_line_coding_cb` in the firmware.
  2. Firmware does not restart: `picotool` lacks permission. Run `picotool info` and read the error.

### M1-L3 real buttons
- **Goal:** the real left, right and joystick-press switches are wired correctly and debounced.
- **Setup:** one-time setup done. Buttons wired: left GPIO14, right GPIO15, joystick press GPIO22, each switching to GND. The bench flashes HW firmware.
- **Run:** `./bench/run.sh M1` in a terminal (it asks you to press)
- **Do:** when asked, press the named button 10 times at a normal pace, then press Enter. Three rounds: LEFT, RIGHT, MIDDLE.
- **Pass if:** each button gives exactly 10 presses and 10 releases.
- **If it fails:**
  1. 0 events: the switch is on the wrong pin or not connected to GND. Watch `evt buttons=` lines with `cat` on the serial port while pressing.
  2. More than 10: the switch bounces for longer than 5 ms. Raise `CFG_DEBOUNCE_US` in `firmware/config.h`.

### M2-L0 scroll maths
- **Goal:** the scroll accumulator gives the right number of wheel detents at every tilt and never drifts at rest.
- **Setup:** PC only.
- **Run:** `make -C tests M=m2`
- **Do:** nothing.
- **Pass if:** inside the deadzone for 10 s = 0 detents; full tilt 5 s = 100 ±1; half tilt = 50 ±1; negative tilt = −100 ±1.
- **If it fails:**
  1. Totals off by a factor: `ks` or the tick period in the test no longer match `firmware/config.h`.
  2. Drift at rest: the deadzone comparison in `common/scroll.c` was changed.

### M3-L0 clamp-and-carry
- **Goal:** no optical count is ever lost or invented, and no report exceeds ±127.
- **Setup:** PC only.
- **Run:** `make -C tests M=m3`
- **Do:** nothing.
- **Pass if:** a 300-count burst sums to exactly 300 in 3 reports; 100,000 random samples sum exactly; the scripted square returns to 0 with Σ|x| = Σ|y| = 1000.
- **If it fails:**
  1. Sum is short: the carry is being reset somewhere in `common/carry.c`.
  2. A report exceeds 127: the clamp limits were changed.

### M4-L0 scaling and the sine model
- **Goal:** raw counts convert to g and °/s using the datasheet numbers, and the simulated IMU follows its formula.
- **Setup:** PC only.
- **Run:** `make -C tests M=m4`
- **Do:** nothing.
- **Pass if:** 16384 counts = 1.000 g and 131 counts = 1.000 °/s at the default ranges; the sim matches a double-precision model within 1 LSB.
- **If it fails:**
  1. Scaling wrong: the range table in `common/scaling.c` does not match the MPU-6050 datasheet.
  2. Sine error above 1 LSB: the phase calculation in `common/sim_data.c` was changed.

### M5-L0 CRC, frames and parser
- **Goal:** the packet format is exactly what the spec says, and the parser survives garbage.
- **Setup:** PC only.
- **Run:** `make -C tests M=m5`
- **Do:** nothing.
- **Pass if:** CRC of "123456789" = 0x29B1 (shown as 10673); every field sits at its spec offset; 10,000 random samples round-trip exactly; injected faults are counted exactly.
- **If it fails:**
  1. Offset checks fail: `common/protocol.h` and `common/imu_frame.c` disagree. `protocol.h` changes need Ant's approval.
  2. Parser counters wrong: the resync rule in `host/imu_parser.c` (skip one byte on a bad CRC) was changed.

### M5-L1 whole pipeline on the PC
- **Goal:** the real reader program receives a 200 Hz stream with nothing lost, and counts injected faults exactly.
- **Setup:** PC only. A fake Pico (`bench/pico_sim`) talks to the reader through a pseudo-terminal.
- **Run:** `./bench/run.sh L1` (takes about 2 minutes)
- **Do:** nothing.
- **Pass if:** clean run: 12,000 frames in 60 s, 0 CRC errors, 0 gaps. Fault run: the reader's CRC errors equal the bad frames injected, and frames lost equal bad + dropped.
- **If it fails:**
  1. Clean run a few frames short: the PC was heavily loaded and the reader started late. Run it again.
  2. Fault counts differ: read the `INJECTED` and `TOTAL` lines by running `bench/build/pico_sim` and `host/build/imu_reader` by hand.

---

## Not built yet

These tests are defined in the spec and appear as SKIP in `run.sh`. Each gets a card when its milestone is built.

| Test | Needs |
|---|---|
| M2-L2 scripted ramp wheel total; M2-L3 real joystick range and drift | milestone 2 |
| M3-L2 scripted square path; M3-L3 sensor ID and 10 cm ruler | milestone 3 |
| M4-L2 text printout vs sine model; M4-L3 six-face test and 90° turn | milestone 4 |
| M5-L2 200 Hz framed stream from the Pico; L4 integration | milestone 5 |
| M6-L5 soak, unplug/replug and reflash | milestone 6 |
