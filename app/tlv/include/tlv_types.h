#pragma once 
#include <errno.h>
#include <string.h>

#include "uart_frame.h"

#define TLV_MAX_VALUE_SIZE (UART_MAX_PACKET_SIZE - 2)
#define TLV_VERSION_MAJOR 1
#define TLV_VERSION_MINOR 0

typedef enum {
    TLV_ID_VERSION,
    TLV_ID_ERR,
    TLV_ID_LED,
    TLV_ID_BUZZER,
    TLV_ID_INFECTION_RISK,
    
    TLV_ID_MAX,
    
    TLV_ID_MEASUREMENT,
} tlv_id_t;

typedef struct 
{
    uint8_t id;
    uint8_t len;
    uint8_t value[TLV_MAX_VALUE_SIZE];
} tlv_packet_t;

typedef void (*event_fn_t)(tlv_packet_t *p);
#define TLV_PACK_SIZE  sizeof(tlv_packet_t)

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

    out->id = (uint8_t)frame->data[0];
    out->len = vlen;
    memcpy(out->value, &frame->data[2], vlen);
    return 0;
}