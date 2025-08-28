#pragma once

#include "uart_frame.h"
#include <errno.h>

#define TLV_MAX_VALUE_SIZE (UART_MAX_PACKET_SIZE - 2)

typedef enum
{
    TLV_VERSION = 0,
    TLV_ERROR,

} tlv_packet_id_e;

typedef uint8_t tlv_packet_id_t;   
typedef struct{ uint8_t major, minor; } tlv_version_t;
typedef struct { uint8_t code; } tlv_error_t;

typedef struct
{
    tlv_packet_id_t id;
    uint8_t len;
    uint8_t value[TLV_MAX_VALUE_SIZE];
} tlv_packet_t;

