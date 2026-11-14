# Phase 1 spec – Theremin Mouse

Goal: the Pico enumerates as a composite USB device on ONE cable: a HID mouse (2 buttons, joystick Y = scroll wheel, joystick press = middle click, optical sensor = x/y) and a CDC serial port streaming raw IMU data (ax ay az gx gy gz) at 200 Hz to a POSIX C reader on Linux. Out of scope: gestures, fusion, audio.

## One cable rules
- The cable carries power, flashing, the HID mouse and CDC serial.
- Every build keeps the 1200-baud reboot: tud_cdc_line_coding_cb calls reset_usb_boot(0, 0) when the baud rate is 1200. Trigger: stty -F /dev/serial/by-id/<pico> 1200
- Debug output: plain text on CDC up to milestone 4; from milestone 5 everything on CDC is framed, debug as type 0x02 packets.
- Always use the /dev/serial/by-id/ path, never ttyACMn.
- The HID mouse must work whether or not the reader has the port open.

## Milestones
0. Blink + hello over USB serial – DONE
1. Composite device: HID mouse + CDC serial, two buttons click. Proof: clicks work in any app, debug text still prints, 1200-baud reboot works, lsusb -v shows HID + CDC interfaces.
2. Joystick scroll + press = middle click. Proof: a long web page scrolls smoothly at every tilt; no drift at rest.
3. Optical sensor moves the cursor. Proof: cursor tracks the desk, no jumps on fast swipes.
4. IMU read + scaled text printout. Proof: flat on the desk, az ≈ 1 g and gyro ≈ 0.
5. Binary packets + POSIX reader. Proof: steady 200 Hz, zero CRC errors, seq gaps counted.
6. Hardening. Proof: unplug/replug recovers; 10-minute run loses no frames.

## HID mouse report (4 bytes, boot-protocol style)
- byte 0 buttons: bit0 left, bit1 right, bit2 middle (joystick press); 1 = pressed
- byte 1 x: int8, optical dx, -127..127
- byte 2 y: int8, optical dy, -127..127
- byte 3 wheel: int8, positive = scroll up
- bInterval = 1 ms. Queue a report only when something changed.

## Optical clamp-and-carry
s_k = r_(k-1) + delta_k ; x_k = clamp(s_k, -127, 127) ; r_k = s_k - x_k
(delta_k = counts read this cycle, x_k = value sent, r_k = carry kept for the next report)

## Joystick scroll (rate control)
u = (v - v0) / (vmax - v0) ; u = 0 if |u| < d
a_k = a_(k-1) + ks * u * dt ; w_k = trunc(a_k) ; a_k = a_k - w_k
- v = 12-bit ADC reading (0–4095); v0 = centre, measured at boot
- d = deadzone ≈ 0.1; ks = detents per second at full tilt (start at 20); dt = report period
- w_k = wheel value sent; the fractional part stays in the accumulator

## Buttons
Debounce ≈ 5 ms: accept a new state only after it has been stable for 5 ms.

## IMU packet (little-endian, 23 bytes)
| offset | field | type | notes |
| 0 | sync | 0xAA 0x55 | frame start |
| 2 | type | u8 | 0x01 = IMU, 0x02 = log text |
| 3 | seq | u16 | gap = dropped frames |
| 5 | t_us | u32 | microseconds since boot |
| 9 | ax, ay, az | i16 x3 | raw accelerometer counts |
| 15 | gx, gy, gz | i16 x3 | raw gyroscope counts |
| 21 | crc | u16 | CRC-16/CCITT over bytes 2–20 |
Log packets (type 0x02) use the same header, then a u8 length, then text, then the CRC over everything after the sync.
Sent at 200 Hz, timer driven.

## Scaling (done on the host, not the Pico)
a = raw_a / S_a [g] ; w = raw_g / S_g [deg/s]
S_a and S_g come from the IMU datasheet for the chosen full-scale range.

## Reader (host/, POSIX + libc only)
1. open(path, O_RDWR | O_NOCTTY)
2. tcgetattr, cfmakeraw, tcsetattr (raw mode)
3. poll() with timeout (detects unplug)
4. read() into a ring buffer, find 0xAA 0x55, check the CRC; on failure skip one byte and resync
5. Count frames, CRC errors and seq gaps; print a summary each second; optionally write CSV
6. On unplug (read returns 0 or EIO), close the port and reopen it

API:
typedef struct { uint16_t seq; uint32_t t_us; int16_t a[3], g[3]; } imu_sample_t;
int  imu_open(const char *path);                         /* fd or -1 */
int  imu_read(int fd, imu_sample_t *s, int timeout_ms);  /* 1 sample, 0 timeout, -1 error */
void imu_close(int fd);
