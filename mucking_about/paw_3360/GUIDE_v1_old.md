# PAW3360 + Raspberry Pi Pico / Pico W: from blank slate to delta X / delta Y in the terminal

This guide takes you from a PAW3360 optical mouse sensor and a Pico to live **delta X** and **delta Y** numbers printing over USB. Each step says **why**, not only what, so you can reuse the ideas for the Theremin Mouse.

It is the sibling of the MPU-6050 guide (`../mpu_6050/GUIDE.md`). The shape is the same: wire it, configure the project, build the code in stages, and test after each stage. The big difference is the bus. The MPU-6050 used **I2C**. The PAW3360 uses **SPI**.


**What you will learn**

1. What a PAW3360 is, and what "delta X / delta Y" means
2. What SPI is, and how it differs from I2C
3. How to wire it, and which Pico pins to use
4. Which options to pick in the Pico project creator
5. How to write the driver one stage at a time
6. How to flash, read the output, and debug

---

## 0. Parts list

| Part | Notes |
|------|-------|
| Raspberry Pi Pico **or** Pico W | Headers soldered on |
| PAW3360 on a breakout / module | Either a purpose-made breakout or a board lifted from a mouse |
| Breadboard + jumper wires | 6 to 7 wires (see wiring table) |
| Micro-USB **data** cable | Not charge-only |
| A flat, textured surface | Desk, paper or a mousepad. Glossy glass will not track |

---

## 1. Background

### What the PAW3360 is

It is a tiny **camera plus processor**. It takes thousands of low-resolution pictures of the surface every second, compares each picture with the last one, and works out how far the surface appears to have shifted. That shift is the output:

- **delta X**: how far it moved left/right since you last asked
- **delta Y**: how far it moved up/down since you last asked

Two important consequences:

- The numbers are **relative movement since the last read**, not a position. Read, then it resets. Add the deltas up yourself if you want a position (the code keeps a running total).
- The size of one count depends on the sensor's **CPI** (counts per inch) setting. At 1600 CPI, one inch of movement is about 1600 counts.

The sensor also needs the right **height** above the surface. A mouse has a lens that sets this, and a bare module held too high or too low gives zeros.

### SPI: four wires, not two

SPI is faster and simpler than I2C, but uses more wires and has no addresses:

| Wire | Name | Direction | Job |
|---|---|---|---|
| **SCK** (SCLK) | Serial clock | Pico to sensor | The Pico ticks this to pace every bit |
| **MOSI** | Main out, sub in | Pico to sensor | Data the Pico sends |
| **MISO** | Main in, sub out | Sensor to Pico | Data the sensor sends back |
| **CS** (NCS) | Chip select | Pico to sensor | Pull **low** to start talking, high to stop |

Because there are no addresses, **each device gets its own CS wire**. CS is active-low: low means "this chip, listen up".

Key ideas:

- **SPI mode.** The clock can idle high or low, and data can be read on the rising or falling edge. That gives 4 combinations, called modes 0 to 3. Both sides must agree. PixArt sensors in this family use **mode 3**: the clock idles **high**, and data is sampled on the **rising** edge. In the SDK that is `SPI_CPOL_1, SPI_CPHA_1`. A wrong mode gives garbage or all `0xFF`/`0x00`. **[verify]**
- **Registers still exist**, just like I2C. The first byte you send is the register address, and **bit 7 says read or write**: `0` means read, `1` means write. So writing register `0x3A` sends `0xBA`.
- **The sensor needs thinking time.** After you send a read address, the sensor needs about 160 microseconds before the data is ready. Sending clock pulses too soon reads garbage. The code has `sleep_us(160)` for this. In the datasheets this gap is called **tSRAD**.

A read looks like this:

```
CS low
Pico  MOSI: [ 0x03 ]            (address with bit 7 = 0 means "read")
        ... wait ~160 us ...
Pico  SCK ticks, sensor MISO: [ data byte ]
CS high
        ... wait ~20 us before the next transaction ...
```

Compare with I2C: there is no ACK/NACK. If nothing is connected, MISO just floats and you read `0x00` or `0xFF` with no error. That makes the ID check in Stage C important.

### Motion registers

These are the registers we use. **[verify]** against your datasheet.

| Address | Name | Meaning |
|---|---|---|
| `0x00` | Product_ID | Fixed identity byte |
| `0x02` | Motion | **Bit 7** is set if there was motion since the last read. **Reading this register latches the delta values**, so read it first |
| `0x03` / `0x04` | Delta_X_L / Delta_X_H | X movement, low and high byte |
| `0x05` / `0x06` | Delta_Y_L / Delta_Y_H | Y movement, low and high byte |
| `0x3A` | Power_Up_Reset | Write `0x5A` to reset the chip |
| `0x3F` | Inverse_Product_ID | The bitwise inverse of Product_ID |

Each delta is a **signed 16-bit** number split over two registers. Low byte first this time. (The MPU-6050 was high byte first, so don't copy the byte order from that guide.)

**The inverse ID is a neat trick.** `Product_ID XOR Inverse_Product_ID` should equal `0xFF`. A floating bus gives `0x00` and `0x00` or `0xFF` and `0xFF`, which fail the test. So a matching pair proves a real chip is answering, whatever the exact ID value.

---

## 2. Build the circuit

**Unplug the Pico before wiring.**

### Voltage warning

Use **3.3V** from the Pico's 3V3(OUT) pin. **Never connect 5V** (the VBUS pin) unless your module's documentation explicitly says its input is 5V-tolerant. PixArt sensors are low-voltage chips, and many breakouts have no protection. **[verify]** the supply range printed on your module or its listing before powering it.

### Connections

| Sensor pin (names vary by module) | Pico pin | Pico label | Why |
|---|---|---|---|
| **VDD / VCC / 3V3** | Pin 36 | 3V3(OUT) | Power |
| **GND** | Pin 23 | GND | Common ground |
| **SCLK / SCK** | Pin 24 | GP18 (SPI0 SCK) | Clock |
| **MOSI / SDIO-in** | Pin 25 | GP19 (SPI0 TX) | Pico to sensor |
| **MISO / SDIO-out** | Pin 21 | GP16 (SPI0 RX) | Sensor to Pico |
| **NCS / CS** | Pin 22 | GP17 | Chip select (we drive it as a normal GPIO) |
| **MOTION / MOT** (optional) | Pin 26 | GP20 | Goes low when there is motion. Not needed here |
| **RESET** (optional) | not connected | | Our code resets by software. If your module has a RESET pin that must be tied high, check its docs |

```
                    +------[ USB ]------+
          GP0  [ 1] |o                 o| [40] VBUS   (do NOT use for the sensor)
          GP1  [ 2] |o                 o| [39] VSYS
          GND  [ 3] |o                 o| [38] GND
          GP2  [ 4] |o                 o| [37] 3V3_EN
          GP3  [ 5] |o                 o| [36] 3V3(OUT)  ---> sensor VDD
          GP4  [ 6] |o                 o| [35] ADC_VREF
          GP5  [ 7] |o                 o| [34] GP28
          GND  [ 8] |o                 o| [33] GND
          GP6  [ 9] |o                 o| [32] GP27
          GP7  [10] |o                 o| [31] GP26
          GP8  [11] |o                 o| [30] RUN
          GP9  [12] |o                 o| [29] GP22
          GND  [13] |o                 o| [28] GND
          GP10 [14] |o                 o| [27] GP21
          GP11 [15] |o                 o| [26] GP20  <--- sensor MOTION (optional)
          GP12 [16] |o                 o| [25] GP19  ---> sensor MOSI
          GP13 [17] |o                 o| [24] GP18  ---> sensor SCLK
          GND  [18] |o                 o| [23] GND   ---> sensor GND
          GP14 [19] |o                 o| [22] GP17  ---> sensor NCS
          GP15 [20] |o                 o| [21] GP16  <--- sensor MISO
                    +-------------------+
```

**Why these pins?** Each SPI controller can only use certain pins for each signal. For `spi0`, valid choices include GP16 (MISO), GP18 (SCK), GP19 (MOSI), and GP17 can be CS. They sit together at the bottom right of the Pico, which keeps the wiring tidy. The pin function table in the Pico datasheet lists the alternatives.

**Single-wire variants:** some modules expose a combined data pin (`SDIO`) instead of separate MISO and MOSI. If yours has only one data pin, check its documentation, because this guide's wiring assumes separate MISO and MOSI. **[verify]**

### Wiring checklist

- [ ] VDD to 3V3(OUT), **not** VBUS
- [ ] GND to GND
- [ ] SCLK, MOSI, MISO, CS go to GP18, GP19, GP16, GP17 (MISO and MOSI not swapped)
- [ ] The sensor faces the surface at the correct height (see section 5)

---

## 3. Project setup: Pico project creator options

In VS Code, open the Raspberry Pi Pico extension and choose **New Project**. Here is each option and what to pick.

| Option | Choose | Why |
|---|---|---|
| **Name** | `paw_3360` | Matches the folder and the `CMakeLists.txt` in this repo |
| **Board type** | `Pico` or `Pico W`, whichever you own | Sets `PICO_BOARD`. Wiring and code are identical for both |
| **Location** | `src/mucking_about/paw_3360/` | Keeps it next to this guide |
| **SDK version** | Same as your MPU-6050 project (`2.3.1`) | Avoids a second SDK install and keeps projects consistent |
| **Toolchain / picotool** | Defaults | Same reason |
| **Features: SPI** | **Tick** | Adds the `hardware_spi` library, which gives us `spi_init`, `spi_write_blocking`, and so on |
| **Features: I2C, PIO, PWM, ADC, etc.** | Leave unticked | Not used here. You can add them later |
| **Stdio support: Console over USB** | **Tick** | `printf` appears as a virtual serial port on the same cable. This is the equivalent of Fix 2 in the MPU-6050 guide |
| **Stdio support: Console over UART** | **Untick** | Needs a separate USB-serial adapter |
| **Code generation options** | Defaults (plain C, not C++) | The code is plain C |
| **Pico W options** (if shown) | None / no Wi-Fi | We aren't using wireless |
| **Debugger** | SWD if you own a Debug Probe, else leave | Not needed for this guide |

### If you reuse the repo's files instead of generating a project

The repo already has a generated project in `paw_3360/paw_3360/`, copied from the MPU-6050 one. If you use it, check these in `CMakeLists.txt`:

1. **Board.** `set(PICO_BOARD pico CACHE STRING "Board type")`. Change `pico` to `pico_w` for a Pico W.
2. **Stdio.** UART off, USB on:
   ```cmake
   pico_enable_stdio_uart(paw_3360 0)
   pico_enable_stdio_usb(paw_3360 1)
   ```
3. **Libraries.** It must link `hardware_spi`. Without it you get "undefined reference to `spi_init`":
   ```cmake
   target_link_libraries(paw_3360
           hardware_spi
           hardware_timer
           )
   ```

Leave the `DO NOT EDIT` block at the top alone. Then run **CMake: Configure**. If the extension complains, delete any old `build/` folder and configure again. It is git-ignored.

---

## 4. Write the code, step by step

Open `paw_3360/paw_3360.c`. The finished file is already there. This section explains how it was built up, so you can type it in stages and test each one. After each stage, build, flash, and look at the output (section 5).

### Stage A: "Hello" over USB

```c
#include <stdio.h>
#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    sleep_ms(5000);
    while (true) {
        printf("Hello from the Pico!\n");
        sleep_ms(1000);
    }
}
```

`stdio_init_all()` starts the USB serial channel. The delay matters because anything printed before your terminal connects is lost. If you don't see "Hello", fix that first (section 6).

### Stage B: set up SPI

```c
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define SPI_BAUD 1000000

static inline void cs_select(void)   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(PIN_CS, 1); }

static void spi_setup(void) {
    spi_init(SPI_PORT, SPI_BAUD);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();
}
```

What each piece does:

- `spi_init(..., 1000000)` starts the SPI hardware at 1 MHz. That is deliberately slow. Once it works you can raise it, but PixArt sensors in this family top out at a few MHz. **[verify]**
- `spi_set_format(..., SPI_CPOL_1, SPI_CPHA_1, ...)` picks **mode 3**, as discussed in section 1. 8 bits per transfer, most significant bit first.
- `gpio_set_function(..., GPIO_FUNC_SPI)` hands each pin to the SPI block. This is the same step people forget on I2C: pins default to plain GPIO.
- **CS is deliberately not given to the SPI block.** We drive it with `gpio_put` so we can hold it low across a whole address, wait, data sequence. The hardware CS function would toggle it between bytes, which breaks the sensor's protocol.
- `cs_deselect()` at the end leaves CS **high** (idle), so the sensor ignores us until we start a transaction.

### Stage C: read and write registers, then check the ID

```c
static uint8_t paw_read(uint8_t reg) {
    uint8_t addr = reg & 0x7F;        // bit 7 = 0 -> read
    uint8_t value = 0;
    cs_select();
    spi_write_blocking(SPI_PORT, &addr, 1);
    sleep_us(160);                    // wait for the sensor (tSRAD)
    spi_read_blocking(SPI_PORT, 0x00, &value, 1);
    sleep_us(1);
    cs_deselect();
    sleep_us(20);                     // gap before the next transaction (tSRR)
    return value;
}

static void paw_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };   // bit 7 = 1 -> write
    cs_select();
    spi_write_blocking(SPI_PORT, buf, 2);
    sleep_us(20);
    cs_deselect();
    sleep_us(180);                    // gap after a write (tSWW)
}
```

Notes:

- `spi_read_blocking(..., 0x00, ...)`: SPI is always a swap. To receive a byte you must also send one, and the `0x00` is a harmless dummy value that goes out on MOSI while the sensor's reply comes back on MISO.
- The delays are conservative values taken from the PMW3360 family. They are the most likely thing to adjust if reads are flaky. **[verify]**

Now the identity check:

```c
uint8_t id  = paw_read(0x00);   // Product_ID
uint8_t inv = paw_read(0x3F);   // Inverse_Product_ID
printf("Product_ID = 0x%02X, Inverse = 0x%02X\n", id, inv);
if ((uint8_t)(id ^ inv) == 0xFF) printf("SPI link looks good.\n");
```

**This is the checkpoint for the whole project.** If the pair XORs to `0xFF`, your wiring, power and SPI mode are all correct, and the rest is just reading registers. If not, don't go on. Use the troubleshooting table.

The code deliberately doesn't hard-code the expected ID. Write down what your chip reports (the PMW3360 reports `0x42`, and I believe the PAW3360 is the same but could not confirm), and compare with the datasheet.

### Stage D: power-up and reset

```c
static void sensor_power_up(void) {
    cs_deselect(); sleep_ms(3);
    cs_select();   sleep_ms(1);
    cs_deselect(); sleep_ms(3);       // toggle CS to resync the serial port

    paw_write(0x3A, 0x5A);            // Power_Up_Reset
    sleep_ms(50);                     // wait for it to boot

    paw_read(0x02); paw_read(0x03);   // read motion registers once
    paw_read(0x04); paw_read(0x05);   // to throw away stale data
    paw_read(0x06);
}
```

Why: after power-on the sensor's internal state is undefined. A reset puts it in a known state, and reading the motion registers once clears anything left over. Without this you can see a large bogus delta on the first read.

**[verify]** Some PixArt sensors need extra register writes after reset (for example, loading firmware or setting configuration registers) before they track properly. If the ID check passes but deltas stay zero while you move the sensor, look for an "initialization" or "power-up sequence" section in your datasheet.

### Stage E: read delta X and delta Y

```c
static bool read_delta(int16_t *dx, int16_t *dy) {
    uint8_t motion = paw_read(0x02);   // reading this latches the deltas
    uint8_t xl = paw_read(0x03);
    uint8_t xh = paw_read(0x04);
    uint8_t yl = paw_read(0x05);
    uint8_t yh = paw_read(0x06);

    *dx = (int16_t)(((uint16_t)xh << 8) | xl);
    *dy = (int16_t)(((uint16_t)yh << 8) | yl);
    return (motion & 0x80) != 0;
}
```

Walkthrough:

1. **Read `Motion` first.** It freezes the current X/Y deltas so that all four bytes belong to the same moment. If you read X, then Y, then new movement arrives in between, X and Y won't match.
2. Combine each pair into a **signed** 16-bit number: `high << 8 | low`. Signed matters because left and up are negative.
3. Bit 7 of `Motion` (`0x80`) says "something moved". We only print when it's set, so a still sensor produces no output.

### Stage F: the main loop

```c
int32_t total_x = 0, total_y = 0;
absolute_time_t next = get_absolute_time();

while (true) {
    int16_t dx, dy;
    if (read_delta(&dx, &dy)) {
        total_x += dx;
        total_y += dy;
        printf("dx=%6d dy=%6d | total x=%8ld y=%8ld\n", dx, dy, (long)total_x, (long)total_y);
    }
    next = delayed_by_ms(next, POLL_MS);
    sleep_until(next);
}
```

`delayed_by_ms` and `sleep_until` give a fixed 100 Hz loop with no drift. Each wake-up time is computed from the previous deadline, not from "now". (This is the same pattern as your MPU-6050 loop.)

The running totals show the idea that deltas add up to position. Move the sensor right for a while, and `total x` climbs.

**A note on rate.** One read here takes 5 register reads, about 1 ms with the delays included, so 100 Hz is easy. The sensor can also provide a **burst read** that returns everything in one transaction. That is the standard way to go fast, and a good next step. See section 7.

---

## 5. Build, flash, and view the output

### Build and flash

1. **Compile** with the Pico extension (status bar, or Pico sidebar, Compile Project). This creates `build/paw_3360.uf2`.
2. Unplug the Pico, hold **BOOTSEL**, plug in, release. It appears as a drive called **RPI-RP2**.
3. Click **Run** in the extension, or drag `build/paw_3360.uf2` onto that drive.

### View the output

```bash
ls /dev/ttyACM*                  # usually /dev/ttyACM0
screen /dev/ttyACM0 115200       # exit: Ctrl+A, then K, then Y
```

If you get "permission denied": `sudo usermod -aG dialout $USER`, then log out and in. The Pico extension's serial monitor also works.

### What you should see

First, the identity lines (after the 5 second wait):

```
Product_ID = 0x42, Inverse_Product_ID = 0xBD
ID pair is consistent (ID xor inverse == 0xFF): SPI link looks good.
```

(Your actual ID bytes may differ. What matters is that they XOR to `0xFF`.)

Then, while you slide the sensor across a surface:

```
dx=     3 dy=    -1 | total x=       3 y=      -1
dx=    12 dy=     0 | total x=      15 y=      -1
dx=    -5 dy=     2 | total x=      10 y=       1
```

If it's perfectly still, nothing prints. That is correct, because we only print when the Motion bit says something moved.

### Experiments, to check you understand it

| Do this | You should see |
|---|---|
| Hold still | No output |
| Slide slowly one way | Small deltas of one sign in X (or Y) |
| Slide back | Deltas of the opposite sign, `total` returns toward 0 |
| Slide faster | Bigger numbers per line |
| Slide diagonally | Both dx and dy non-zero |
| Lift it off the surface | Motion stops (or deltas go to 0) |
| Swap which way the sensor is rotated | The X and Y axes swap or flip: orientation decides which direction is "positive" |

**Surface and height.** The sensor needs a textured surface and, for a bare module, the right distance. Mouse sensors are designed for roughly 2 to 3 mm from the surface through their lens. **[verify]** the "lift detection" or height spec for your part. Try a sheet of printed paper or a mousepad, and avoid glass and mirrors.

---

## 6. Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Terminal shows nothing at all | USB stdio must be on and UART off (section 3). Wait out the 5 second delay. Use a data cable |
| No `/dev/ttyACM0` | Pico stuck in BOOTSEL. Unplug, plug in without holding the button |
| Build error "undefined reference to `spi_init`" | `hardware_spi` isn't in `target_link_libraries` |
| ID is `0x00` / inverse `0x00` | Nothing is driving MISO. Check power, GND, and the CS wire. Check MISO goes to GP16 |
| ID is `0xFF` / inverse `0xFF` | MISO is stuck high, or the sensor isn't powered, or CS isn't reaching it |
| ID is a random value that changes each run | Wrong SPI mode, or the clock is too fast for your wiring. Confirm `SPI_CPOL_1, SPI_CPHA_1`, and try `SPI_BAUD` of `500000` |
| MISO and MOSI swapped | Same symptom as above: swap the two jumpers |
| ID is stable but doesn't XOR to `0xFF` | The link works but the register map differs from the PMW3360 family. Read the PAW3360 datasheet's register table, and adjust the addresses at the top of the file |
| ID check passes but deltas are always 0 | The surface or height is wrong, or the part needs extra initialization (Stage D note). Try a different surface, and check the datasheet's power-up sequence |
| Huge delta on the first line | The post-reset read-and-discard in `sensor_power_up()` was skipped |
| Deltas jump to huge values | A signed/unsigned mix-up. Check that dx and dy are `int16_t` and the high byte is shifted by 8 |
| Works, then glitches | Loose breadboard contacts, or wires too long. Shorten them and lower `SPI_BAUD` |
| Direction is reversed | Normal. It depends on how the sensor is mounted. Negate the value in code, or rotate the module |

**How to tell wiring faults from register-map differences:** wiring and power faults give `0x00` or `0xFF` or a random ID. A register-map difference gives a **stable, non-trivial** ID that just doesn't match what you expected. The second case means your hardware is fine and only the constants need fixing.

---

## 7. Where to go next

- **Burst mode.** The PMW3360 family has a `Motion_Burst` register that returns motion, X, Y and more in one read, which is the standard fast path. The PMW3360 libraries linked below use it. Check your datasheet for the PAW3360 burst register and timing. **[verify]**
- **Set CPI.** Change the sensor's resolution register to trade sensitivity for noise. Check the datasheet for the register and step size.
- **Use the MOTION pin.** Wire it to GP20 and use a GPIO interrupt, so the Pico only reads when something moved instead of polling.
- **Raise the SPI speed** once everything works.
- **Feed the theremin mouse.** Combine this with the MPU-6050: the PAW3360 gives translation, and the IMU gives orientation. Send both over USB HID, or map them to pitch and volume.

### References

These are open-source drivers for the related PMW3360, useful for cross-checking registers and timings. They are **not** PAW3360 sources:

- [QMK PMW3360 driver header](https://hacktivis.me/git/qmk_firmware/file/drivers/sensors/pmw3360.h.html)
- [PMW3360 Arduino library by Sunjun Kim](https://github.com/SunjunKim/PMW3360_Arduino)
- [Nordic nRF Connect PMW3360 driver notes](https://nrfconnectdocs.nordicsemi.com/ncs/latest/nrf/drivers/pmw3360.html)
