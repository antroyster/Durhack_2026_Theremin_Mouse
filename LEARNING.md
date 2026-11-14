# Learning log

Written by Claude after each milestone. Each entry covers what was built, how it works, the key lines of code, three quiz questions with full answers, and an experiment worth trying.

---

## Pre-milestone 0 quiz: build, USB serial and flashing (2026-11-14)

These four questions were asked before any code was written. The answers below are the model answers, checked against Pico SDK 2.3.0.

### Q1. What happens if `include(pico_sdk_import.cmake)` comes after `project()`?

CMake picks your PC's native compiler (`/usr/bin/cc`, which is gcc on Ubuntu) instead of `arm-none-eabi-gcc`.

- `project()` is the step that detects compilers. It runs once, at configure time.
- The SDK import sets the ARM toolchain file. If that happens after `project()`, the compiler has already been chosen and the toolchain file is ignored.
- The SDK does not stop you: nothing in SDK 2.3.0 checks for this ordering.
- Configure therefore succeeds. The first error shows at compile, when the PC's compiler rejects ARM-only flags such as `-mcpu=cortex-m0plus` and the ARM assembly in the SDK's startup code.

```
correct order                      wrong order
-------------                      -----------
include(pico_sdk_import.cmake)     project()            <- picks /usr/bin/cc
   sets toolchain = arm gcc        include(pico_sdk_import.cmake)
project()        <- picks arm gcc     toolchain set too late, ignored
pico_sdk_init()                    pico_sdk_init()
```

**Experiment:** move the `include` line below `project()` in `firmware/CMakeLists.txt`, delete `firmware/build`, run both cmake commands and read the first error line. Put the line back afterwards.

### Q2. The main loop never calls `tud_task()`. What does, and what if interrupts are off for 2 s?

`pico_stdio_usb` sets a repeating timer alarm every 1000 µs (`PICO_STDIO_USB_TASK_INTERVAL_US`). The alarm raises a low-priority interrupt, and that interrupt calls `tud_task()`. The USB interrupt itself does the low-level packet work.

```
timer alarm (every 1 ms) --> low-priority IRQ --> tud_task() --> CDC data moves
USB controller IRQ ---------> TinyUSB driver ---> queues events for tud_task()
```

With interrupts disabled for 2 s:

- Nothing services USB in software.
- The RP2040's USB hardware stays connected and answers "not ready" (NAK) on the data endpoints, so the port does not disappear.
- Text stops for 2 s and then resumes. Anything that overflowed the buffer in the gap is lost.
- **Control requests are different.** The hardware only NAKs data endpoints. A control request from the PC in that window gets no reply, and the PC gives up after about 5 s. Opening the port sends a control request, so an open during a long stall can fail.

### Q3. What USB devices does the PC see during `picotool load -f -x`, and why does `cat` die?

```
step  PC sees                              what is happening
----  -----------------------------------  ---------------------------------------
1     2e8a:000a "Pico" (CDC + reset)       app running; picotool sends reboot
                                           request to the reset interface
2     (nothing)                            Pico disconnects; /dev/ttyACM1 removed
3     2e8a:0003 "RP2 Boot"                 ROM bootloader: mass storage (RPI-RP2)
      (mass storage + PICOBOOT)            + PICOBOOT; picotool writes the flash
4     (nothing)                            -x reboots into the new program
5     2e8a:000a "Pico" (CDC + reset)       new app enumerates; port paths return
```

`cat` dies at step 2. Its file descriptor points at a device that no longer exists, so `read()` returns end-of-file or `EIO` and `cat` exits. It never reopens the port. This is why the host reader in `host/` has to detect an unplug and reopen.

### Q4. The firmware hangs with interrupts off. Does `picotool -f` still work? How do you recover?

No. The reboot request is handled by firmware code (TinyUSB, running from interrupts). With interrupts off, nothing answers it. The 1200-baud trick fails for the same reason.

Recovery routes, from most to least manual:

1. **BOOTSEL:** hold the BOOTSEL button, unplug and replug. The ROM bootloader runs whatever is in flash, so `picotool load` (without `-f`) or dragging the `.uf2` onto the drive always works. The bootloader is in mask ROM and cannot be overwritten, so a bad flash cannot brick the board.
2. **RUN button:** a button between the RUN pin and GND resets the chip without replugging. Hold BOOTSEL while pressing it to land in the bootloader.
3. **Debug Probe:** over SWD it can halt and reflash the chip even when the firmware has crashed.
4. **Watchdog:** the only hands-free route. The firmware must "feed" a hardware timer regularly; if it hangs and stops feeding, the watchdog resets the chip by itself and the firmware (and its USB reset interface) comes back.

---

## Milestone 0: blink + "hello" over USB serial (2026-11-14)

### What was built

`firmware/main.c` toggles the onboard LED (GPIO25) and prints `hello` once a second over USB serial. `firmware/CMakeLists.txt` builds it into `firmware/build/theremin_mouse.uf2`.

Measured on the board after flashing, with PC timestamps in seconds:

```
14.677 hello
15.676 hello
16.676 hello
17.676 hello
18.676 hello
```

The lines are 1.000 s apart to within a millisecond.

### How it works

```
main.c --arm-none-eabi-gcc--> theremin_mouse.elf --picotool--> theremin_mouse.uf2
                                                                    |
                                              picotool load -f -x   v
PC  <== one USB cable ==>  Pico:  ROM bootloader writes flash, then runs the app

inside the app:
  main loop:   is it time yet? --no--> loop again (never sleeps)
                     |yes
                     v
               toggle LED, printf("hello\n"), next deadline += 1 s
  background:  1 ms timer IRQ --> tud_task() --> bytes leave over USB CDC
```

The loop never sleeps. It compares the clock with a deadline on every pass, which is the pattern every later milestone uses, because a sleeping loop cannot service USB or sensors.

**Why the timing does not drift.** Let T = 1 s be the period, t_k the k-th deadline, and e_k ≥ 0 the lateness with which the loop notices deadline k.

- Advance from the deadline: t_(k+1) = t_k + T, so t_k = t_0 + k·T. The error at tick k is only e_k. It is bounded and never accumulates.
- Advance from "now": t_(k+1) = (t_k + e_k) + T, so t_k = t_0 + k·T + (e_0 + ... + e_(k-1)). The error grows without limit.

### Key lines explained

`firmware/CMakeLists.txt`:

| Line | Why it is there |
|---|---|
| `include(pico_sdk_import.cmake)` before `project()` | `project()` fixes the compiler, so the SDK must select the ARM toolchain first |
| `project(theremin_mouse C CXX ASM)` | ASM is needed for the SDK's second-stage boot and startup code |
| `pico_sdk_init()` | creates the SDK library targets, so it must come before `add_executable` |
| `target_link_libraries(theremin_mouse pico_stdlib)` | brings in GPIO, time and stdio |
| `pico_enable_stdio_usb(theremin_mouse 1)` | sends `printf` to a USB CDC serial port; also brings in TinyUSB and the reset-into-BOOTSEL interface that `picotool -f` uses |
| `pico_enable_stdio_uart(theremin_mouse 0)` | no UART output, so GPIO0/GPIO1 stay free |
| `pico_add_extra_outputs(theremin_mouse)` | converts the `.elf` into the `.uf2` the bootloader accepts |

`firmware/main.c`:

| Line | Why it is there |
|---|---|
| `stdio_init_all()` | starts USB serial; the SDK then calls `tud_task()` from a 1 ms background timer interrupt |
| `next_due = make_timeout_time_us(TICK_PERIOD_US)` | first deadline, one period after boot |
| `if (time_reached(next_due))` | non-blocking check of the 64-bit microsecond clock |
| `next_due = delayed_by_us(next_due, TICK_PERIOD_US)` | advance from the old deadline, not from now (the drift-free step) |
| `gpio_put(LED_PIN, led_on)` | drive the LED with the toggled state |

### Quiz

**Q1. The tick is 1 s, but the LED's full blink takes 2 s. Why, and how would you get one full blink per second?**

Each tick only toggles the LED, so one on-and-off cycle needs two ticks: period = 2·T = 2 s. For a 1 s blink, halve the tick to 500 ms and print `hello` on every second tick, so the text stays at one line per second.

**Q2. You change the deadline line to "now + 1 s" and add 50 ms of other work to each loop pass. After 100 ticks, how late is the 100th `hello`?**

Each tick is noticed up to 50 ms late (e_k ≤ 50 ms), and that lateness is now baked into the next deadline. After 100 ticks the error is the sum of 100 values of e_k, so up to 100 × 50 ms = 5 s. With the deadline-based version the 100th tick is at most 50 ms late.

**Q3. The PC has the serial port open but its program has stopped reading. What happens to the blink?**

The USB buffer fills, and `printf` then waits for space. The SDK gives up after 500 ms (`PICO_STDIO_USB_STDOUT_TIMEOUT_US`) and discards the text, so one pass of the loop can stall for up to half a second and the LED toggles late. The deadline scheme then catches up, so there is no lasting drift. This is why later milestones never `printf` in the main loop: they queue packets and send only what fits.

### Experiments worth trying

1. **Include after `project()`:** described in Q1 of the pre-milestone quiz above.
2. **Drift:** change the deadline line to `next_due = make_timeout_time_us(TICK_PERIOD_US);`, add `busy_wait_ms(50);` to the loop, print `time_us_64()` with each `hello`, and watch the timestamps slide. Then restore the original line and compare.
3. **No terminal attached:** close every program reading the port and check that the LED still blinks on time. With no host listening, the text is dropped straight away.

---

## Milestone 1: composite HID mouse + CDC serial, and the test bench (2026-11-14)

### What was built

- **Firmware:** the Pico is now one USB device with two functions: a HID mouse (three buttons) and a CDC serial port. It appears as `cafe:4005 Ant Theremin Mouse`.
- **Pure logic in `common/`:** debounce, scroll, clamp-and-carry, report builder, packet framer, CRC and scaling. None of it calls the Pico SDK, so the same files compile for the Pico and for the PC tests.
- **Two sensor backends:** `sensors_hw.c` reads the real pins; `sensors_sim.c` plays scripted data. `./bench/flash.sh SIM` or `HW` picks one.
- **Test bench:** `./bench/run.sh L0..L5` or `M0..M6`, the reader in `host/`, a fake Pico (`bench/pico_sim`) and a live monitor.

### How it works

```
1 kHz timer interrupt                          main loop (never blocks)
---------------------                          ------------------------
sensors: raw button bits                       tud_task()          <- runs USB
   -> debounce x3 (5 ms rule)                  if HID endpoint free:
   -> g_buttons (one byte) ------------------>    snapshot g_buttons
                                                  mouse_report_build() says "changed"?
                                                     yes -> tud_hid_report(4 bytes)
                                               once a second: "hello ..." status line
                                               CDC bytes in -> cmd_match -> @RUN / @STOP
```

The USB layout, as the PC sees it:

```
Device  class EF/02/01  ("my functions are grouped by IADs")
`- Configuration (100 bytes, 250 mA)
   |- IAD: interfaces 0 and 1 are ONE function (CDC)
   |   |- IF0 CDC control   EP 0x81 interrupt IN
   |   `- IF1 CDC data      EP 0x02 bulk OUT, EP 0x82 bulk IN
   `- IF2 HID boot mouse    EP 0x83 interrupt IN, bInterval 1 ms
```

The 4-byte report is `{ buttons, x, y, wheel }`. Buttons are sent as states (absolute); x, y and wheel as changes (relative).

### Key lines explained

| Line | File | Why it is there |
|---|---|---|
| `add_repeating_timer_us(-(int64_t)CFG_TICK_US, ...)` | `firmware/main.c` | the negative period means "between callback starts", so the 1 kHz rate does not depend on how long the callback runs |
| `g_buttons = debounced;` | `firmware/main.c` | one byte, one writer: the hand-off from interrupt to main loop needs no lock |
| `if (!tud_hid_ready()) return;` then `mouse_report_build(...)` | `firmware/main.c` | when the endpoint is busy the state simply waits; a report goes out only on a change |
| `if (p_line_coding->bit_rate == 1200) reset_usb_boot(0, 0);` | `firmware/main.c` | the one-cable reflash path, kept in every build |
| `if (tud_cdc_write_available() < n) return;` | `firmware/dbg.c` | debug text is all-or-nothing and never blocks |
| `(uint32_t)(now_us - d->since_us) >= stable_us` | `common/debounce.c` | unsigned subtraction: still correct when the microsecond clock wraps |
| `s->acc / SCROLL_ONE_DETENT` | `common/scroll.c` | integer division truncates toward zero, which is the spec's `trunc()` |
| `0x81, 0x06` vs `0x81, 0x02` | `firmware/usb_descriptors.c` | Input (Relative) for motion, Input (Absolute) for buttons |
| `_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN)` | `firmware/usb_descriptors.c` | `wTotalLength` must equal the real byte count or the PC rejects the device |

### How the tests work and what each one proves

| Test | How it works | What it proves |
|---|---|---|
| M1 L0 (`tests/test_m1.c`) | feeds made-up waveforms to the debouncer at 0.1 ms and 1 ms steps and counts accepted edges; feeds the report builder and counts reports; feeds strings to the command matcher | the logic is right, with no hardware in the loop |
| M1 L2 `lsusb` | parses `lsusb -v -d cafe:4005` | the descriptors the PC actually received are the ones in the spec |
| M1 L2 `hello` | timestamps each line as it arrives and divides lines by time | CDC still works next to HID, and the firmware's clock is right |
| M1 L2 clicks | grabs the mouse with `evtest`, sends `@RUN`, counts BTN events over the 30 s script | clicks cross real USB and arrive exactly once each |
| M1 L2 reboot | sets 1200 baud and times how long `2e8a:0003` takes to appear | the board can always be reflashed without touching it |
| M1 L3 presses | counts events while you press each real button 10 times | the wiring and the debounce time suit your switches |
| M5 L1 | a fake Pico on a pseudo-terminal sends 12,000 frames, then again with faults injected; the real reader's counts are compared with what was injected | framing, CRC and resync work end to end before any IMU exists |

Two honest limits of the click test:

- **The scripted bounce is invisible to the firmware.** It toggles every 0.5 ms, and the firmware samples every 1 ms, so every sample lands on the "pressed" half. The 0.1 ms unit tests do exercise bounce; the L2 test only proves the glitches and the counts.
- **Why `@RUN` and not one letter.** A serial port opened without `stty raw -echo` echoes the Pico's own text back to it. The status line contains the letter r, so a one-letter trigger would have started the clicks about once a second.

### Quiz

**Q1. Remove the IAD and set the device class to 0/0/0. What probably still works, what might not, and why does the HID interface need no IAD?**

A USB "function" is what one driver binds to. HID is one interface, so it is one function. CDC ACM is two interfaces (control + data) that only make sense together, and the IAD is the statement "interfaces 0 and 1 are one function". Class 0/0/0 means "each interface is its own function", so without the IAD a generic composite driver could give IF0 and IF1 to different drivers. Linux would probably still work, because its `cdc_acm` driver also reads the CDC Union descriptor inside IF0. Windows relies on the IAD, so the serial port may not appear there. Class EF/02/01 and the IAD belong together: the first is the promise that the second exists.

**Q2. What breaks if bInterval = 10 instead of 1?**

`bInterval` is the longest time the PC may wait between polls of the endpoint, in 1 ms frames. The device never "sends"; it loads a buffer and the PC collects it at the next poll.

- **Click delay.** With debounce T_db = 5 ms, tick T_tick = 1 ms and poll period T_poll:
  t_report = t_last_bounce + T_db + [0, T_tick) + [0, T_poll).
  At 1 ms that is 5 to 7 ms; at 10 ms it is 5 to about 16 ms.
- **Clicks are not lost.** The shortest debounced press is about 5 ms, and the button byte is a state, so the press waits in the buffer and the release follows on the next poll.
- **The real damage arrives in milestone 3.** Each report carries at most 127 counts, so the fastest cursor speed is 127 × (1000 / T_poll) counts per second: 127,000 at 1 ms but about 12,700 at 10 ms. A faster swipe piles up in the carry and the cursor keeps moving after your hand stops.

**Q3. Why does a 4 ms glitch give no click, and why does the interrupt-to-main hand-off need no lock?**

```
raw     ___|‾|_|‾|_|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾    bouncy press: each return to 0 cancels the candidate
state   _______________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾    accepted 5 ms after the LAST bounce: exactly 1 event

raw     ___|‾‾‾‾‾‾‾|____________________    4 ms glitch: samples at 0, 1, 2, 3 ms are high
state   ________________________________    longest (now - since) = 3000 < 5000 -> cancelled
```

The rule is time-based (`now − since ≥ 5000 µs`), not "N equal samples", so the result does not depend on the sample rate. At 1 ms sampling a pulse must cover 6 samples to be accepted. The cost is that every edge is reported 5 ms late.

No lock is needed because `g_buttons` is one byte with one writer. Storing or loading a byte is a single instruction on the Cortex-M0+, and an interrupt cannot split an instruction, so the main loop sees the old value or the new one and never a mixture. It would break if the shared data were several values read as a set, or if the main loop modified it. Milestone 3's optical counts accumulate, so they will need interrupts off briefly around "read and zero".

### Experiments worth trying

1. **Slow the mouse down.** Change the HID endpoint's `bInterval` from 1 to 10 in `firmware/usb_descriptors.c`, run `./bench/flash.sh SIM`, then `./bench/run.sh M1`. The click counts should stay at 14 (and the `lsusb` bInterval check should fail, which shows the test is real). Restore it afterwards.
2. **Turn the debounce off.** Set `CFG_DEBOUNCE_US` to 0 in `firmware/config.h`, flash SIM and run `./bench/run.sh M1`. The 4 ms glitches should now show up as extra clicks and the test should go red.
3. **Break the parser's resync.** In `host/imu_parser.c`, make a CRC failure skip the whole 23-byte candidate, not one byte, and run `make -C tests M=m5_parser`. The "frames found behind a false start" check should fail. Restore it.
4. **Watch the fake Pico.** Run `make -C host && make -C bench` once, then `bench/build/pico_sim --link /tmp/pico --corrupt 1 &` and `host/build/imu_reader /tmp/pico --duration 10`, and compare the `INJECTED` and `TOTAL` lines.

---

## Milestone 2: joystick scroll (2026-11-14)

### What was built

The joystick's Y axis now drives the scroll wheel. The further you tilt, the faster it scrolls (rate control). Pushing the stick in was already the middle button from milestone 1.

### How it works

```
every 1 ms (timer interrupt):
   v  = ADC reading, 0..4095
   u  = (v - v0) / (4095 - v0)      above centre      (v0 = centre measured at boot)
        (v - v0) / (v0 - 0)         below centre
   u  = 0  if |u| < 0.1             deadzone
   a  = a + ks * u * dt             ks = 20 detents/s, dt = 0.001 s
   w  = trunc(a) ; a = a - w        w = whole detents, the fraction stays in a
   g_wheel_pending += w

main loop, when the HID endpoint is free:
   take g_wheel_pending (interrupts off), send up to +/-127, put the rest back
```

At full tilt u = 1, so a grows by 20 × 1 × 0.001 = 0.02 per tick and a whole detent comes out every 50 ticks: 20 detents per second. At half tilt it takes 100 ticks. Nothing is rounded away, because the fraction is carried forward.

### Key lines explained

| Line | File | Why it is there |
|---|---|---|
| `s->acc += ks * u_q15 * dt_us` | `common/scroll.c` | integers only: u is scaled by 32768 and dt is in microseconds, so the PC and the Pico get identical results |
| `w = s->acc / SCROLL_ONE_DETENT` | `common/scroll.c` | C integer division truncates toward zero, which is `trunc()` |
| `return (mag < deadzone_q15) ? 0 : u;` | `common/scroll.c` | the hard deadzone that stops drift at rest |
| `bool floating = (high - low) > CFG_JOY_FLOAT_DELTA` | `firmware/sensors_hw.c` | boot check: weak pull resistors swing an unconnected pin rail to rail but barely move a real potentiometer |
| `save_and_disable_interrupts()` around `g_wheel_pending = 0` | `firmware/main.c` | read-and-clear of a running total must not be split by the timer, or a detent is lost |
| `g_wheel_pending += pending;` after a failed send | `firmware/main.c` | unsent detents go back; nothing is discarded when USB is busy |

### How the tests work and what each one proves

| Test | How it works | What it proves |
|---|---|---|
| M2 L0 | holds fixed ADC values for a simulated 5 or 10 s and counts detents | the maths: 100 at full tilt, 50 at half, 0 in the deadzone, symmetric |
| M2 L2 | plays the scripted ramp on the Pico and sums REL_WHEEL in `evtest`; the limits are printed by the same code on the PC | the Pico's timer, accumulator and USB path deliver the predicted count (measured 99 up, 99 down) |
| M2 L3 | reads the `joy raw=` debug lines at rest and while you move the stick | your joystick's centre, travel and drift suit the deadzone |

### Quiz

**Q1. The stick is held at exactly u = 0.1004 (just outside the deadzone). How long until the first detent, and why does that feel like a jump?**
Rate = ks × u = 20 × 0.1004 ≈ 2 detents per second, so the first detent arrives after about 0.5 s. Just inside the deadzone the rate is 0; just outside it is already 2 per second. The hard deadzone makes the rate jump from 0 to 2. Rescaling u to (|u| − d) / (1 − d) would start from 0 smoothly; the spec chose the simple step.

**Q2. Why must the main loop disable interrupts around "read g_wheel_pending and set it to 0", when the button byte needed no protection?**
The button byte is a state with one writer: reading it late is harmless. The wheel total is modified by both sides. If the timer adds a detent between the main loop's read and its write of 0, that detent is erased. With interrupts off for those two instructions, the timer simply runs a few hundred nanoseconds later.

**Q3. The ramp from centre to full tilt takes 5 s. Why does it give about 49.5 detents and not 50?**
Without a deadzone the count is the area under rate against time: ∫₀⁵ 20·(t/5) dt = 50. The deadzone removes the first 0.5 s (u < 0.1), a triangle of area ½ × 0.5 s × 2 detents/s = 0.5. So 50 − 0.5 = 49.5; 49 are sent and half a detent stays in the accumulator.

### Experiments worth trying

1. In `firmware/config.h` set `CFG_SCROLL_KS` to 40, run `./bench/flash.sh SIM` and `./bench/run.sh M2`. The test should fail with about 198 up, because the prediction still uses 20. Restore it.
2. With HW firmware and the joystick wired, run `cat` on the serial port and watch `joy raw=` while moving the stick slowly: note where scrolling starts (about 205 counts from centre).

---

## Milestone 3: optical sensor moves the cursor (2026-11-14)

### What was built

The optical sensor's counts now move the cursor. `firmware/opt_pmw3360.c` is the driver, ported from your `mucking_about/paw_3360` experiment: ID check, sensor firmware (SROM) upload, CPI setting and burst reads.

### How it works

```
every 1 ms (timer interrupt):
   burst read: NCS low, send 0x50, wait 35 us, read 6 bytes, NCS high     (about 100 us)
   g_dx_pending += dx ; g_dy_pending += dy

main loop, when the HID endpoint is free:
   take dx, dy (interrupts off)
   s = r + dx ; x = clamp(s, -127, 127) ; r = s - x          clamp-and-carry, per axis
   send { buttons, x, y, wheel }
   if the send failed: restore r and put dx, dy back
```

A fast swipe of 300 counts in one tick becomes three reports: 127, 127, 46. The sum is still 300, so the cursor ends up in the right place; it just takes 3 ms to get there.

### Key lines explained

| Line | File | Why it is there |
|---|---|---|
| `if (product_id != 0x42 \|\| (id ^ inverse) != 0xFF) return false;` | `firmware/opt_pmw3360.c` | two registers that must be bitwise opposites: all-zeros or all-ones from a dead wire cannot pass |
| `gpio_pull_down(CFG_PIN_OPT_MISO)` | `firmware/opt_pmw3360.c` | with no sensor, every read is a clean 0x00 and never noise |
| `busy_wait_us(35)` | `firmware/opt_pmw3360.c` | the sensor needs 35 µs to prepare burst data; `busy_wait` because `sleep` is not allowed in an interrupt |
| `if (buf[0] & 0x07) { s_in_burst = false; return; }` | `firmware/opt_pmw3360.c` | those bits read 0 in burst mode; anything else means the sensor dropped out, so discard and re-enter |
| `carry_t undo_x = s_carry_x;` … `s_carry_x = undo_x;` | `firmware/main.c` | a failed send is undone exactly, so no count is lost or doubled |
| `c->r = s - x;` | `common/carry.c` | what did not fit in this report is kept for the next |

### How the tests work and what each one proves

| Test | How it works | What it proves |
|---|---|---|
| M3 L0 | feeds bursts and 100,000 random deltas through the carry and compares sums | in = sent + carried, always, and no report exceeds ±127 |
| M3 L2 | the Pico plays bursts and a square path; `evtest` sums REL_X and REL_Y | the totals return to 0 and the absolute sums are exactly 1000, over real USB |
| M3 L3 | reads the ID registers from the `info` line, then counts a 10 cm ruler move | the chip is a PMW3360, took its firmware, and its scale is right |

A test that only checked "total = 0" would pass with a dead sensor, so the absolute sums are checked too. That check caught a real problem during the build: the bench had tested an older firmware still on the board.

### Quiz

**Q1. At 1600 CPI, how fast must the mouse move before the carry starts to build up?**
One report per millisecond carries at most 127 counts, so the limit is 127,000 counts/s. At 1600 counts per inch that is 79 inches/s, about 2 m/s. Slower than that, the carry empties every report. Faster, the cursor lags and catches up when you slow down.

**Q2. Why can a failed send not simply be retried with the same x and y next time?**
Because by the next pass the timer has added new counts. The clean way is to undo: restore the carry to what it was and put this pass's dx, dy back into the pending totals. The next pass then starts from a state as if this one never happened, with the new counts included.

**Q3. Why is the sensor polled every tick and not read only when its MOTION pin goes low?**
Polling costs about 100 µs in every 1 ms tick (10% of the CPU) but needs no extra wire and has no interrupt of its own to get wrong. The MOTION pin would save that time when the mouse is still. It is in `config.h` for later; nothing depends on it yet.

### Experiments worth trying

1. In `common/carry.c` change the clamp to ±50 and run `make -C tests M=m3`: the "reports used" check fails (6 reports, not 3) while every sum still passes. Restore it.
2. With the sensor wired and HW firmware, change `CFG_OPT_CPI` to 800 and rerun `./bench/run.sh M3`: the ruler total should halve and fail the limit.

---

## Milestone 4: IMU read and text printout (2026-11-14)

### What was built

The MPU-6050 is read at 200 Hz and every 4th sample is printed as text: raw counts plus the same values in milli-g and °/s. `firmware/imu_mpu6050.c` is the driver.

### How it works

```
tick interrupt, every 5th tick (200 Hz):         main loop:
   t_us = time of the tick                          while queue not empty:
   read 14 bytes over I2C (about 0.45 ms)              take a sample
   put { t_us, a[3], g[3] } in the queue  ------->     print every 4th one

queue (ring buffer, 32 slots):   tail --> [s][s][s][ ][ ] <-- head
                                 main loop only moves tail; the interrupt only moves head
```

Scaling: a [g] = raw / S_a and ω [°/s] = raw / S_g. At ±2 g, S_a = 32768 / 2 = 16384 counts per g. At ±250 °/s, S_g = 32768 / 250 ≈ 131 counts per °/s.

### Key lines explained

| Line | File | Why it is there |
|---|---|---|
| `a[i] = (int16_t)(b[2*i] << 8 \| b[2*i+1]);` | `firmware/imu_mpu6050.c` | the MPU-6050 sends the high byte first (big-endian); our packets are little-endian, so the bytes are assembled, never copied |
| `g[i] = ... b[8 + 2*i] ...` | `firmware/imu_mpu6050.c` | the gyro starts at byte 8: bytes 6–7 are the temperature |
| `reg_write(REG_SMPLRT_DIV, 1000 / 200 - 1)` | `firmware/imu_mpu6050.c` | output rate = 1 kHz / (1 + divider) = 200 Hz |
| `i2c_read_timeout_us(..., 2000)` | `firmware/imu_mpu6050.c` | a stuck bus gives up after 2 ms and cannot hang the interrupt |
| `s_imu_head = next;` after filling the slot | `firmware/main.c` | the sample becomes visible to the main loop only when it is complete |
| `if (next == s_imu_tail) g_imu_dropped++;` | `firmware/main.c` | a full queue drops the new sample and counts it; it never blocks |

### How the tests work and what each one proves

| Test | How it works | What it proves |
|---|---|---|
| M4 L0 | converts known raw values and compares with datasheet numbers; compares the sim against an independent double-precision sine | the scaling constants and the simulated signal are right |
| M4 L2 | reads 5 s of `imu` lines and recomputes each value from its own `t_us` | the timestamp and the data belong together, and no sample is altered between tick, queue and text (measured: 250 lines, worst error 0) |
| M4 L3 | six still orientations, then a timed 90° turn | each real axis reads ±1 g, and the gyro's scale is right |

### Quiz

**Q1. Why is `t_us` taken at the tick and not when the I2C read finishes?**
The tick is regular to within microseconds; the read takes about 0.45 ms and varies. Stamping at the tick gives timestamps exactly 5000 µs apart, which the host can use to integrate or to detect lost samples. The small fixed delay between tick and actual sampling is a constant offset and does no harm.

**Q2. The six-face test reads +0.98 g on one face and −1.03 g on the opposite face. What are the offset and scale error of that axis?**
Offset = (a₊ + a₋) / 2 = (0.98 − 1.03) / 2 = −0.025 g. Scale = (a₊ − a₋) / 2 = (0.98 + 1.03) / 2 = 1.005, so +0.5%. This is why the test uses both directions of each axis: one reading cannot separate the two errors.

**Q3. A gyro bias of 2 °/s is left uncorrected. How far out is the angle after the 4 s turn test, and after a minute?**
Angle error = bias × time: 2 × 4 = 8° after the test (most of the ±10° allowance), and 120° after a minute. A gyro alone cannot hold an angle for long; that is the job of sensor fusion in a later phase.

### Experiments worth trying

1. Set `CFG_IMU_PRINT_DIV` to 1 in `firmware/config.h` (200 lines per second) and rerun `./bench/run.sh M4`. If the line count comes up short, text is being dropped for lack of room: that is the reason milestone 5 moves to compact binary packets.
2. With the IMU wired, lay it flat and `cat` the port: `mg=` should read about `0,0,1000`. Tilt it 30° and check the value against 1000 × sin 30° = 500.

---

## Milestone 5: binary packets and the reader (2026-11-14)

### What was built

The serial port now carries only framed packets: a 23-byte IMU frame 200 times a second, and debug text wrapped in log packets. `host/build/imu_reader` (written at milestone 1 for the bench) reads the real Pico. Plain `cat` now shows binary.

### How it works

```
Pico                                                   PC (host/imu_reader)
tick -> queue -> proto_pack_imu() -> USB CDC  ----->   read() -> ring buffer
dbg_printf() -> proto_pack_log()  -> USB CDC  ----->   find AA 55 -> whole frame? -> CRC ok?
                                                          yes: IMU sample or LOG line
   AA 55 | type | seq | t_us | payload | crc              no:  skip ONE byte, search again
```

Each IMU sample gets its `seq` number in the tick interrupt, whether or not it is later sent. So a sample dropped anywhere (queue full, USB buffer full, no listener) shows up on the PC as a gap in `seq`.

Throughput: 200 frames × 23 bytes = 4600 bytes/s, plus about 7 log packets a second. The earlier text printout needed about 100 bytes per sample.

### Key lines explained

| Line | File | Why it is there |
|---|---|---|
| `uint16_t seq = s_imu_seq++;` in the tick | `firmware/main.c` | numbering at the source makes every later loss visible |
| `if (!tud_cdc_connected() \|\| tud_cdc_write_available() < 23) continue;` | `firmware/main.c` | never blocks: with no reader or no room the sample is discarded |
| `proto_pack_log(frame, ..., seq, time_us_32(), buf, n)` | `firmware/dbg.c` | debug text rides in type 0x02 packets with its own counter |
| `put_u16(&out[21], proto_crc16(&out[2], 19))` | `common/imu_frame.c` | the CRC covers bytes 2–20: everything except the sync and itself |
| `discard_one(p)` on a CRC failure | `host/imu_parser.c` | skip one byte, not 23: a real frame may start inside the bad one |
| `(uint16_t)(seq - last_seq - 1u)` | `host/imu_parser.c` | 16-bit unsigned arithmetic gives the right answer across the 65535 → 0 wrap |

### How the tests work and what each one proves

| Test | How it works | What it proves |
|---|---|---|
| M5 L0 | packs frames and checks every byte offset; round-trips 10,000 random samples through the parser; feeds it garbage | the format matches the spec, and the parser neither loses nor invents frames |
| M5 L1 | a fake Pico on a pseudo-terminal sends 12,000 frames, then repeats with faults injected | the reader's counters are exactly right, with no hardware involved |
| M5 L2 | the real Pico streams for 20 s; the reader's CSV is checked against the sine model | measured: 4001 frames, 200.00 Hz, every `t_us` step exactly 5000 µs, 0 CRC errors, 0 lost |
| M5 L2 (mouse) | the 30 s mouse script is replayed while the reader is running | HID and CDC do not disturb each other: 14 clicks, 1000 counts, 0 frames lost |

### Quiz

**Q1. A CRC error is reported but the frame count is exactly right. How can that be?**
The parser found `AA 55` inside ordinary data (for example a `seq` of 0x55AA is sent as the bytes AA 55), took it for a frame start, and the CRC rejected it. It then skipped one byte and found the real frames behind it. A false start costs a counted error but no data. It only happens after the parser has lost its place, which is why the fault-injection rules avoid creating such pairs.

**Q2. The reader is closed for 10 s and then reopened. What does it report, and is that right?**
While it is closed the Pico discards about 2000 samples but keeps numbering them. On reopening, the reader takes the first frame as a new baseline and reports no gap, because a fresh open has no "previous" frame. Within one open session every loss is counted. If you need losses across sessions, compare the first `seq` with the last one of the previous session.

**Q3. Why does the 16-bit CRC make a silent wrong sample unlikely but not impossible?**
A corrupted frame passes only if its 16-bit CRC happens to match: 1 chance in 65,536 for random corruption (and never for a single flipped bit, which this CRC always catches). At one corrupted frame per minute that is one bad sample per 45 days. USB already has its own CRC, so in practice corruption only appears after a resync or with a second program reading the port.

### Experiments worth trying

1. Run the reader and unplug the Pico for a few seconds: `host/build/imu_reader /dev/serial/by-id/usb-Ant_Theremin_Mouse_*-if00`. Watch it report the unplug, reconnect, and count `reconnects=1`.
2. Set `CFG_IMU_QUEUE_SIZE` to 2 in `firmware/config.h`, flash SIM and run `./bench/run.sh M5`. If "frames lost" stays at 0, the main loop is keeping up with a nearly empty queue; add `busy_wait_ms(20)` to the main loop and watch the gaps appear.
