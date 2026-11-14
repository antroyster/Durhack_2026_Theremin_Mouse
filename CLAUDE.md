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

## Repo layout
- firmware/   Pico code (firmware agent only)
- host/       POSIX reader (host agent only)
- common/protocol.h   shared packet format – change only with Ant's approval
- LEARNING.md   Ant's notes and quiz answers

## Rules
- Plan first (max 5 bullets); wait for approval before writing code.
- Small diffs. No refactors that weren't asked for.
- Never block in the TinyUSB task; sensor reads are timer-driven.
- Every change comes with a test or a manual test procedure and its expected output.
- If hardware behaviour is unknown, ask Ant for evidence (logs, lsusb, dmesg, scope or logic captures). Do not guess.
- Items marked "Ant writes" in the milestone plan: give hints in this order (concept, approach, code), and give code only when he asks.

## Teaching style
Ant is an MEng mechatronics student who learns through diagrams and clear maths.
After each change, add "What I did and why": an ASCII diagram or equation where it helps, all variables defined, and one concept to check.

## Current milestone
0