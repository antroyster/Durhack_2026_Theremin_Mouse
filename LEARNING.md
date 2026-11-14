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
