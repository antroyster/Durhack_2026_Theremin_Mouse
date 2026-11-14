/*
 * usb_descriptors.c - what the Pico tells the PC it is (spec U1-U6).
 *
 * When the cable is plugged in, the PC asks for these tables and TinyUSB
 * answers through the tud_descriptor_*_cb() functions at the bottom.
 *
 *   Device  CAFE:4005  "Ant" / "Theremin Mouse" / <flash ID>     class EF/02/01
 *   `- Configuration 1  (100 bytes, bus powered, 250 mA)
 *      |- IAD: interfaces 0 and 1 form one function (CDC ACM)
 *      |  |- Interface 0  CDC control   EP 0x81 interrupt IN  (notifications)
 *      |  `- Interface 1  CDC data      EP 0x02 bulk OUT      (PC -> Pico)
 *      |                                EP 0x82 bulk IN       (Pico -> PC)
 *      `- Interface 2  HID boot mouse   EP 0x83 interrupt IN, bInterval 1 ms
 *
 * Endpoint address = number (low 4 bits) + direction (bit 7: 1 = IN, towards
 * the PC). Endpoint 0 is the control endpoint that every device has.
 */
#include <string.h>

#include "config.h"
#include "pico/unique_id.h"
#include "tusb.h"

/* ---- Numbers shared by the tables below ---- */
enum {
    ITF_CDC_CTRL = 0, /* CDC control interface (U3) */
    ITF_CDC_DATA = 1, /* CDC data interface */
    ITF_HID      = 2, /* HID mouse */
    ITF_COUNT    = 3
};

#define EP_CDC_NOTIF 0x81 /* interrupt IN: serial-state notifications */
#define EP_CDC_OUT   0x02 /* bulk OUT: bytes from the PC */
#define EP_CDC_IN    0x82 /* bulk IN: bytes to the PC */
#define EP_HID_IN    0x83 /* interrupt IN: mouse reports */

#define EP_CDC_NOTIF_SIZE 8  /* a serial-state notification is 10 bytes, sent in 8-byte packets */
#define EP_CDC_DATA_SIZE  64 /* largest bulk packet at full speed */
#define EP_HID_SIZE       8  /* holds the 4-byte report; = CFG_TUD_HID_EP_BUFSIZE */

enum { STR_LANGID = 0, STR_MANUFACTURER = 1, STR_PRODUCT = 2, STR_SERIAL = 3 };

#define LE16(x) (uint8_t)((x) & 0xFF), (uint8_t)(((x) >> 8) & 0xFF) /* 16-bit value, low byte first */

/* ======================================================================
 * Device descriptor (18 bytes): one per device.
 * ====================================================================== */
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t), /* 18 */
    .bDescriptorType    = TUSB_DESC_DEVICE,           /* 0x01 = device descriptor */
    .bcdUSB             = 0x0200,                     /* written to the USB 2.0 spec (the Pico runs at full speed, 12 Mbit/s) */
    /* Class EF / subclass 02 / protocol 01 = "Miscellaneous / Common class /
     * Interface Association". It tells the PC that the device is a composite
     * and that IADs group its interfaces into functions. Needed because the
     * CDC function spans two interfaces. */
    .bDeviceClass       = TUSB_CLASS_MISC,            /* 0xEF */
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,       /* 0x02 */
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,          /* 0x01 */
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,     /* endpoint 0 packet size: 64 */
    .idVendor           = CFG_USB_VID,                /* 0xCAFE (U1) */
    .idProduct          = CFG_USB_PID,                /* 0x4005 (U1) */
    .bcdDevice          = CFG_USB_BCD_DEVICE,         /* device release 1.00 */
    .iManufacturer      = STR_MANUFACTURER,           /* string 1: "Ant" */
    .iProduct           = STR_PRODUCT,                /* string 2: "Theremin Mouse" */
    .iSerialNumber      = STR_SERIAL,                 /* string 3: flash unique ID; gives a stable /dev/serial/by-id/ name */
    .bNumConfigurations = 1                           /* one configuration */
};

/* ======================================================================
 * HID report descriptor: tells the PC how to read the 4-byte mouse report.
 *
 *   byte 0: bit0 left, bit1 right, bit2 middle, bits 3-7 padding
 *   byte 1: x      int8, relative
 *   byte 2: y      int8, relative
 *   byte 3: wheel  int8, relative, positive = scroll up
 *
 * Each item is a prefix byte (tag + size) followed by its data. "Global"
 * items (usage page, logical min/max, report size/count) stay in force until
 * changed; an Input item then emits (size x count) bits into the report.
 * No Report ID item: the report starts directly with the button byte, which
 * is what the boot protocol requires (U5).
 * ====================================================================== */
static const uint8_t desc_hid_report[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop): the usages below are desktop controls */
    0x09, 0x02, /* Usage (Mouse): this top-level collection is a mouse */
    0xA1, 0x01, /* Collection (Application): start of the mouse */
    0x09, 0x01, /*   Usage (Pointer): the fields below describe one pointer */
    0xA1, 0x00, /*   Collection (Physical): data measured at one physical point */

    /* -- byte 0, bits 0-2: three buttons -- */
    0x05, 0x09, /*     Usage Page (Button) */
    0x19, 0x01, /*     Usage Minimum (1): button 1 = left ... */
    0x29, 0x03, /*     Usage Maximum (3): ... 2 = right, 3 = middle */
    0x15, 0x00, /*     Logical Minimum (0): released */
    0x25, 0x01, /*     Logical Maximum (1): pressed */
    0x95, 0x03, /*     Report Count (3): three fields ... */
    0x75, 0x01, /*     Report Size (1): ... of 1 bit each */
    0x81, 0x02, /*     Input (Data, Variable, Absolute): one bit per button, its current state */

    /* -- byte 0, bits 3-7: padding up to a whole byte -- */
    0x95, 0x01, /*     Report Count (1): one field ... */
    0x75, 0x05, /*     Report Size (5): ... of 5 bits */
    0x81, 0x01, /*     Input (Constant): filler, the PC ignores it */

    /* -- bytes 1-3: x, y, wheel -- */
    0x05, 0x01, /*     Usage Page (Generic Desktop) */
    0x09, 0x30, /*     Usage (X) */
    0x09, 0x31, /*     Usage (Y) */
    0x09, 0x38, /*     Usage (Wheel) */
    0x15, 0x81, /*     Logical Minimum (-127) */
    0x25, 0x7F, /*     Logical Maximum (+127): symmetric range, -128 is never sent */
    0x75, 0x08, /*     Report Size (8): 8 bits each ... */
    0x95, 0x03, /*     Report Count (3): ... three fields, in usage order X, Y, Wheel */
    0x81, 0x06, /*     Input (Data, Variable, Relative): movement since the last report */

    0xC0,       /*   End Collection (Physical) */
    0xC0        /* End Collection (Application) */
};

/* ======================================================================
 * Configuration descriptor: one block holding the configuration, the IAD,
 * all interfaces, the class-specific descriptors and the endpoints. The PC
 * reads it in one go; wTotalLength says how long the whole block is.
 * ====================================================================== */
#define CONFIG_TOTAL_LEN (9 + 8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7 + 9 + 9 + 7) /* = 100 */

static const uint8_t desc_configuration[] = {
    /* ---- Configuration (9 bytes) ---- */
    9,                         /* bLength */
    TUSB_DESC_CONFIGURATION,   /* bDescriptorType = 0x02 */
    LE16(CONFIG_TOTAL_LEN),    /* wTotalLength: this descriptor + everything after it */
    ITF_COUNT,                 /* bNumInterfaces = 3 */
    1,                         /* bConfigurationValue: the PC selects it with SET_CONFIGURATION(1) */
    0,                         /* iConfiguration: no string */
    0x80,                      /* bmAttributes: bit7 must be 1; bit6 = 0 bus powered; bit5 = 0 no remote wakeup (U6) */
    CFG_USB_MAX_POWER_MA / 2,  /* bMaxPower in 2 mA units: 125 = 250 mA */

    /* ---- Interface Association (8 bytes): "interfaces 0-1 are one CDC function" ---- */
    8,                                        /* bLength */
    TUSB_DESC_INTERFACE_ASSOCIATION,          /* bDescriptorType = 0x0B */
    ITF_CDC_CTRL,                             /* bFirstInterface = 0 */
    2,                                        /* bInterfaceCount: control + data */
    TUSB_CLASS_CDC,                           /* bFunctionClass = 0x02 communications */
    CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, /* bFunctionSubClass = 0x02 ACM (a serial port) */
    CDC_COMM_PROTOCOL_NONE,                   /* bFunctionProtocol = 0: no AT-command protocol */
    0,                                        /* iFunction: no string */

    /* ---- Interface 0: CDC control (9 bytes) ---- */
    9,                                        /* bLength */
    TUSB_DESC_INTERFACE,                      /* bDescriptorType = 0x04 */
    ITF_CDC_CTRL,                             /* bInterfaceNumber = 0 */
    0,                                        /* bAlternateSetting: only one setting */
    1,                                        /* bNumEndpoints: the notification endpoint */
    TUSB_CLASS_CDC,                           /* bInterfaceClass = 0x02 */
    CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, /* bInterfaceSubClass = 0x02: Linux binds cdc_acm -> /dev/ttyACMn */
    CDC_COMM_PROTOCOL_NONE,                   /* bInterfaceProtocol = 0 */
    0,                                        /* iInterface: no string */

    /* CDC Header functional descriptor (5 bytes): starts the class-specific list */
    5,                        /* bFunctionLength */
    TUSB_DESC_CS_INTERFACE,   /* bDescriptorType = 0x24 class-specific interface */
    CDC_FUNC_DESC_HEADER,     /* bDescriptorSubType = 0x00 */
    LE16(0x0120),             /* bcdCDC: CDC spec release 1.20 */

    /* CDC Call Management functional descriptor (5 bytes) */
    5,                             /* bFunctionLength */
    TUSB_DESC_CS_INTERFACE,        /* bDescriptorType */
    CDC_FUNC_DESC_CALL_MANAGEMENT, /* bDescriptorSubType = 0x01 */
    0x00,                          /* bmCapabilities: the device does no call management (it is not a modem) */
    ITF_CDC_DATA,                  /* bDataInterface = 1 */

    /* CDC Abstract Control Management functional descriptor (4 bytes) */
    4,                                         /* bFunctionLength */
    TUSB_DESC_CS_INTERFACE,                    /* bDescriptorType */
    CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, /* bDescriptorSubType = 0x02 */
    0x02,                                      /* bmCapabilities bit1: supports SET_LINE_CODING (baud rate: the 1200-baud
                                                  reboot uses it), SET_CONTROL_LINE_STATE (DTR: "a program opened the
                                                  port") and the SERIAL_STATE notification */

    /* CDC Union functional descriptor (5 bytes): ties the two interfaces together */
    5,                        /* bFunctionLength */
    TUSB_DESC_CS_INTERFACE,   /* bDescriptorType */
    CDC_FUNC_DESC_UNION,      /* bDescriptorSubType = 0x06 */
    ITF_CDC_CTRL,             /* bControlInterface = 0 */
    ITF_CDC_DATA,             /* bSubordinateInterface0 = 1 */

    /* Endpoint 0x81: CDC notification (7 bytes) */
    7,                        /* bLength */
    TUSB_DESC_ENDPOINT,       /* bDescriptorType = 0x05 */
    EP_CDC_NOTIF,             /* bEndpointAddress = 0x81: endpoint 1, IN */
    TUSB_XFER_INTERRUPT,      /* bmAttributes = 0x03 interrupt */
    LE16(EP_CDC_NOTIF_SIZE),  /* wMaxPacketSize = 8 */
    16,                       /* bInterval = 16 ms: notifications are rare, so poll slowly */

    /* ---- Interface 1: CDC data (9 bytes) ---- */
    9,                        /* bLength */
    TUSB_DESC_INTERFACE,      /* bDescriptorType = 0x04 */
    ITF_CDC_DATA,             /* bInterfaceNumber = 1 */
    0,                        /* bAlternateSetting */
    2,                        /* bNumEndpoints: bulk OUT + bulk IN */
    TUSB_CLASS_CDC_DATA,      /* bInterfaceClass = 0x0A CDC data */
    0,                        /* bInterfaceSubClass: unused for CDC data */
    0,                        /* bInterfaceProtocol: raw bytes */
    0,                        /* iInterface: no string */

    /* Endpoint 0x02: CDC data OUT (7 bytes) */
    7,                        /* bLength */
    TUSB_DESC_ENDPOINT,       /* bDescriptorType */
    EP_CDC_OUT,               /* bEndpointAddress = 0x02: endpoint 2, OUT (PC -> Pico) */
    TUSB_XFER_BULK,           /* bmAttributes = 0x02 bulk: reliable, uses spare bus time */
    LE16(EP_CDC_DATA_SIZE),   /* wMaxPacketSize = 64 */
    0,                        /* bInterval: ignored for bulk endpoints */

    /* Endpoint 0x82: CDC data IN (7 bytes) */
    7,                        /* bLength */
    TUSB_DESC_ENDPOINT,       /* bDescriptorType */
    EP_CDC_IN,                /* bEndpointAddress = 0x82: endpoint 2, IN (Pico -> PC) */
    TUSB_XFER_BULK,           /* bmAttributes = 0x02 bulk */
    LE16(EP_CDC_DATA_SIZE),   /* wMaxPacketSize = 64 */
    0,                        /* bInterval: ignored for bulk endpoints */

    /* ---- Interface 2: HID mouse (9 bytes) ---- */
    9,                        /* bLength */
    TUSB_DESC_INTERFACE,      /* bDescriptorType = 0x04 */
    ITF_HID,                  /* bInterfaceNumber = 2 */
    0,                        /* bAlternateSetting */
    1,                        /* bNumEndpoints: interrupt IN only */
    TUSB_CLASS_HID,           /* bInterfaceClass = 0x03 HID */
    HID_SUBCLASS_BOOT,        /* bInterfaceSubClass = 1: supports the boot protocol (works in a BIOS) (U5) */
    HID_ITF_PROTOCOL_MOUSE,   /* bInterfaceProtocol = 2: mouse */
    0,                        /* iInterface: no string */

    /* HID descriptor (9 bytes): points to the report descriptor above */
    9,                        /* bLength */
    HID_DESC_TYPE_HID,        /* bDescriptorType = 0x21 HID */
    LE16(0x0111),             /* bcdHID: HID spec release 1.11 */
    0,                        /* bCountryCode: not localised */
    1,                        /* bNumDescriptors: one class descriptor follows ... */
    HID_DESC_TYPE_REPORT,     /* bDescriptorType = 0x22: ... a report descriptor ... */
    LE16(sizeof(desc_hid_report)), /* wDescriptorLength: ... of this many bytes (52) */

    /* Endpoint 0x83: HID reports (7 bytes) */
    7,                        /* bLength */
    TUSB_DESC_ENDPOINT,       /* bDescriptorType */
    EP_HID_IN,                /* bEndpointAddress = 0x83: endpoint 3, IN */
    TUSB_XFER_INTERRUPT,      /* bmAttributes = 0x03 interrupt: the PC polls on a fixed schedule */
    LE16(EP_HID_SIZE),        /* wMaxPacketSize = 8 (the report uses 4) */
    1                         /* bInterval = 1 ms: polled every full-speed frame = up to 1000 reports/s */
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN,
               "wTotalLength must equal the real size of the configuration descriptor");
_Static_assert(EP_HID_SIZE == CFG_TUD_HID_EP_BUFSIZE, "HID endpoint size must match tusb_config.h");
_Static_assert(EP_CDC_DATA_SIZE == CFG_TUD_CDC_EP_BUFSIZE, "CDC endpoint size must match tusb_config.h");

/* ======================================================================
 * Callbacks: TinyUSB calls these (from tud_task) when the PC asks.
 * ====================================================================== */

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; /* only one configuration */
    return desc_configuration;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance; /* only one HID interface */
    return desc_hid_report;
}

/*
 * String descriptors. On the wire a string is: bLength, bDescriptorType
 * (0x03), then UTF-16LE characters. A uint16_t array gives exactly that
 * layout: element 0 is the 2-byte header, the rest are the characters.
 */
#define STR_MAX_CHARS 31
static uint16_t s_str_desc[1 + STR_MAX_CHARS];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid; /* only one language is offered */

    /* Serial = the 64-bit unique ID of the flash chip as 16 hex digits (U2). */
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *text;
    size_t n;

    switch (index) {
        case STR_LANGID:
            /* String 0 is special: a list of language codes. 0x0409 = English (US). */
            s_str_desc[1] = 0x0409;
            s_str_desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2));
            return s_str_desc;
        case STR_MANUFACTURER:
            text = CFG_USB_MANUFACTURER;
            break;
        case STR_PRODUCT:
            text = CFG_USB_PRODUCT;
            break;
        case STR_SERIAL:
            pico_get_unique_board_id_string(serial, sizeof(serial));
            text = serial;
            break;
        default:
            return NULL; /* unknown index: TinyUSB answers with a STALL */
    }

    n = strlen(text);
    if (n > STR_MAX_CHARS) n = STR_MAX_CHARS;
    for (size_t i = 0; i < n; i++) {
        s_str_desc[1 + i] = (uint16_t)(unsigned char)text[i]; /* ASCII -> UTF-16LE */
    }
    /* Header: low byte = total length in bytes, high byte = type 0x03. */
    s_str_desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2 * n));
    return s_str_desc;
}
