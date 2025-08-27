#pragma once
#include <stdint.h>
#include "uart_cfg.h"

static inline uint16_t crc16_ccitt_step(uint16_t crc, uint8_t b)
{

    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    return crc;
}

static inline uint16_t crc16_ccitt_update(uint16_t crc, const uint8_t *d, size_t n)
{

    while (n--)
    {
        crc ^= (uint16_t)(*d++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline size_t build_frame(uint8_t *out, const uint8_t *payload, uint8_t len)
{
    out[0] = (uint8_t)SYNC_BYTE;
    out[1] = len;
    if (len)
        memcpy(&out[2], payload, len);
    uint16_t crc = crc16_ccitt_update(UART_CRC_INT, &out[1], (size_t)1 + len); /* LEN+DATA */
    out[2 + len] = (uint8_t)(crc >> 8);
    out[2 + len + 1] = (uint8_t)(crc & 0xFF);
    return (size_t)(2 + len + 2); /* total frame len */
}