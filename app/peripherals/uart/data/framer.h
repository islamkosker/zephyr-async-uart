#pragma once
#include <zephyr/kernel.h>
#include <stdbool.h>

#include "uart_cfg.h"

/* (Opsiyonel) DATA içinde SYNC görülürse yeni frame başlat (ESC/COBS yoksa kapalı tutmak daha güvenli) */
// #define ALLOW_MIDFRAME_SYNC_RESTART 1

typedef struct {
    uint8_t len;
    uint8_t data[UART_MAX_PACKET_SIZE];
} frame_rx_packet_t;

extern struct k_msgq uart_rx_msg_q;
extern struct k_work_poll uart_rx_wp;
extern struct k_poll_event uart_rx_pe;

void framer_init(void);
void framer_reset(void);
void framer_dump_stats(void);
void framer_push_bytes(const uint8_t *buf, size_t len);

