/*
 * tusb_config.h - compile-time settings for TinyUSB. TinyUSB includes this
 * file by name, so it must be on the include path.
 *
 * CFG_TUSB_MCU (RP2040) and CFG_TUSB_OS (Pico SDK) are already set by the
 * SDK's tinyusb_device library; only the device layout is chosen here.
 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

/* Port 0 (the Pico's only USB port) runs as a device, not a host. */
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE

/* Endpoint 0 (control) packet size. 64 is the full-speed maximum and must
 * match bMaxPacketSize0 in the device descriptor. */
#define CFG_TUD_ENDPOINT0_SIZE 64

/* Class drivers: how many interfaces of each kind the device has. */
#define CFG_TUD_CDC    1 /* one serial port (debug text now, IMU frames later) */
#define CFG_TUD_HID    1 /* one HID interface (the mouse) */
#define CFG_TUD_MSC    0
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

/* CDC software FIFOs. TX holds text waiting for the host: 512 bytes is about
 * six status lines, and leaves room for 200 Hz x 23-byte IMU frames later
 * (4600 bytes/s, drained every 1 ms frame). RX only ever carries commands. */
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 512
/* Size of one bulk transfer buffer = the bulk endpoints' wMaxPacketSize. */
#define CFG_TUD_CDC_EP_BUFSIZE 64

/* HID IN transfer buffer; must hold one report (4 bytes) and match the HID
 * endpoint's wMaxPacketSize (8) in usb_descriptors.c. */
#define CFG_TUD_HID_EP_BUFSIZE 8

#endif /* TUSB_CONFIG_H */
