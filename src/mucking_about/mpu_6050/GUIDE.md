# MPU-6050 + Raspberry Pi Pico / Pico W: from blank slate to readings in the terminal

This guide takes you from a pile of parts to live accelerometer and gyroscope numbers printing in a terminal. Each step explains **why** you are doing it, not only what to type, so you can reuse the ideas later (for example, to turn hand motion into a theremin-style mouse).

> **Which board?** This guide covers the **Raspberry Pi Pico** and the **Raspberry Pi Pico W** (both use the RP2040 chip). The wiring and code are identical for both. Only a few settings differ, and they are marked **Pico** and **Pico W** below.

**What you will learn**

1. What an MPU-6050 is and what it measures
2. What I2C is and how a Pico talks over it
3. How to wire the circuit on a breadboard
4. How to fix the project settings (there are two things to change)
5. How to write the driver code one piece at a time
6. How to flash the Pico and read the output
7. How to debug when it does not work

---

## 0. Parts list

| Part | Notes |
|------|-------|
| Raspberry Pi Pico **or** Pico W | Header pins soldered on, so it sits in the breadboard. (A plain Pico is often sold with headers as "Pico H", and the W as "Pico WH") |
| MPU-6050 module (usually labelled **GY-521**) | The small blue board, not the bare chip |
| Breadboard | Half-size is fine |
| 4 jumper wires | Male-to-male |
| Micro-USB cable | Must be a **data** cable, not charge-only |

---

## 1. Background: what are we actually talking to?

### The MPU-6050

It is a motion sensor with two sensors in one package:

- **Accelerometer** (3 axes: X, Y, Z). It measures acceleration, including gravity. Lying flat and still, it reads about **1 g** on Z. This is how you detect tilt.
- **Gyroscope** (3 axes). It measures rotation speed in degrees per second. Still means roughly 0. This is how you detect turning.
- **Temperature sensor**, a bonus. It is mainly there to correct the other two.

That is 7 values in total: 3 accelerometer, 1 temperature, 3 gyroscope.

### I2C: the two-wire conversation

I2C lets one **controller** (the Pico) talk to many **devices** (like the MPU-6050) using only two shared wires:

- **SDA** carries the data.
- **SCL** is the clock, which keeps both sides in step.

Key ideas:

- Every device has a **7-bit address**. The MPU-6050 is `0x68`, or `0x69` if its **AD0** pin is tied to 3.3V.
- The sensor has **registers**, which are numbered memory slots. Each holds one byte with a meaning. To read the accelerometer you say "give me the bytes starting at register `0x3B`".
- Both wires are **open-drain**. Devices only pull the line low, and a **pull-up resistor** pulls it high. The GY-521 board already has pull-up resistors, and the code also enables the Pico's internal ones. That is usually enough for short breadboard wires.

A typical read goes like this:

```
Pico:  START, "0x68 write", register 0x3B, (repeated START)
Pico:  "0x68 read"
MPU:   sends byte, byte, byte ... (Pico ACKs each one)
Pico:  STOP
```

You do not implement this by hand. The SDK's `i2c_write_blocking` and `i2c_read_blocking` do it for you.

---

## 2. Build the circuit

**Unplug the Pico from USB before wiring.** Wire with the power off, always.

### Connections

| MPU-6050 (GY-521) pin | Pico / Pico W pin | Pico label | Why |
|---|---|---|---|
| **VCC** | Pin 36 | 3V3(OUT) | Power. Use 3.3V so the I2C signals match the Pico's logic level |
| **GND** | Pin 38 | GND | Common ground. Without it nothing works |
| **SDA** | Pin 11 | GP8 | I2C0 data |
| **SCL** | Pin 12 | GP9 | I2C0 clock |
| XDA, XCL, AD0, INT | not connected | | XDA/XCL are for chaining other sensors. AD0 left floating means address `0x68`. INT is for interrupts, which you don't need yet |

The diagram below is the Pico / Pico W as it looks from the **top**, with the USB port at the top, in the same notation as the Pico datasheet. Pin numbers are in brackets. The left side counts **down** from 1 to 20, and the right side counts **up** from 21 (bottom) to 40 (top).

```
                    +------[ USB ]------+
          GP0  [ 1] |o                 o| [40] VBUS
          GP1  [ 2] |o                 o| [39] VSYS
          GND  [ 3] |o                 o| [38] GND  <--- GY-521 GND
          GP2  [ 4] |o                 o| [37] 3V3_EN
          GP3  [ 5] |o                 o| [36] 3V3(OUT)  <--- GY-521 VCC
          GP4  [ 6] |o                 o| [35] ADC_VREF
          GP5  [ 7] |o                 o| [34] GP28
          GND  [ 8] |o                 o| [33] GND
          GP6  [ 9] |o                 o| [32] GP27
          GP7  [10] |o                 o| [31] GP26
 SDA ---> GP8  [11] |o                 o| [30] RUN
 SCL ---> GP9  [12] |o                 o| [29] GP22
          GND  [13] |o                 o| [28] GND
          GP10 [14] |o                 o| [27] GP21
          GP11 [15] |o                 o| [26] GP20
          GP12 [16] |o                 o| [25] GP19
          GP13 [17] |o                 o| [24] GP18
          GND  [18] |o                 o| [23] GND
          GP14 [19] |o                 o| [22] GP17
          GP15 [20] |o                 o| [21] GP16
                    +-------------------+
```

Every `GND` pin is the same electrically, so any of them works. Pin 13 (GND) sits on the same side as GP8 and GP9, which keeps the jumper wires short, but the guide uses pin 38 in the table. Pick whichever is easier on your breadboard.

Check the pinout printed on your board or the Pico datasheet before plugging in. Swapping power and ground can kill the sensor.

### Wiring checklist

- [ ] Pico sits across the breadboard's centre gap, so the two rows of pins aren't shorted together
- [ ] GY-521 header pins are in the breadboard, each pin in its own column
- [ ] VCC to 3V3 (pin 36)
- [ ] GND to GND (pin 38)
- [ ] SDA to GP8 (pin 11) and SCL to GP9 (pin 12). Do not swap them

**Why GP8 and GP9?** Each GPIO pin can have several functions, and the chip only allows certain pairs for I2C. GP8 is SDA and GP9 is SCL for the `i2c0` controller. Other valid pairs include GP4/GP5 and GP0/GP1. The pin function table in the Pico datasheet lists them all.

---

## 3. Project setup

The project in `mpu_6050/` was generated by the Raspberry Pi Pico VS Code extension. Two things in the generated `CMakeLists.txt` need checking. Fix 1 only needs a change if you have a Pico W. Fix 2 applies to both boards.

Open `mpu_6050/CMakeLists.txt`.

### Fix 1: the board

The generated file says:

```cmake
set(PICO_BOARD pico CACHE STRING "Board type")
```

The board name tells the SDK which pin layout and extras to build for. Use the line that matches your board:

| Your board | Setting |
|---|---|
| **Raspberry Pi Pico** | `set(PICO_BOARD pico CACHE STRING "Board type")` (already the generated default) |
| **Raspberry Pi Pico W** | `set(PICO_BOARD pico_w CACHE STRING "Board type")` |

The Pico W has a wireless chip, and on the W the on-board LED is wired to that chip instead of a normal GPIO pin. Setting `pico_w` tells the SDK about this. For this project it changes nothing in the code, because we don't use Wi-Fi or the LED, but it keeps the board definition correct. If you flash a build for the wrong board, the program usually still runs, since both use the same RP2040 chip.

Both boards use the same I2C pins, so the wiring and the rest of the code stay the same.

### Fix 2: where printf goes

The generated file sends `printf` out of a **UART** pin, which needs a separate USB-serial adapter:

```cmake
pico_enable_stdio_uart(mpu_6050 1)
pico_enable_stdio_usb(mpu_6050 0)
```

We want output over the same USB cable we use for power, so swap them:

```cmake
pico_enable_stdio_uart(mpu_6050 0)
pico_enable_stdio_usb(mpu_6050 1)
```

Now `printf` appears as a virtual serial port on your computer.

### Leave alone

- The `DO NOT EDIT` block at the top, which the VS Code extension needs.
- `hardware_i2c` and `hardware_timer` in `target_link_libraries`. This is how the SDK's I2C code gets added to your program. Without `hardware_i2c`, you'd get "undefined reference" errors.

After editing, run **CMake: Configure** from the command palette (`Ctrl+Shift+P`) or let the Pico extension re-configure. If the old `build/` folder causes odd errors, delete it and configure again. It is git-ignored anyway.

---

## 4. Write the code, step by step

Open `mpu_6050/mpu_6050.c`. We'll build it up in stages, so you can test each stage before moving on. After each stage, build, flash and check the output (see section 5).

### Stage A: "Hello" over USB (is the toolchain working?)

Replace the file's contents with:

```c
#include <stdio.h>
#include "pico/stdlib.h"

int main() {
    stdio_init_all();       // set up printf over USB
    sleep_ms(3000);         // give the PC time to open the serial port

    while (true) {
        printf("Hello from the Pico!\n");
        sleep_ms(1000);
    }
}
```

**What is happening**

- `stdio_init_all()` starts the output channel we selected in `CMakeLists.txt` (USB).
- `sleep_ms(3000)` matters. USB serial takes a moment to appear on your computer, and anything printed before you connect is lost. Without the delay, you would miss the first lines.

Run this first. If you can't see "Hello", nothing later will work, so fix it now (section 6).

### Stage B: set up I2C and scan for the sensor

Now we check the wiring without needing to know anything about the sensor. An **I2C scan** tries every address and reports who answers.

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define I2C_PORT i2c0
#define I2C_SDA  8
#define I2C_SCL  9

int main() {
    stdio_init_all();
    sleep_ms(10000);

    // 1. Start the I2C0 hardware at 400 kHz ("fast mode").
    i2c_init(I2C_PORT, 400 * 1000);

    // 2. Hand the two pins over to the I2C hardware.
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);

    // 3. Turn on internal pull-ups (the GY-521 also has its own).
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    printf("Scanning I2C bus...\n");
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {   // valid 7-bit range
        uint8_t dummy;
        int result = i2c_read_blocking(I2C_PORT, addr, &dummy, 1, false);
        if (result >= 0) {                               // >= 0 means "someone ACKed"
            printf("Found device at 0x%02X\n", addr);
        }
    }
    printf("Scan done.\n");

    while (true) sleep_ms(1000);
}
```

**What is happening**

- `i2c_init` starts the hardware and sets the speed. 400 kHz is the MPU-6050's maximum, and 100 kHz is the safe default if you hit errors.
- `gpio_set_function(..., GPIO_FUNC_I2C)` is the step people forget. Pins default to plain GPIO, and this tells the chip to route them to the I2C block.
- Reading one byte from an address is a cheap way to ask "anyone there?". If a device exists, it **acknowledges** (ACK) and the function returns a number of bytes `>= 0`. If nobody answers it returns a negative error code.

**Expected output**

```
Scanning I2C bus...
Found device at 0x68
Scan done.
```

`0x68` means the wiring is right. If nothing is found, jump to section 6 before going on.

### Stage C: confirm the chip identity (WHO_AM_I)

Every MPU-6050 has a read-only register, `WHO_AM_I` (`0x75`), that always contains `0x68`. Reading it proves you can do a real register read, which is the pattern for everything else.

Add these helper functions **above** `main` (keep the includes and defines):

```c
#define MPU_ADDR         0x68
#define REG_WHO_AM_I     0x75
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_XOUT_H 0x3B

// Write one byte into one register: send [register, value].
static int mpu_write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_write_blocking(I2C_PORT, MPU_ADDR, buf, 2, false);
}

// Read `len` bytes starting at register `reg`.
static int mpu_read_regs(uint8_t reg, uint8_t *dst, size_t len) {
    // Step 1: tell the sensor which register we want.
    //         `true` = keep control of the bus (no STOP), so the next
    //         read follows directly as a "repeated start".
    int r = i2c_write_blocking(I2C_PORT, MPU_ADDR, &reg, 1, true);
    if (r < 0) return r;
    // Step 2: read the bytes back.
    return i2c_read_blocking(I2C_PORT, MPU_ADDR, dst, len, false);
}
```

Then, in `main`, after the I2C setup, replace the scan with:

```c
    uint8_t id = 0;
    if (mpu_read_regs(REG_WHO_AM_I, &id, 1) < 0) {
        printf("No response from MPU-6050, check wiring.\n");
    } else {
        printf("WHO_AM_I = 0x%02X (expect 0x68)\n", id);
    }
```

**The two-step read is the key idea.** An I2C device has no way to know which register you want unless you say so. So reading a register is always: **write the register number, then read the data**. The `true` flag (`nostop`) glues the two halves into one transaction.

### Stage D: wake the sensor up

The MPU-6050 powers on in **sleep mode**, and in sleep it returns zeros or stale data. Register `PWR_MGMT_1` (`0x6B`) controls this. Writing `0x00` clears the sleep bit.

Add this after the WHO_AM_I check:

```c
    mpu_write_reg(REG_PWR_MGMT_1, 0x00);   // clear SLEEP bit: wake up
    sleep_ms(100);                          // let it settle
```

If you ever see all zeros, a missing wake-up is the usual cause.

### Stage E: read all the data

The seven values sit next to each other in memory, starting at `0x3B`:

| Register(s) | Contents |
|---|---|
| `0x3B`-`0x3C` | Accel X (high byte, low byte) |
| `0x3D`-`0x3E` | Accel Y |
| `0x3F`-`0x40` | Accel Z |
| `0x41`-`0x42` | Temperature |
| `0x43`-`0x44` | Gyro X |
| `0x45`-`0x46` | Gyro Y |
| `0x47`-`0x48` | Gyro Z |

That's 14 bytes. Because the register address auto-increments, **one** read of 14 bytes from `0x3B` grabs everything in a single transaction. That's better than 7 separate reads, since all the values come from the same instant.

Each value is a **16-bit signed integer, high byte first** ("big-endian"). Combine the bytes like this:

```c
static int16_t to_i16(uint8_t hi, uint8_t lo) {
    return (int16_t)((hi << 8) | lo);
}
```

The raw numbers need scaling into real units. At power-on the sensor uses its most sensitive ranges:

- Accelerometer is ±2 g, so **16384 LSB per g**. A raw 16384 means 1 g.
- Gyroscope is ±250 °/s, so **131 LSB per °/s**.
- Temperature in °C is `raw / 340.0 + 36.53` (from the datasheet).

### Stage F: the complete program

This is the final `mpu_6050.c`. It combines all the stages.

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// ---- Hardware config -------------------------------------------------
#define I2C_PORT i2c0
#define I2C_SDA  8
#define I2C_SCL  9

// ---- MPU-6050 constants ----------------------------------------------
#define MPU_ADDR         0x68    // 0x69 if AD0 is tied to 3.3V
#define REG_WHO_AM_I     0x75
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_XOUT_H 0x3B

#define ACCEL_LSB_PER_G   16384.0f   // +/-2 g range
#define GYRO_LSB_PER_DPS  131.0f     // +/-250 deg/s range

// ---- Helpers ---------------------------------------------------------
static int mpu_write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    return i2c_write_blocking(I2C_PORT, MPU_ADDR, buf, 2, false);
}

static int mpu_read_regs(uint8_t reg, uint8_t *dst, size_t len) {
    int r = i2c_write_blocking(I2C_PORT, MPU_ADDR, &reg, 1, true);
    if (r < 0) return r;
    return i2c_read_blocking(I2C_PORT, MPU_ADDR, dst, len, false);
}

static int16_t to_i16(uint8_t hi, uint8_t lo) {
    return (int16_t)((hi << 8) | lo);
}

// ---- Main ------------------------------------------------------------
int main() {
    stdio_init_all();
    sleep_ms(3000);                       // wait for the serial terminal

    // I2C hardware + pins
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Is the sensor there?
    uint8_t id = 0;
    if (mpu_read_regs(REG_WHO_AM_I, &id, 1) < 0) {
        printf("No response from MPU-6050. Check wiring and address.\n");
        while (true) sleep_ms(1000);
    }
    printf("WHO_AM_I = 0x%02X (expect 0x68)\n", id);

    // Wake it up
    mpu_write_reg(REG_PWR_MGMT_1, 0x00);
    sleep_ms(100);

    // Read and print forever
    while (true) {
        uint8_t raw[14];
        if (mpu_read_regs(REG_ACCEL_XOUT_H, raw, sizeof raw) < 0) {
            printf("Read failed\n");
            sleep_ms(500);
            continue;
        }

        float ax = to_i16(raw[0],  raw[1])  / ACCEL_LSB_PER_G;
        float ay = to_i16(raw[2],  raw[3])  / ACCEL_LSB_PER_G;
        float az = to_i16(raw[4],  raw[5])  / ACCEL_LSB_PER_G;
        float t  = to_i16(raw[6],  raw[7])  / 340.0f + 36.53f;
        float gx = to_i16(raw[8],  raw[9])  / GYRO_LSB_PER_DPS;
        float gy = to_i16(raw[10], raw[11]) / GYRO_LSB_PER_DPS;
        float gz = to_i16(raw[12], raw[13]) / GYRO_LSB_PER_DPS;

        printf("Accel[g] %6.2f %6.2f %6.2f | Gyro[dps] %7.1f %7.1f %7.1f | %.1fC\n",
               ax, ay, az, gx, gy, gz, t);

        sleep_ms(100);                    // ~10 readings per second
    }
}
```

**Walkthrough of the loop**

1. `mpu_read_regs(REG_ACCEL_XOUT_H, raw, 14)` fetches all 14 bytes in one go.
2. `to_i16` merges each high/low pair into a signed 16-bit number. It must be **signed**, because negative acceleration and rotation are normal.
3. Dividing by the sensitivity gives real units.
4. `printf` formats the values with `%6.2f` (width 6, 2 decimals) so the columns line up.

---

## 5. Build, flash, and view the output

### Build

In VS Code, with the Raspberry Pi Pico extension:

1. Click **Compile** in the bottom status bar, or open the Pico extension sidebar and choose **Compile Project**.
2. A successful build creates `build/mpu_6050.uf2`. That is the file you copy to the Pico.

### Flash

1. **Unplug** the Pico.
2. Hold the **BOOTSEL** button, plug the USB cable in, then release the button.
3. The Pico (or Pico W) appears as a USB drive called **RPI-RP2**.
4. Either click **Run** in the Pico extension, or drag `build/mpu_6050.uf2` onto that drive.
5. The drive disappears and the Pico reboots into your program.

### View the output

Pick one:

**Option 1: VS Code.** The Pico extension has a serial monitor. If you prefer something else, use one of the terminal options below.

**Option 2: terminal (Linux).**

```bash
ls /dev/ttyACM*                 # find the port, usually /dev/ttyACM0
screen /dev/ttyACM0 115200      # exit with Ctrl+A then K then Y
# or:
minicom -D /dev/ttyACM0
```

If you get "permission denied", add yourself to the `dialout` group, then log out and back in:

```bash
sudo usermod -aG dialout $USER
```

### What you should see

```
WHO_AM_I = 0x68 (expect 0x68)
Accel[g]   0.02  -0.01   1.03 | Gyro[dps]    -0.4     0.2     0.1 | 24.3C
Accel[g]   0.02  -0.01   1.03 | Gyro[dps]    -0.5     0.3     0.1 | 24.3C
```

### Experiments, to check you understand it

| Do this | You should see |
|---|---|
| Lay the sensor flat and still | Z is about **1.0**, X and Y are near 0, gyro is near 0 |
| Tilt it onto its side | The 1 g moves from Z to X or Y |
| Rotate it quickly around one axis | The matching gyro value spikes, then returns to about 0 |
| Shake it | Accel values jump well above 1 g |
| Pinch the chip | Temperature creeps up a degree or two |

Small constant gyro offsets (for example -0.4) are normal. That's called **bias**, and you can calibrate it out by averaging the first few hundred readings while the sensor is still, then subtracting that average.

---

## 6. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Terminal shows nothing at all | Check `CMakeLists.txt`: USB must be `1`, UART `0` (section 3). Make sure you waited out the 3 s delay. Check the cable carries data |
| No `/dev/ttyACM0` | The Pico may be stuck in BOOTSEL mode. Unplug, plug in without holding the button. Try another cable |
| Build fails or the board behaves oddly | Check `PICO_BOARD` matches your board: `pico` for the Pico, `pico_w` for the Pico W (section 3) |
| "No response from MPU-6050" | SDA and SCL swapped, a loose jumper, no GND, or VCC not at 3V3. Re-check section 2 and run the Stage B scan |
| Scan finds `0x69`, not `0x68` | AD0 is pulled high. Change `MPU_ADDR` to `0x69` |
| WHO_AM_I is `0x70` or `0x72` | Your module may be a clone or a different chip, such as an MPU-6500 or 9250. The register map is mostly the same, so it usually still works |
| All values are exactly 0 | You didn't wake it. Check the `PWR_MGMT_1` write |
| Values are huge or random | You are not combining bytes correctly (use signed 16-bit, high byte first), or the wiring is flaky. Press the jumpers in firmly |
| Works, then glitches | Breadboard contacts are loose. Drop the speed to `100 * 1000` in `i2c_init` |

---

## 7. Where to go next

- **Calibrate the gyro** by subtracting the average still-reading from each sample.
- **Estimate tilt angles** from the accelerometer with `atan2f(ax, az)` and similar.
- **Combine the sensors** with a **complementary filter**: `angle = 0.98 * (angle + gyro * dt) + 0.02 * accel_angle`. The gyro is smooth but drifts, and the accelerometer is noisy but stable, so the filter blends the two.
- **Use the data.** Map tilt to pitch and volume for the theremin, or to cursor movement for the mouse.
- **Change ranges** by writing to `GYRO_CONFIG` (`0x1B`) and `ACCEL_CONFIG` (`0x1C`). Larger ranges handle faster motion but are less precise. Remember to update the LSB-per-unit constants if you do.
