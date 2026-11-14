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
