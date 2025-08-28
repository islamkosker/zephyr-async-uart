#pragma once 
#include "uart_cfg.h"
#include <limits.h>  

typedef struct {
    uint8_t len;
    uint8_t data[UART_MAX_PACKET_SIZE];
} uart_frame_t;

#if UART_MAX_PACKET_SIZE > UINT8_MAX
# error “uart_frame_t.len is uint8_t; either make len uint16_t or ensure PACKET_SIZE <= 255.”
#endif