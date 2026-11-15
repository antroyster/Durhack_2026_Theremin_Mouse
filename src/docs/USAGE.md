# Using the Theremin Mouse

How to go from wired-up components to a working mouse with a live IMU stream. Run every command from the repo root (`src/`).

Related documents: `docs/TESTBENCH.md` (the tests, one card each), `docs/phase1-spec.md` (the rules), `LEARNING.md` (how it works).

## 1. Wire the components

Everything runs from 3.3 V. Do not connect any sensor to VBUS (5 V).

| Component | Its pin | Pico GPIO | Pico physical pin |
|---|---|---|---|
| Left button | one side | GPIO14 | 19 |
| | other side | GND | 18 |
| Right button | one side | GPIO15 | 20 |
| | other side | GND | 23 |
| Joystick | Y output (VRy) | GPIO26 (ADC0) | 31 |
| | press switch (SW) | GPIO22 | 29 |
| | supply (+) | 3V3(OUT) | 36 |
| | ground | GND | 33 |
| Optical sensor (PMW3360 / "PAW3360"), currently switched off | MISO | GPIO16 | 21 |
| | CS (NCS) | GPIO17 | 22 |
| | SCK | GPIO18 | 24 |
| | MOSI | GPIO19 | 25 |
| | supply | 3V3(OUT) | 36 |
| | ground | GND | 23 |
| IMU (MPU-6050) | SDA | GPIO8 | 11 |
| | SCL | GPIO9 | 12 |
| | supply (VCC) | 3V3(OUT) | 36 |
| | ground | GND | 13 |

- The buttons and the joystick press need no resistors: the Pico's internal pull-ups are used, and each switch connects its pin to GND.
- The optical sensor is switched off in the firmware (`CFG_OPT_ENABLE 0` in `firmware/config.h`), so it need not be connected. To use it again set the value to 1 and run `./bench/flash.sh HW`.
- Keep the optical sensor's wires under 10 cm.
- GPIO0 and GPIO1 are left free for the Debug Probe.
- If your wiring differs, change the pin numbers in `firmware/config.h`, not the wiring table.

## 2. One-time PC setup

```
sudo apt install evtest usbutils
sudo cp bench/70-theremin-mouse.rules /etc/udev/rules.d/
sudo udevadm control --reload
make -C host && make -C bench
```

Then unplug the Pico and plug it back in. (You have already done the first three lines on this laptop.)

## 3. Flash the real-sensor firmware

```
./bench/flash.sh HW
```

This builds the firmware, reboots the Pico into its bootloader over USB and writes it. No button press is needed.

If it prints "The Pico did not enter BOOTSEL by itself": unplug the Pico, hold the BOOTSEL button, plug it in, release. The script carries on by itself.

## 4. Check that every sensor was found

**Look at the LED first.**

| LED | Meaning |
|---|---|
| Changes state once a second | all sensors found |
| 2 quick blinks, pause, repeat | joystick missing |
| 3 quick blinks, pause, repeat | optical sensor missing |
| 4 quick blinks, pause, repeat | IMU missing |

Only the lowest-numbered fault is shown. A missing sensor does not stop the others: the rest of the mouse still works.

**Then read the status line:**

```
host/build/imu_reader /dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00 --duration 3
```

Expected, once a second:

```
LOG: hello fw=0.1.0 git=<hash> sensors=HW btn=ok joy=ok opt=off imu=ok
LOG: info opt_id=0x00 opt_inv=0x00 opt_srom=0x00 opt_cpi=0 imu_id=0x68
```

- `opt=off` means the optical sensor is switched off on purpose (see section 1); with it enabled you would see `opt=ok` and `opt_id=0x42`.
- For a hands-free check of whatever is wired, leave the board still and run `./bench/run.sh HW`.
- `missing` next to a sensor means its check failed at power-up. Fix the wiring, then unplug and replug (the checks only run at power-up).
- Leave the joystick alone while plugging in: its centre is measured at power-up.

## 5. Validate each sensor

Each command tests one sensor against its datasheet and tells you what to do.

| Command | What you do | Card in TESTBENCH.md |
|---|---|---|
| `./bench/run.sh M1` | press each button 10 times | M1-L3 |
| `./bench/run.sh M2` | leave the stick alone, then push it fully up and down | M2-L3 |
| `./bench/run.sh M3` | slide the sensor 10 cm along a ruler | M3-L3 |
| `./bench/run.sh M4` | hold the IMU in six orientations, then turn it 90° | M4-L3 |
| `./bench/run.sh M6` | unplug and replug five times (after a 12-minute automatic part) | M6-L5 |

Each run also repeats that milestone's automatic tests and switches the board between SIM and HW firmware as needed. Every line should say PASS. For a FAIL, the matching card lists the two most likely causes.

**When you finish testing, flash HW again**, because the last test may have left the scripted (SIM) firmware on the board:

```
./bench/flash.sh HW
```

## 6. Use it as a mouse

With HW firmware there is nothing to start. Plug it in and it is a mouse.

| You do | The PC sees |
|---|---|
| Press the left / right button | left / right click |
| Push the joystick in | middle click |
| Tilt the joystick up / down | scroll up / down; more tilt scrolls faster (20 detents per second at full tilt) |
| Slide the device on the table, with the theremin program running and M pressed (section 13) | cursor movement |

The mouse works whether or not anything is reading the serial port.

## 7. Read the IMU stream

The same cable also carries the IMU data: 200 samples a second, as binary packets. `cat` on the port shows garbage; use the reader.

```
host/build/imu_reader /dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00
```

| Option | Effect |
|---|---|
| `--csv FILE` | save every sample to a CSV file |
| `--duration SECONDS` | stop after that long (otherwise Ctrl-C) |
| `--quiet` | print only the final `TOTAL` line |

- Each second it prints the frame rate, CRC errors, gaps and frames lost. A healthy stream shows 200 Hz and zeros.
- If you unplug the Pico, the reader waits and reconnects by itself.

**CSV columns:** `seq,t_us,ax,ay,az,gx,gy,gz`

| Column | Meaning | To convert |
|---|---|---|
| `seq` | sample number; a jump means samples were lost | – |
| `t_us` | microseconds since the Pico started (wraps after 71.6 minutes) | – |
| `ax ay az` | raw acceleration | divide by 16384 to get g |
| `gx gy gz` | raw rotation rate | divide by 131 to get °/s |

Flat on the desk you should see `az` near 16384 (1 g) and the gyro values near 0.

## 8. Watch everything live

```
bench/build/monitor
```

One screen, updated ten times a second: button states, wheel and x/y totals, raw and scaled IMU values, frame rate, error counters and the latest log lines. Ctrl-C quits. Use it to poke at one input and confirm only its own values change.

## 9. Use the IMU data in your own C program

The reader is also a small library. The API is three functions:

```c
#include "imu_reader.h"

int fd = imu_open("/dev/serial/by-id/usb-Ant_Theremin_Mouse_...-if00");   /* fd, or -1 */
imu_sample_t s;
while (1) {
    int r = imu_read(fd, &s, 1000);        /* 1 = sample, 0 = timeout, -1 = error or unplugged */
    if (r < 0) break;
    if (r == 1) {
        float az_g   = s.a[2] / 16384.0f;  /* s.a[0..2] = ax ay az, s.g[0..2] = gx gy gz */
        float gz_dps = s.g[2] / 131.0f;
        /* s.seq and s.t_us are also available */
    }
}
imu_close(fd);
```

Build it from the repo root:

```
gcc -std=c11 -D_DEFAULT_SOURCE -Icommon -Ihost myprog.c host/imu_reader.c host/imu_parser.c common/*.c -lm -o myprog
```

After `imu_read` returns −1, call `imu_close`, then `imu_open` again until it succeeds; that is how `host/main.c` rides out an unplug.

## 10. Tune it

All settings are in `firmware/config.h`. Change a value, then run `./bench/flash.sh HW`.

| Setting | What it changes | Default |
|---|---|---|
| `CFG_SCROLL_KS` | scroll speed at full tilt, in detents per second | 20 |
| `CFG_SCROLL_DEADZONE_PERMILLE` | how far the stick must move before scrolling starts (100 = 10%) | 100 |
| `CFG_SCROLL_INVERT` | 1 swaps the scroll direction | 0 |
| `CFG_OPT_ENABLE` | 1 uses the optical sensor, 0 switches it off | 0 |
| `CFG_OPT_CPI` | cursor sensitivity, counts per inch (100 to 12000, steps of 100) | 1600 |
| `CFG_OPT_INVERT_X`, `CFG_OPT_INVERT_Y` | 1 flips that cursor axis | 0 |
| `CFG_OPT_SWAP_XY` | 1 swaps the axes, for a sensor mounted rotated | 0 |
| `CFG_DEBOUNCE_US` | how long a button level must hold before it counts | 5000 |
| `CFG_PIN_...` | which GPIO each component uses | see section 1 |

After changing scroll or CPI values, the automatic tests that predict exact numbers (`./bench/run.sh M2`, `M3`) will report the difference. That is expected; the limits assume the defaults.

## 11. SIM firmware: testing with nothing wired

```
./bench/flash.sh SIM
./bench/run.sh L2
```

SIM firmware replaces every sensor with scripted data, so USB, the mouse reports and the packet stream can be tested on a bare Pico. It does nothing with the mouse until the bench starts a 30-second script, and the bench captures those events so they do not reach your desktop. It is not a usable mouse: flash HW again afterwards.

## 12. When something goes wrong

| Symptom | Likely cause | What to do |
|---|---|---|
| No `/dev/serial/by-id/usb-Ant_Theremin_Mouse...` | board unplugged, or sitting in the bootloader | `lsusb \| grep -i -e cafe -e 2e8a`. `2e8a:0003` means bootloader: run `./bench/flash.sh HW` |
| `flash.sh` cannot reach the bootloader | firmware hung badly | unplug, hold BOOTSEL, plug in, release, rerun `flash.sh` |
| LED blinks an error code | that sensor failed its power-up check | fix its wiring (section 1), then unplug and replug |
| Mouse does nothing at all | SIM firmware is on the board | `./bench/flash.sh HW` |
| A button does nothing | switch not reaching GND, or on another pin | run the reader and press: a `LOG: evt buttons=0x01` line should appear (0x02 right, 0x04 middle) |
| Scrolls by itself | stick was held at power-up, or rests outside the deadzone | replug without touching it; if it persists raise `CFG_SCROLL_DEADZONE_PERMILLE` |
| Cursor moves the wrong way | sensor mounted rotated | the `CFG_OPT_INVERT_` / `CFG_OPT_SWAP_XY` settings |
| Cursor does not move, `opt=ok` | sensor too far from the surface, or a shiny surface | hold it 2–3 mm above a matt surface |
| Reader shows CRC errors or gaps | a second program has the port open | close the monitor, other readers and serial terminals |
| Tests print SKIP "no permission" | the udev rule is not installed | section 2, then replug |
| Board keeps restarting about once a second | the watchdog is resetting a hung main loop | reflash; if it persists, tell Claude what the last `LOG:` lines say |

## 13. The theremin program: 3D view, IMU cursor, notes and gestures

One-time: `sudo apt install freeglut3-dev libsdl2-dev` (already on this laptop). Then:

```
make -C theremin        # build
make -C theremin test   # check the maths, no hardware needed
cd theremin && build/theremin
```

Close the reader and the monitor first: only one program can read the port.

The window shows the device as a box turning exactly as the IMU turns (red = its x axis, green = y, blue = z), the table as a grid, and the note ladder at the back left.

| Key | Effect |
|---|---|
| C | Calibrate the table: lay the device still on the table first. Stores this tilt as "the table plane" and this height as 0 |
| Z | Set the height to 0 here |
| M | Cursor on/off. When on, table movements move the real mouse pointer. Starts off |
| S | Sound on/off. Gesture 1 also switches it |
| 1 to 9 | Record a gesture into that slot: hold still, make the movement once, hold still. Record each 3 or 4 times |
| X | Clear all gestures |
| Up / Down, Left / Right | Pick a tuning number, change it |
| W | Save tuning, table plane and gestures to `theremin.cfg` (loaded at the next start) |
| Q | Quit |

More keys: **B** stores the gyro bias (hold still), **A** captures one face for the accelerometer calibration (rest it on each of its six faces in turn, pressing A each time), **O** restarts the orientation from the accelerometer.

The top right panel shows the raw data: counts straight from the chip, the calibrated values in g and degrees per second, the gyro bias, the accelerometer offset and scale, the quaternion, and the Pico's `imuhealth` line. A red "IMU NOT ANSWERING" banner means the Pico is sending zeros: the IMU's wiring, not the software.

Orientation filter (tuning number `filter`, changeable while running):

| `filter` | Method | Its gain |
|---|---|---|
| 0 | Complementary | `tilt_gain`: higher follows the accelerometer faster, but shakes more |
| 1 | Mahony | `mahony_kp` (same trade), `mahony_ki` (learns the gyro bias; 0 switches that off) |
| 2 | Madgwick (default) | `madgwick_beta`: 0.03 smooth and slow, 0.3 quick and jittery |

`acc_reject_g` makes every filter ignore the accelerometer while the device is being shaken. `auto_bias` 0 stops the automatic gyro bias update, leaving only what B stored.

Cursor method (tuning number `cursor_method`):

| `cursor_method` | How the pointer moves | Its numbers |
|---|---|---|
| 0 | Strokes on the table, from acceleration | `cursor_gain_x`, `cursor_gain_y` |
| 1 | Air mouse: turn left/right, tip up/down | `cursor_gyro_gain`, `gyro_dead_dps`, `cursor_smooth` |
| 2 | Tilt sets the speed, like a joystick; zero is the calibrated table plane | `cursor_tilt_gain`, `tilt_dead_deg`, `cursor_smooth` |

Method 1 is the one most IMU mouse projects use, because rotation rate needs no integration and does not drift.

How it decides:

- **TABLE mode** (grid turns green): tilt from the stored table plane is under `table_tilt_deg` and the height estimate is within `table_h_m`, for `table_dwell_s`. Movements become cursor counts: `cursor_gain_x`, `cursor_gain_y` are counts per metre along the device's x and y axes; the `cursor_invert_` values flip a direction.
- **AIR mode**: everything else.
- **Notes** work on change in height, not height above the table. On the table the note is the lowest. Each time the height has changed by one gate (`note_step_m`) since the last note change, the note steps one up or one down, through `note_count` notes on a pentatonic ladder starting at MIDI note `note_base_midi`. `note_by_height` 0 switches this off, so height no longer changes the note.
- **Jerk lock**: if the vertical acceleration exceeds `note_lock_acc_g`, the note is locked, and the position is put back to the average of the last `note_safe_logs` positions logged before the jerk (20 by default). The lock releases once the acceleration has stayed below the limit for `note_lock_hold_s`. The note line shows `LOCKED (jerk)` and counts the jerks. Movements that start and end at rest are compared with the recorded gestures; one closer than `gesture_max_dist` is recognised.

Tuning order: press C; check the mode stays TABLE while you slide and turns to AIR when you lift (adjust `table_tilt_deg`, `table_h_m`); then press M and set the two cursor gains and inverts; then gestures.

If the box sinks or floats away while you hold it: the top line shows `still`, `calm` or `moving`. Held in the hand it should say `calm` (velocity is then damped and the acceleration offset re-learned). If it says `moving` while you hold it steady, raise `calm_acc_g` or `calm_gyro_dps`; if slow deliberate lifts are ignored, lower them. The height can never go below 0 (the table).

Limits you will meet (they are physics, not bugs):

- Position and height come from integrating acceleration twice, so they drift. The drift is cancelled every time the device is still, and height returns to exactly 0 whenever it rests on the table. Expect height good to a few centimetres for a few seconds of movement; press Z if it wanders.
- The cursor follows short strokes, as with a mouse you lift and replace; a slow, steady glide is not seen (constant velocity has no acceleration).
- The gyro clips at 250 degrees per second. A fast wrist flick exceeds that and the 3D view takes a second to settle afterwards.

To check a recording without the window: `build/theremin --replay file.csv` (a CSV saved by `imu_reader --csv`) prints what the tracker made of it.
