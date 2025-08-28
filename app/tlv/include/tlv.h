#pragma once

#include <errno.h>

#include <string.h>
#include "uart_frame.h"
#include "tlv_types.h"

static inline void put_u16_le(uint8_t *dst, uint16_t v)
{ dst[0] = (uint8_t)(v & 0xFF); dst[1] = (uint8_t)(v >> 8); }

static inline void put_i16_le(uint8_t *dst, int16_t v)
{ uint16_t u = (uint16_t)v; dst[0] = (uint8_t)(u & 0xFF); dst[1] = (uint8_t)(u >> 8); }

static inline uint16_t get_u16_le(const uint8_t *src)
{ return (uint16_t)src[0] | ((uint16_t)src[1] << 8); }

static inline int16_t get_i16_le(const uint8_t *src)
{ return (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8)); }


/// @brief Craete a uart frame with tlv packet
/// @param frame out
/// @param tlv_packet in
/// @return
static inline int tlv_encode(uart_frame_t *frame, const tlv_packet_t *p)
{
    if (!frame || !p)
        return -EINVAL;

    if (p->len > TLV_MAX_VALUE_SIZE)
        return -EMSGSIZE;

    frame->data[0] = (uint8_t)p->id; 
    frame->data[1] = p->len;
    memcpy(&frame->data[2], p->value, p->len);

    frame->len = (uint8_t)(2u + p->len);
    return 0;
}

/// @brief Create a tlv packet with uart frame
/// @param tlv_packet
/// @param frame
/// @return
static inline int tlv_decode(tlv_packet_t *out, const uart_frame_t *frame)
{
    if (!out)
        return -EFAULT; // Output pointer null

    if (!frame)
        return -ENODATA; // Frame pointer null

    if (frame->len < 2u)
        return -EBADMSG; // Frame too short for TLV header

    uint8_t vlen = frame->data[1];

    if (vlen > TLV_MAX_VALUE_SIZE)
        return -EMSGSIZE; // Value length too large

    if ((uint16_t)frame->len < (uint16_t)(2u + vlen))
        return -EOVERFLOW; // Frame length not enough for value

    out->id = (tlv_packet_id_t)frame->data[0];
    out->len = vlen;
    memcpy(out->value, &frame->data[2], vlen);
    return 0;
}