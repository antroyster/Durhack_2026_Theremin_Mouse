# Theremin Mouse – Phase 1

## Goal
Pico enumerates as a composite USB device:
1. HID mouse: L/R buttons, joystick Y = scroll wheel, joystick press = middle click, optical sensor = x/y.
2. CDC serial: 200 Hz IMU frames (ax ay az gx gy gz) per protocol.h, read on the PC by a POSIX C reader.
Out of scope this phase: gestures, sensor fusion, audio.
One cable only: it carries power, flashing (BOOTSEL or 1200-baud reboot), the mouse and the IMU stream. A Raspberry Pi Debug Probe is on the bench for recovery, but nothing may depend on it; debug output goes over the CDC port.

## Hardware
- MCU: Raspberry Pi Pico (RP2040), Pico C SDK, TinyUSB
- IMU: MPU-6050, I2C (i2c0, 400 kHz, address 0x68), pins SDA GPIO8 / SCL GPIO9, full-scale ±2 g / ±250 °/s
- Optical sensor: PAW3360, SPI (spi0, mode 3), pins MISO GPIO16 / CS GPIO17 / SCK GPIO18 / MOSI GPIO19, MOTION on GPIO20 (active low)
- Joystick: Y axis on ADC0 (GPIO26), press on GPIO22 (internal pull-up, active low)
- Buttons: left GPIO14, right GPIO15, pull-up (internal, switch to GND, active low)
- Reserved: GPIO0/GPIO1 kept free for the Debug Probe's UART; GPIO25 is the onboard LED
- Host OS: Ubuntu 24.04 (Linux only)

Spec: docs/phase1-spec.md is the source of truth for milestones, HID report, maths, packet format and reader API.

## Repo layout
- firmware/   Pico code (firmware agent only)
- host/       POSIX reader (host agent only)
- common/protocol.h   shared packet format – change only with Ant's approval
- common/     pure logic modules (no Pico SDK calls), built for both the Pico and the PC
- tests/      L0 unit tests on the PC (`make -C tests`)
- bench/      test bench: `./bench/run.sh L0..L5 | M0..M6`, `./bench/flash.sh SIM|HW`, pico_sim, monitor
- docs/TESTBENCH.md   checklist and one card per test
- LEARNING.md   learning log, written by Claude after each milestone

## Rules
- Plan first (max 5 bullets); wait for approval before writing code.
- Small diffs. No refactors that weren't asked for.
- Never block in the TinyUSB task; sensor reads are timer-driven.
- Every change comes with a test or a manual test procedure and its expected output.
- A milestone is only done when `./bench/run.sh M<n>` shows every test passing, with none skipped.
- Flash with `./bench/flash.sh SIM|HW` (1200-baud reboot, then picotool). A SIM build only moves the mouse after the bench sends `@RUN`.
- If hardware behaviour is unknown, ask Ant for evidence (logs, lsusb, dmesg, scope or logic captures). Do not guess.
- Claude writes everything, including items marked "Ant writes" in the milestone plan. No hint-only coaching.
- Never block on predictions or quizzes before building. Get the program running first; Ant learns from LEARNING.md afterwards.

## Teaching style
Ant is an MEng mechatronics student who learns through diagrams and clear maths.
After each change, add "What I did and why": an ASCII diagram or equation where it helps, all variables defined, and one concept to check.
After each milestone, add a LEARNING.md entry with: what was built, how it works (diagram or equation where it helps), the key lines of code explained, 3 quiz questions with full answers, and any experiment worth trying.

## Current milestone
1