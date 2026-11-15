# PAW3360 + Raspberry Pi Pico: from parts to live delta X / delta Y

**Goal:** by the end you will have `dx=  12 dy=  -3` lines scrolling in a terminal as you slide the sensor over a desk.

**How this guide works.** Every step has the same four parts:

- **Do**: exactly what to do, in order.
- **Why**: one short reason, so you can reuse the idea later.
- **Expect**: what success looks like, character for character where it matters.
- **If not**: what to check, in order of likelihood.

Never move to the next step until the current step's **Expect** is true. Each stage is a complete file you copy in whole, so there are no fragments to stitch together.

```
  Step 1   Identify your module        ┐
  Step 2   Wire it                      ├─ hardware (no code)
  Step 3   Set up the build loop        ┘
  Step 4   STAGE 1: read the chip ID        ← proves wiring + SPI
  Step 5   STAGE 2: reset + read dx, dy     ← first real data
  Step 6   STAGE 3: firmware + burst reads  ← final, reliable data
```

---

## 0. Read this first: what a "PAW3360" really is

I looked at every public PAW3360 project I could find, plus the PixArt datasheets. The findings change how this guide works, so they come first.

1. **There is no public PAW3360 datasheet.** PixArt publishes the PMW3360 (the "M" part), but not the PAW3360 (the "A" part).
2. **The boards sold as "PAW3360" carry a PMW3360 chip.** The common 10-pin round AliExpress module, silkscreened "PAW3360/3389-Module", reads back Product_ID `0x42`, which is the PMW3360 ([maido-39/pico-pmw3360-debug](https://github.com/maido-39/pico-pmw3360-debug), [Bastardkb Charybdis notes](https://github.com/Bastardkb/Charybdis/blob/main/files/mods/bottom_adapter_alternative_sensors/README.md)). So the **PMW3360 datasheet and drivers are the right reference**, and this guide uses them.
3. **Genuine newer PixArt "PAW" parts are different.** The PAW3395, for example, has a different burst register (`0x16`) and a long list of init writes ([PAW3395 driver](https://github.com/kirltrz/PAW3395_Arduino_ESP32)). A PMW3360 recipe will not work on those.
4. **So Step 4 reads the chip ID and decides.** If it is `0x42`, everything below applies. If not, you stop, and nothing below is guessed.

**What I verified and what I could not.**

| Verified | Not verified |
|---|---|
| All three stages compile with the Pico SDK 2.3.1 with zero warnings | I have no hardware, so none of this has run on your board |
| Register addresses, timings and the SPI limit match the PMW3360 datasheet (v1.30/R1.50) | Whether your module is a PMW3360 (Step 4 tells you) |
| Power-up, SROM and burst sequences match three independent drivers (SunjunKim, jfedor2, the STM32 project below) | Exact behaviour of Stage 2 *without* the SROM: some boards track, some give zeros. Stage 3 is the reliable path |

---

## 1. Identify your module

**Do.** Find the pin labels printed on your module and match them against this table. Pin names vary a little between sellers.

| Typical label | Meaning | Goes to |
|---|---|---|
| `VDD` / `VCC` / `VI` | Main power in | Pico 3V3(OUT) (see voltage box) |
| `VDDIO` / `VIO` | Signal voltage reference | Pico 3V3(OUT) |
| `NRESET` / `RST` / `RS` | Chip reset, **active low** | **Pico 3V3(OUT)** (tied high) |
| `GND` | Ground | Pico GND |
| `NCS` / `CS` / `SS` | Chip select, active low | GP17 |
| `SCLK` / `SCK` / `SC` | SPI clock | GP18 |
| `MOSI` / `MO` | Data Pico → sensor | GP19 |
| `MISO` / `MI` | Data sensor → Pico | GP16 |
| `MOT` / `MT` | Goes low when there is motion | GP20 (optional, not used until the end) |

> **Voltage box: read before powering.**
> The bare chip is a **1.8 to 2.1 V** part on VDD. Giving it 3.3 V directly **destroys it**. The common round module has a 2.0 V regulator on its VDD pin, which makes 3.3 V safe ([maido-39's notes](https://github.com/maido-39/pico-pmw3360-debug): module VDD 1.8 to 6.0 V, VDDIO 1.8 to 3.6 V).
> - Module with a small regulator chip next to the VDD pin, or a listing that says "VDD 1.8-6V": **3.3 V is fine.**
> - Bare chip on a plain breakout with no regulator: **do not use 3.3 V on VDD.** Stop and tell me what board you have.
> - Never use the Pico's 5 V (VBUS) pin.

> **NRESET warning.** The chip has no internal pull-up on NRESET. If it floats, the chip can sit in reset and every read returns `0x00` or `0xFF`. **Tie NRESET to 3.3 V.** (The previous version of this guide said to leave it unconnected. That was wrong for many modules.)

> **Lens.** The sensor only tracks with its lens fitted, with the **surface about 2.4 mm** (±0.2 mm) from the lens reference plane. Without a lens, Stages 1 to 3 can still read the ID, but you will get zero movement.

**Expect.** You have a written list of your module's pin labels and know whether it has a regulator.

**If not.** Don't wire anything yet. Photograph the board, check the listing, and ask.

---

## 2. Wire it

**Do.** Unplug the Pico from USB. Then connect:

| Module pin | Pico pin number | Pico label | Colour suggestion |
|---|---|---|---|
| VDD | 36 | 3V3(OUT) | red |
| VDDIO | 36 | 3V3(OUT) | red |
| NRESET | 36 | 3V3(OUT) | orange |
| GND | 38 | GND | black |
| NCS | 22 | GP17 | yellow |
| SCLK | 24 | GP18 | green |
| MOSI | 25 | GP19 | blue |
| MISO | 21 | GP16 | white |
| MOT | not connected for now | | |

Pin 36 feeds three module pins. That is fine: they are all just "3.3 V". Use a breadboard rail to share it.

```
                    +------[ USB ]------+
          GP0  [ 1] |o                 o| [40] VBUS    (never use)
          GP1  [ 2] |o                 o| [39] VSYS
          GND  [ 3] |o                 o| [38] GND  ---> module GND
          GP2  [ 4] |o                 o| [37] 3V3_EN
          GP3  [ 5] |o                 o| [36] 3V3(OUT) ---> module VDD, VDDIO, NRESET
          GP4  [ 6] |o                 o| [35] ADC_VREF
          GP5  [ 7] |o                 o| [34] GP28
          GND  [ 8] |o                 o| [33] GND
          GP6  [ 9] |o                 o| [32] GP27
          GP7  [10] |o                 o| [31] GP26
          GP8  [11] |o                 o| [30] RUN
          GP9  [12] |o                 o| [29] GP22
          GND  [13] |o                 o| [28] GND
          GP10 [14] |o                 o| [27] GP21
          GP11 [15] |o                 o| [26] GP20  <--- module MOT (later)
          GP12 [16] |o                 o| [25] GP19  ---> module MOSI
          GP13 [17] |o                 o| [24] GP18  ---> module SCLK
          GND  [18] |o                 o| [23] GND
          GP14 [19] |o                 o| [22] GP17  ---> module NCS
          GP15 [20] |o                 o| [21] GP16  <--- module MISO
                    +-------------------+
```

Keep the jumper wires **short** (under about 10 cm). Long wires are the most common cause of the SROM upload in Stage 3 failing ([maido-39](https://github.com/maido-39/pico-pmw3360-debug)).

The project already uses these pins (GP16 to 20 are all in `CLAUDE.md`), so what you learn here carries over to the final firmware.

**Why these pins.** GP16 = SPI0 RX (MISO), GP18 = SPI0 SCK, GP19 = SPI0 TX (MOSI). GP17 is chip select, and we drive it by hand as a plain GPIO.

**Expect (before powering).** Check this list, then check it again:

- [ ] VDD, VDDIO and NRESET all reach the 3V3(OUT) pin (pin 36)
- [ ] GND reaches a Pico GND pin
- [ ] MISO is on GP16 and MOSI on GP19 (they are the easiest pair to swap)
- [ ] With a multimeter in continuity mode, 3V3 and GND are **not** connected to each other
- [ ] Nothing is connected to VBUS (pin 40)

**If not.** Fix it now. Nothing below works on bad wiring.

---

## 3. Set up the build loop

You will repeat the same four-part loop at every stage. Learn it once here.

### 3a. Check the project once

The project already exists at `src/mucking_about/paw_3360/paw_3360/`. Open that folder in VS Code and check `CMakeLists.txt` has all three of these:

```cmake
pico_enable_stdio_uart(paw_3360 0)
pico_enable_stdio_usb(paw_3360 1)

target_link_libraries(paw_3360
        hardware_spi
        hardware_timer
        )
```

They do in the repo as shipped. If you use a **Pico W**, change `set(PICO_BOARD pico ...)` to `pico_w`.

### 3b. The loop (used at every stage)

1. **Copy the stage file** over `paw_3360.c`. From `src/mucking_about/paw_3360/`:
   ```bash
   cp stages/stage1.c paw_3360/paw_3360.c      # use stage2.c, stage3.c later
   ```
2. **Compile**: Pico extension sidebar, *Compile Project* (or run CMake: Build). It should finish with no red errors and create `build/paw_3360.uf2`.
3. **Flash**: unplug the Pico, hold **BOOTSEL**, plug it in, release. A drive called `RPI-RP2` appears. Click *Run* in the extension, or drag `build/paw_3360.uf2` onto the drive. The Pico reboots by itself.
4. **Watch the output**:
   ```bash
   ls /dev/ttyACM*                  # usually /dev/ttyACM0
   screen /dev/ttyACM0 115200       # leave with: Ctrl+A, then K, then Y
   ```
   If you get "permission denied": `sudo usermod -aG dialout $USER`, then log out and back in.

**Tip.** Every stage waits (up to 15 s) for your terminal to connect before printing, so you will not miss the first lines. **Open the terminal right after flashing.** The port disappears and reappears at each reboot, so re-run `screen` if it drops.

**Expect.** You can already run the loop with Stage 1 below. If compile fails, read the first red error line. The usual cause is the wrong folder.

---

## 4. STAGE 1: read the chip ID

**What this stage proves.** That power, wiring, SPI mode and chip select are all correct. It does not move the sensor or reset it. If Stage 1 passes, you are 80 % done.

### Do

Run the loop (Step 3b) with `stage1.c`. Its full contents are:

```c
// paw_3360.c  --  STAGE 1: talk to the sensor and read its ID
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ---- Pins (SPI0) -------------------------------------------------------
#define SPI_PORT  spi0
#define PIN_MISO  16
#define PIN_CS    17
#define PIN_SCK   18
#define PIN_MOSI  19
#define SPI_BAUD  500000      // 500 kHz. The sensor's maximum is 2 MHz.

// ---- Registers ---------------------------------------------------------
#define REG_PRODUCT_ID      0x00
#define REG_REVISION_ID     0x01
#define REG_INVERSE_ID      0x3F

// ---- Chip select (active low) -----------------------------------------
static inline void cs_select(void)   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(PIN_CS, 1); }

static void spi_setup(void) {
    spi_init(SPI_PORT, SPI_BAUD);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); // mode 3
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();
}

// ---- Register access ---------------------------------------------------
static uint8_t paw_read(uint8_t reg) {
    uint8_t addr = reg & 0x7F;           // bit 7 = 0 means READ
    uint8_t value = 0;
    cs_select();
    sleep_us(1);                         // tNCS-SCLK: NCS low before first clock
    spi_write_blocking(SPI_PORT, &addr, 1);
    sleep_us(160);                       // tSRAD: sensor fetches the data
    spi_read_blocking(SPI_PORT, 0x00, &value, 1);
    sleep_us(1);                         // tSCLK-NCS (read)
    cs_deselect();
    sleep_us(20);                        // tSRR: gap before the next command
    return value;
}

static void paw_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };   // bit 7 = 1 means WRITE
    cs_select();
    sleep_us(1);
    spi_write_blocking(SPI_PORT, buf, 2);
    sleep_us(35);                        // tSCLK-NCS (write)
    cs_deselect();
    sleep_us(180);                       // tSWW / tSWR: gap before the next command
}

// ---- Wait for the USB serial terminal so no output is lost -------------
static void wait_for_terminal(void) {
    for (int i = 0; i < 150 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(500);
}

int main(void) {
    stdio_init_all();
    wait_for_terminal();
    spi_setup();

    printf("\n=== PAW3360 stage 1: ID check ===\n");
    uint8_t id  = paw_read(REG_PRODUCT_ID);
    uint8_t inv = paw_read(REG_INVERSE_ID);
    uint8_t rev = paw_read(REG_REVISION_ID);
    printf("Product_ID = 0x%02X   Inverse_ID = 0x%02X   Revision_ID = 0x%02X\n", id, inv, rev);

    if ((uint8_t)(id ^ inv) != 0xFF) {
        printf("FAIL: ID and inverse do not XOR to 0xFF. The sensor is not answering.\n");
        printf("      Go to the stage 1 troubleshooting table.\n");
    } else if (id == 0x42) {
        printf("PASS: PMW3360 core found (ID 0x42). Go on to stage 2.\n");
    } else {
        printf("The sensor answers but its ID is 0x%02X, not 0x42. Stop and read\n", id);
        printf("the 'unexpected ID' note in the guide before going on.\n");
    }

    while (true) sleep_ms(1000);
}
```

### Why: what the code does

SPI has no ACK like I2C, so a disconnected sensor does not report an error. It just returns `0x00` or `0xFF`. We use the **inverse ID trick**: register `0x3F` always holds the bitwise NOT of register `0x00`. If `ID XOR Inverse == 0xFF`, a real chip is answering, because floating or stuck lines can never give that.

One register read is this exact sequence (this is where every number in `paw_read` comes from):

```
NCS   ‾‾‾‾\_____________________________________________/‾‾‾‾‾‾‾‾‾‾
SCLK  ‾‾‾‾‾\_/‾\_/‾ ... 8 clocks ... ‾‾‾‾‾‾‾\_/‾\_/‾ ... 8 clocks ‾‾‾‾‾
MOSI       [ 0 a a a a a a a ]      (idle)
            └ bit 7 = 0 → READ, bits 6..0 = register address
MISO       (idle)  . . . .  tSRAD  . . . . [ d d d d d d d d ]
                   └─ wait ≥160 µs ─┘        └ the register's value
                                                           ↑
                                                       then NCS high,
                                                       wait ≥20 µs (tSRR)
```

| Number in code | Datasheet name | Meaning |
|---|---|---|
| `sleep_us(160)` | tSRAD | After sending the address, the sensor needs ≥160 µs to fetch the data. Reading earlier gives garbage |
| `sleep_us(20)` | tSRR | Minimum gap between a read and the next command |
| `sleep_us(35)` + `sleep_us(180)` | tSCLK-NCS (write), tSWW | Hold NCS low 35 µs after a write's last bit, then wait 180 µs before the next command |
| `SPI_BAUD 500000` | fSCLK ≤ 2 MHz | We run at 500 kHz to be forgiving of jumper wires |
| `SPI_CPOL_1, SPI_CPHA_1` | SPI mode 3 | Clock idles **high**, data is sampled on the **rising** edge. The wrong mode gives garbage |

A **write** is the same but the first byte has bit 7 = 1 (`reg | 0x80`) and the second byte is the value, so there is no waiting for data.

### Expect

```
=== PAW3360 stage 1: ID check ===
Product_ID = 0x42   Inverse_ID = 0xBD   Revision_ID = 0x01
PASS: PMW3360 core found (ID 0x42). Go on to stage 2.
```

(Revision_ID can differ. `0x42` / `0xBD` is the important pair.)

### If not

Match what you saw:

| You see | Meaning | Do |
|---|---|---|
| Nothing at all | Terminal not connected, or the Pico is not running | Re-open `screen`; re-flash; try a different USB cable (must be a **data** cable) |
| `ID = 0x00`, `Inverse = 0x00` | MISO stuck low: no power, no ground, or MISO not connected | Check VDD and GND reach the module. Check the MISO wire (GP16) |
| `ID = 0xFF`, `Inverse = 0xFF` | MISO floating high, or the chip is held in reset or not selected | Check **NRESET is tied to 3.3 V**. Check NCS (GP17) |
| Two different values that do **not** XOR to `0xFF` | The sensor is talking but the bits are misaligned: wrong SPI mode, or a MOSI/MISO swap, or wires too long | Check you flashed `stage1.c` unmodified. Swap-check MISO/MOSI. Shorten wires |
| Values that change every run | Loose connection or noisy long wires | Reseat every wire; shorten them |
| `XOR == 0xFF` but ID is **not** `0x42` (e.g. `0x47`) | The chip works but is not a PMW3360. `0x47` is a PMW3389; a genuine PAW part will show its own ID | **Stop here.** Do not run Stages 2 or 3. Tell me the ID and I will rewrite the recipe for that chip |

Do not go on until you see the `PASS` line.

---

## 5. STAGE 2: reset the sensor and read delta X / delta Y

**What this stage proves.** That you can get movement numbers out of the chip using plain register reads. This is the simplest possible data path, and the first time you see live numbers.

### Do

Run the loop with `stage2.c`. Full contents:

```c
// paw_3360.c  --  STAGE 2: reset the sensor and read delta X / delta Y
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ---- Pins (SPI0) -------------------------------------------------------
#define SPI_PORT  spi0
#define PIN_MISO  16
#define PIN_CS    17
#define PIN_SCK   18
#define PIN_MOSI  19
#define SPI_BAUD  500000      // 500 kHz. The sensor's maximum is 2 MHz.

// ---- Registers ---------------------------------------------------------
#define REG_PRODUCT_ID      0x00
#define REG_REVISION_ID     0x01
#define REG_MOTION          0x02
#define REG_DELTA_X_L       0x03
#define REG_DELTA_X_H       0x04
#define REG_DELTA_Y_L       0x05
#define REG_DELTA_Y_H       0x06
#define REG_SQUAL           0x07
#define REG_POWER_UP_RESET  0x3A
#define REG_INVERSE_ID      0x3F

// ---- Chip select (active low) -----------------------------------------
static inline void cs_select(void)   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(PIN_CS, 1); }

static void spi_setup(void) {
    spi_init(SPI_PORT, SPI_BAUD);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); // mode 3
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();
}

// ---- Register access ---------------------------------------------------
static uint8_t paw_read(uint8_t reg) {
    uint8_t addr = reg & 0x7F;           // bit 7 = 0 means READ
    uint8_t value = 0;
    cs_select();
    sleep_us(1);                         // tNCS-SCLK: NCS low before first clock
    spi_write_blocking(SPI_PORT, &addr, 1);
    sleep_us(160);                       // tSRAD: sensor fetches the data
    spi_read_blocking(SPI_PORT, 0x00, &value, 1);
    sleep_us(1);                         // tSCLK-NCS (read)
    cs_deselect();
    sleep_us(20);                        // tSRR: gap before the next command
    return value;
}

static void paw_write(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };   // bit 7 = 1 means WRITE
    cs_select();
    sleep_us(1);
    spi_write_blocking(SPI_PORT, buf, 2);
    sleep_us(35);                        // tSCLK-NCS (write)
    cs_deselect();
    sleep_us(180);                       // tSWW / tSWR: gap before the next command
}

// ---- Wait for the USB serial terminal so no output is lost -------------
static void wait_for_terminal(void) {
    for (int i = 0; i < 150 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(500);
}

// ---- Power-up: put the sensor in a known state ------------------------
static void sensor_reset(void) {
    cs_deselect(); sleep_us(40);
    cs_select();   sleep_us(40);
    cs_deselect(); sleep_us(40);         // toggling NCS resyncs the serial port
    paw_write(REG_POWER_UP_RESET, 0x5A); // 0x5A = "reset now"
    sleep_ms(50);                        // tMOT-RST: sensor reboots
    paw_read(REG_MOTION);                // read 0x02..0x06 once and throw the
    paw_read(REG_DELTA_X_L);             // values away (stale data from before
    paw_read(REG_DELTA_X_H);             // the reset)
    paw_read(REG_DELTA_Y_L);
    paw_read(REG_DELTA_Y_H);
}

// ---- Read one (dx, dy) pair; returns the Motion register ---------------
static uint8_t read_delta(int16_t *dx, int16_t *dy) {
    paw_write(REG_MOTION, 0x01);         // any write to Motion + a read of it
    uint8_t motion = paw_read(REG_MOTION); // freezes the four delta registers
    uint8_t xl = paw_read(REG_DELTA_X_L);
    uint8_t xh = paw_read(REG_DELTA_X_H);
    uint8_t yl = paw_read(REG_DELTA_Y_L);
    uint8_t yh = paw_read(REG_DELTA_Y_H);
    *dx = (int16_t)((xh << 8) | xl);     // low byte first, signed 16-bit
    *dy = (int16_t)((yh << 8) | yl);
    return motion;
}

int main(void) {
    stdio_init_all();
    wait_for_terminal();
    spi_setup();

    printf("\n=== PAW3360 stage 2: delta X / delta Y ===\n");
    uint8_t id  = paw_read(REG_PRODUCT_ID);
    uint8_t inv = paw_read(REG_INVERSE_ID);
    printf("Product_ID = 0x%02X   Inverse_ID = 0x%02X\n", id, inv);
    if ((uint8_t)(id ^ inv) != 0xFF || id != 0x42) {
        printf("STOP: sensor not answering, or not a PMW3360 core (ID must be 0x42).\n");
        printf("      Go back to stage 1.\n");
        while (true) sleep_ms(1000);
    }

    sensor_reset();
    printf("Reset done. Slide the sensor over a surface.\n");
    printf("(Silence is normal while it is still. A status line prints every 2 s.)\n\n");

    int32_t total_x = 0, total_y = 0;
    uint32_t last_status_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        int16_t dx, dy;
        uint8_t motion = read_delta(&dx, &dy);

        if (motion & 0x80) {             // bit 7 = MOT: moved since last read
            total_x += dx;
            total_y += dy;
            printf("dx=%6d dy=%6d   total x=%8ld y=%8ld   (Motion=0x%02X)\n",
                   dx, dy, (long)total_x, (long)total_y, motion);
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_status_ms >= 2000) {
            last_status_ms = now;
            printf("[alive] Motion=0x%02X  SQUAL=%u\n", motion, paw_read(REG_SQUAL));
        }
        sleep_ms(10);                    // ~100 reads per second
    }
}
```

### Why: what is new

1. **`sensor_reset()`** puts the chip in a known state. Writing `0x5A` to `Power_Up_Reset` (`0x3A`) reboots the sensor. We wait 50 ms (the datasheet's tMOT-RST), then read registers `0x02` to `0x06` once and discard them, because they hold stale values from before the reset. Without this you can get a huge bogus first delta.
2. **`read_delta()`** returns movement **since the last read**, not a position. Both axes are **signed 16-bit** numbers split across two registers, **low byte first**:

   ```
   Delta_X_L (0x03)  Delta_X_H (0x04)      dx = (int16_t)( H << 8 | L )
        0x0C              0x00      →  dx = +12
        0xF4              0xFF      →  dx = -12     (0xFFF4 as signed)
   ```
   Writing then reading `Motion` (`0x02`) freezes all four delta registers, so X and Y belong to the same instant.
3. **Bit 7 of `Motion`** (`0x80`, "MOT") is set if the sensor moved since the last read. We only print when it is set, so a still sensor is silent.
4. **Totals.** `total_x` and `total_y` are just the deltas added up. That is how a position is built from a relative sensor.
5. **`[alive]` line every 2 s** prints `SQUAL` (surface quality) so silence does not look like a crash.

### Expect

First the header:

```
=== PAW3360 stage 2: delta X / delta Y ===
Product_ID = 0x42   Inverse_ID = 0xBD
Reset done. Slide the sensor over a surface.
(Silence is normal while it is still. A status line prints every 2 s.)

[alive] Motion=0x20  SQUAL=0
```

Then, as you slide the sensor across a surface 2.4 mm below the lens (these numbers are **illustrative**, yours will differ):

```
dx=     5 dy=    -1   total x=       5 y=      -1   (Motion=0x80)
dx=    11 dy=     0   total x=      16 y=      -1   (Motion=0x80)
dx=     9 dy=     2   total x=      25 y=       1   (Motion=0x80)
[alive] Motion=0x20  SQUAL=47
```

- Sliding one way makes `total x` climb; the other way makes it fall. Which direction is positive depends on how the chip is mounted. Just note it.
- `SQUAL` should rise to **tens** (rough guide: 30+ is a healthy surface) when you hold the sensor over a textured surface, and sit near 0 over nothing.

### If not

| You see | Do |
|---|---|
| `STOP: sensor not answering...` | Go back to Stage 1 |
| `[alive]` lines appear but **no dx/dy ever** and SQUAL stays 0 | The sensor sees no surface. Check the lens is fitted, the surface is ~2.4 mm from the lens, and the surface is not glass or mirror. Try paper or a desk |
| `[alive]` shows SQUAL in the tens but still **no dx/dy** | This can happen without the SROM firmware on some boards. **This is not an error.** Go to Stage 3, which loads it |
| dx/dy are huge and random while still | Noise on a long wire, or the discard reads were skipped. Shorten wires |
| dx and dy swap or invert | Just how the chip is mounted. Fix it in software later |

Stage 2 working is enough to start experimenting. Stage 3 is the reliable, fast version.

---

## 6. STAGE 3: load the firmware, set CPI, read in burst mode

**What this stage adds, and why you want it.**

1. **SROM firmware.** The PMW3360 has a small program memory that PixArt's own sequence fills at every power-up (4094 bytes). Drivers for this chip all do it, including [SunjunKim](https://github.com/SunjunKim/PMW3360), [jfedor2](https://github.com/jfedor2/rp2040-pmw3360), and the QMK/Nordic ones. The datasheet's start-up procedure includes it (jfedor2's code cites page 18).
2. **CPI** (counts per inch): how many counts one inch of travel makes. We set 1600.
3. **Burst read.** One transaction returns motion, dx, dy, surface quality and exposure together, faster and with all values from the same instant.

### Do, part 1: generate `srom.h`

The 4094 firmware bytes are not typed into this guide. A small script downloads them from SunjunKim's library and writes a header file.

```bash
cd src/mucking_about/paw_3360
python3 make_srom.py
```

**Expect:**

```
Wrote paw_3360/srom.h: 4094 bytes, checksum(sum) = 0x08f6
```

If the byte count is not 4094, the script refuses to write the file. **If not:** check you have internet access and are in the `paw_3360` folder that contains `make_srom.py`. (The script and the data come from an LGPL-2.1 library: [SunjunKim/PMW3360](https://github.com/SunjunKim/PMW3360).)

### Do, part 2: flash `stage3.c`

Run the loop (Step 3b) with `stage3.c`. Full contents:

```c
// paw_3360.c  --  STAGE 3: SROM firmware, CPI, burst reads (final)
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "srom.h"                 // generated by make_srom.py

// ---- Pins (SPI0) -------------------------------------------------------
#define SPI_PORT  spi0
#define PIN_MISO  16
#define PIN_CS    17
#define PIN_SCK   18
#define PIN_MOSI  19
#define SPI_BAUD  500000      // 500 kHz. The sensor's maximum is 2 MHz.
#define CPI       1600        // counts per inch, 100..12000 in steps of 100

// ---- Registers ---------------------------------------------------------
#define REG_PRODUCT_ID      0x00
#define REG_REVISION_ID     0x01
#define REG_MOTION          0x02
#define REG_DELTA_X_L       0x03
#define REG_DELTA_X_H       0x04
#define REG_DELTA_Y_L       0x05
#define REG_DELTA_Y_H       0x06
#define REG_SQUAL           0x07
#define REG_CONFIG1         0x0F
#define REG_CONFIG2         0x10
#define REG_SROM_ENABLE     0x13
#define REG_SROM_ID         0x2A
#define REG_POWER_UP_RESET  0x3A
#define REG_SHUTDOWN        0x3B
#define REG_INVERSE_ID      0x3F
#define REG_MOTION_BURST    0x50
#define REG_SROM_LOAD_BURST 0x62

// ---- Chip select (active low) -----------------------------------------
static inline void cs_select(void)   { gpio_put(PIN_CS, 0); }
static inline void cs_deselect(void) { gpio_put(PIN_CS, 1); }

static void spi_setup(void) {
    spi_init(SPI_PORT, SPI_BAUD);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); // mode 3
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    cs_deselect();
}

// ---- Register access ---------------------------------------------------
// Burst mode (stage 3) is switched off by ANY other register access, so the
// two helpers below clear this flag and read_burst() sets it again.
static bool in_burst = false;

static uint8_t paw_read(uint8_t reg) {
    in_burst = false;
    uint8_t addr = reg & 0x7F;           // bit 7 = 0 means READ
    uint8_t value = 0;
    cs_select();
    sleep_us(1);                         // tNCS-SCLK: NCS low before first clock
    spi_write_blocking(SPI_PORT, &addr, 1);
    sleep_us(160);                       // tSRAD: sensor fetches the data
    spi_read_blocking(SPI_PORT, 0x00, &value, 1);
    sleep_us(1);                         // tSCLK-NCS (read)
    cs_deselect();
    sleep_us(20);                        // tSRR: gap before the next command
    return value;
}

static void paw_write(uint8_t reg, uint8_t value) {
    in_burst = false;
    uint8_t buf[2] = { (uint8_t)(reg | 0x80), value };   // bit 7 = 1 means WRITE
    cs_select();
    sleep_us(1);
    spi_write_blocking(SPI_PORT, buf, 2);
    sleep_us(35);                        // tSCLK-NCS (write)
    cs_deselect();
    sleep_us(180);                       // tSWW / tSWR: gap before the next command
}

// ---- Wait for the USB serial terminal so no output is lost -------------
static void wait_for_terminal(void) {
    for (int i = 0; i < 150 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(500);
}

// ---- Power-up: put the sensor in a known state ------------------------
static void sensor_reset(void) {
    cs_deselect(); sleep_us(40);
    cs_select();   sleep_us(40);
    cs_deselect(); sleep_us(40);          // toggling NCS resyncs the serial port

    // The Pico may have been re-flashed while the sensor stayed powered, so
    // the sensor could be in any state. Shutdown first, then reset.
    paw_write(REG_SHUTDOWN, 0xB6);
    sleep_ms(300);

    cs_select();   sleep_us(40);
    cs_deselect(); sleep_us(40);
    paw_write(REG_POWER_UP_RESET, 0x5A);  // 0x5A = "reset now"
    sleep_ms(50);                         // tMOT-RST: sensor reboots
    paw_read(REG_MOTION);                 // read 0x02..0x06 once and discard
    paw_read(REG_DELTA_X_L);
    paw_read(REG_DELTA_X_H);
    paw_read(REG_DELTA_Y_L);
    paw_read(REG_DELTA_Y_H);
}

// ---- Load the 4094-byte SROM firmware into the sensor -------------------
// Returns true if the sensor then reports the expected SROM_ID.
static bool sensor_load_srom(uint8_t *id_out) {
    paw_write(REG_CONFIG2, 0x00);         // disable Rest mode (wired mouse setting)
    paw_write(REG_SROM_ENABLE, 0x1D);     // "get ready for a download"
    sleep_ms(10);                         // wait more than one frame period
    paw_write(REG_SROM_ENABLE, 0x18);     // "start the download"

    in_burst = false;
    uint8_t cmd = REG_SROM_LOAD_BURST | 0x80;
    cs_select();                          // NCS stays LOW for the whole image
    sleep_us(1);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    sleep_us(15);
    for (int i = 0; i < SROM_LENGTH; i++) {
        spi_write_blocking(SPI_PORT, &srom_data[i], 1);
        sleep_us(15);                     // >= 15 us between bytes
    }
    sleep_us(1);
    cs_deselect();
    sleep_us(200);

    *id_out = paw_read(REG_SROM_ID);      // must be the FIRST access afterwards
    return *id_out == SROM_EXPECTED_ID;
}

// ---- Resolution --------------------------------------------------------
static bool sensor_set_cpi(unsigned cpi) {
    uint8_t v = (uint8_t)(cpi / 100 - 1); // register = CPI/100 - 1
    paw_write(REG_CONFIG1, v);
    return paw_read(REG_CONFIG1) == v;    // read back to prove it stuck
}

// ---- Burst read: Motion + dx + dy + SQUAL + ... in ONE transaction ------
typedef struct {
    bool     moved;      // MOT bit: movement since the last read
    bool     lifted;     // true = sensor is off the surface
    int16_t  dx, dy;
    uint8_t  squal;      // surface quality, 0..~128. Higher = better tracking
    uint16_t shutter;    // exposure; goes up when the surface is dark / far
    uint8_t  raw_motion; // the Motion byte itself, for debugging
} burst_t;

static bool read_burst(burst_t *out) {
    uint8_t buf[12];
    if (!in_burst) {
        paw_write(REG_MOTION_BURST, 0x00);  // any write here ENTERS burst mode
        in_burst = true;
    }
    uint8_t cmd = REG_MOTION_BURST;
    cs_select();
    sleep_us(1);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    sleep_us(35);                           // tSRAD_MOTBR (burst is faster than 160)
    spi_read_blocking(SPI_PORT, 0x00, buf, 12);
    sleep_us(1);
    cs_deselect();
    sleep_us(2);                            // tBEXIT: >= 500 ns before the next use

    // Motion bits 2..0 should read 0 in burst mode. If not, the sensor left
    // burst mode: re-enter next time and throw this sample away.
    if (buf[0] & 0x07) { in_burst = false; return false; }

    out->raw_motion = buf[0];
    out->moved   = (buf[0] & 0x80) != 0;
    out->lifted  = (buf[0] & 0x08) != 0;
    out->dx      = (int16_t)((buf[3] << 8) | buf[2]);
    out->dy      = (int16_t)((buf[5] << 8) | buf[4]);
    out->squal   = buf[6];
    out->shutter = (uint16_t)((buf[11] << 8) | buf[10]);
    return true;
}

int main(void) {
    stdio_init_all();
    wait_for_terminal();
    spi_setup();

    printf("\n=== PAW3360 stage 3: SROM + burst ===\n");
    uint8_t id  = paw_read(REG_PRODUCT_ID);
    uint8_t inv = paw_read(REG_INVERSE_ID);
    printf("Product_ID = 0x%02X   Inverse_ID = 0x%02X\n", id, inv);
    if ((uint8_t)(id ^ inv) != 0xFF || id != 0x42) {
        printf("STOP: sensor not answering, or not a PMW3360 core (ID must be 0x42).\n");
        printf("      Go back to stage 1.\n");
        while (true) sleep_ms(1000);
    }

    sensor_reset();
    printf("Reset done.\n");

    uint8_t srom_id;
    bool ok = sensor_load_srom(&srom_id);
    printf("SROM_ID = 0x%02X  (expected 0x%02X)  -> %s\n",
           srom_id, SROM_EXPECTED_ID, ok ? "PASS" : "FAIL");
    if (!ok) {
        printf("Upload failed. See the stage 3 troubleshooting table.\n");
        while (true) sleep_ms(1000);
    }

    paw_write(REG_CONFIG2, 0x00);
    printf("CPI set to %d: %s\n", CPI, sensor_set_cpi(CPI) ? "PASS" : "FAIL");
    sleep_ms(10);

    printf("\nReady. Slide the sensor over a surface (about 2.4 mm from the lens).\n\n");

    int32_t total_x = 0, total_y = 0;
    uint32_t last_status_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        burst_t b;
        if (read_burst(&b)) {
            if (b.moved && !b.lifted) {
                total_x += b.dx;
                total_y += b.dy;
                printf("dx=%6d dy=%6d   total x=%8ld y=%8ld   SQUAL=%3u\n",
                       b.dx, b.dy, (long)total_x, (long)total_y, b.squal);
            }
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (now - last_status_ms >= 2000) {
                last_status_ms = now;
                printf("[alive] %s  SQUAL=%u  shutter=%u\n",
                       b.lifted ? "LIFTED" : "on surface", b.squal, b.shutter);
            }
        }
        sleep_ms(5);                     // ~200 reads per second
    }
}
```

### Why: what is new

| Piece | What it does |
|---|---|
| `sensor_reset()` | Now also does **Shutdown (`0xB6`) first**. If you re-flash the Pico while the sensor stays powered, the sensor can be left mid-operation. Shutdown then reset puts it in a known state every time |
| `sensor_load_srom()` | `Config2=0x00` (no rest mode) → `SROM_Enable=0x1D` → wait 10 ms → `SROM_Enable=0x18` → send address `0xE2` (that is `0x62` with bit 7 set, SROM_Load_Burst) → send **4094 bytes with NCS held low the whole time and ≥15 µs between bytes** → NCS high → read `SROM_ID` |
| `SROM_ID` check | Must be the **first** access after the download, and must read `0x04`. That proves the firmware loaded |
| `sensor_set_cpi()` | The register value is `CPI/100 - 1`, so 1600 CPI = `0x0F`. We read it back to prove the write stuck. Range 100 to 12000 |
| `read_burst()` | Write 0 to `Motion_Burst` (`0x50`) **once** to enter burst mode. Then each read: send `0x50`, wait only **35 µs** (tSRAD_MOTBR, shorter than 160), clock out 12 bytes, NCS high |
| `in_burst` flag | **Any other register access leaves burst mode.** `paw_read` and `paw_write` clear the flag, and `read_burst` re-enters if needed |
| `buf[0] & 0x07` check | In a good burst these bits read 0. If not, the sensor dropped out of burst mode: discard that sample and re-enter |

The 12 burst bytes, in order:

| Byte | Content | Used as |
|---|---|---|
| 0 | Motion | bit 7 = moved, bit 3 = **lifted** (1 = off the surface) |
| 1 | Observation | not used |
| 2, 3 | Delta_X_L, Delta_X_H | `dx` (signed 16-bit, low byte first) |
| 4, 5 | Delta_Y_L, Delta_Y_H | `dy` |
| 6 | SQUAL | surface quality |
| 7, 8, 9 | Raw_Data_Sum, Max, Min | not used |
| 10, 11 | Shutter_Lower, Shutter_Upper | exposure time (rises when the surface is dark or too far) |

### Expect

```
=== PAW3360 stage 3: SROM + burst ===
Product_ID = 0x42   Inverse_ID = 0xBD
Reset done.
SROM_ID = 0x04  (expected 0x04)  -> PASS
CPI set to 1600: PASS

Ready. Slide the sensor over a surface (about 2.4 mm from the lens).

dx=     7 dy=    -2   total x=       7 y=      -2   SQUAL= 52
dx=    13 dy=     1   total x=      20 y=      -1   SQUAL= 55
[alive] on surface  SQUAL=54  shutter=640
```

Your numbers will differ from the sample above. Now try:

- Lift the sensor off the surface: the `[alive]` line switches to `LIFTED` and movement lines stop. That is the sensor's lift detection.
- Slide exactly **1 inch (25.4 mm)** at a steady pace and compare `total x` with `1600`. Real-world error of a few percent is normal. (The datasheet quotes 1 % at controlled speeds.)

You now have working delta X / delta Y data. 🎉

### If not

| You see | Meaning | Do |
|---|---|---|
| `SROM_ID = 0x00` | The download did not take | Shorten wires; make sure Stage 1 passes first; lower `SPI_BAUD` to `250000`; re-flash |
| `SROM_ID = 0xFF` | Chip not responding by then, or NRESET not tied high | Re-check the Stage 1 troubleshooting rows |
| `SROM_ID` is some other value | Partial or corrupted upload (noise) | Same as above. Also regenerate `srom.h` (`python3 make_srom.py`) |
| `CPI ... FAIL` | Read-back mismatch, so the SROM probably did not load properly or timing is off | Same as above |
| Build error: `srom.h: No such file` | You skipped part 1 | Run `python3 make_srom.py` in `src/mucking_about/paw_3360` |
| Everything passes but `SQUAL` stays near 0 and `LIFTED` appears while on the desk | Optics: lens missing or wrong height | Check the lens and the 2.4 mm distance; try matte paper |
| `SQUAL` fine, `shutter` is huge (thousands) | Surface too dark, too far, or glossy | Move closer; use paper |

---

## 7. Cheat sheet

**Pins**

```
SCK=GP18  MOSI=GP19  MISO=GP16  CS=GP17  MOT=GP20 (unused)   spi0, mode 3, 500 kHz
```

**Transactions**

```
READ  reg : CS low, send (reg & 0x7F), wait 160 µs, read 1 byte, CS high, wait 20 µs
WRITE reg : CS low, send (reg | 0x80), send value, wait 35 µs, CS high, wait 180 µs
BURST     : write 0 to 0x50 once; then CS low, send 0x50, wait 35 µs, read 12 bytes, CS high
```

**Registers used**

| Addr | Name | Notes |
|---|---|---|
| `0x00` | Product_ID | `0x42` |
| `0x01` | Revision_ID | |
| `0x02` | Motion | bit 7 moved, bit 3 lifted |
| `0x03`-`0x06` | Delta X L/H, Delta Y L/H | signed 16-bit, low byte first |
| `0x07` | SQUAL | surface quality |
| `0x0F` | Config1 | CPI = (value + 1) × 100 |
| `0x10` | Config2 | `0x00` = rest mode off |
| `0x13` | SROM_Enable | `0x1D` then `0x18` |
| `0x2A` | SROM_ID | `0x04` after a good upload |
| `0x3A` | Power_Up_Reset | write `0x5A` |
| `0x3B` | Shutdown | write `0xB6` |
| `0x3F` | Inverse_Product_ID | `0xBD` |
| `0x50` | Motion_Burst | enter / read burst |
| `0x62` | SROM_Load_Burst | write-only |

**Timing**

| Name | Value | Where |
|---|---|---|
| tSRAD | ≥ 160 µs | between read address and data |
| tSRAD_MOTBR | ≥ 35 µs | the same, for burst only |
| tSRR | ≥ 20 µs | after a read |
| tSWW / tSWR | ≥ 180 µs | after a write |
| tMOT-RST | ≥ 50 ms | after Power_Up_Reset |
| fSCLK | ≤ 2 MHz | |

---

## 8. Where to go next

1. **MOTION pin (GP20).** Wire MOT and read it as a GPIO. It goes **low** when there is data, so you only talk to the sensor when something moved. This is how the final firmware will avoid wasting SPI time.
2. **Hand it to the HID mouse.** `dx`/`dy` map straight onto a USB mouse report. In the main project this read must be timer-driven, never blocking the USB task (see `CLAUDE.md`).
3. **Try CPI.** Change `#define CPI` (try 800, 3200) and watch the totals scale.
4. **Try SQUAL / shutter.** They are a free "how good is the surface" signal.

---

## 9. Sources

- PixArt, [PMW3360DM-T2QU product datasheet, R1.50](https://d3s5r33r268y59.cloudfront.net/datasheets/9604/2017-05-07-18-19-11/PMS0058-PMW3360DM-T2QU-DS-R1.50-26092016._20161202173741.pdf): registers, timings, SPI limits, pin list.
- [SunjunKim/PMW3360](https://github.com/SunjunKim/PMW3360): Arduino library. Source of the shutdown and reset sequence, burst read, and the SROM data used by `make_srom.py`.
- [jfedor2/rp2040-pmw3360](https://github.com/jfedor2/rp2040-pmw3360): RP2040 + PMW3360 in C with the Pico SDK. Cross-check for power-up, SROM upload and timing.
- [maido-39/pico-pmw3360-debug](https://github.com/maido-39/pico-pmw3360-debug): the "PAW3360/3389 module" is a PMW3360 (ID `0x42`), module voltages, NRESET pull-up, wiring on a Pico.
- [NNone55555/ELEC_3300_Project](https://github.com/NNone55555/ELEC_3300_Project): an STM32 mouse built on a "PAW3360" board that uses the PMW3360 driver and SROM.
- [Bastardkb/Charybdis](https://github.com/Bastardkb/Charybdis/blob/main/files/mods/bottom_adapter_alternative_sensors/README.md): the cheap "PAW3360 / PMW3389 module".
- [kirltrz/PAW3395_Arduino_ESP32](https://github.com/kirltrz/PAW3395_Arduino_ESP32): evidence that genuine PAW-series parts use a different init and burst register.

*The previous version of this guide is kept as `GUIDE_v1_old.md`.*
