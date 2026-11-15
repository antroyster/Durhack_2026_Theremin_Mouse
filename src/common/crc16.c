/* crc16.c - CRC-16/CCITT-FALSE. Pure C: builds on the Pico and the PC. */
#include "protocol.h"

uint16_t proto_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;                      /* initial value */
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8); /* feed the byte in at the top (no reflection) */
        for (int bit = 0; bit < 8; bit++) {
            /* Shift left; if a 1 fell off the top, subtract (XOR) the polynomial. */
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;                                  /* no final XOR */
}
