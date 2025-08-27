#pragma once 


typedef void (*uart_io_rx_cb_t)(void *ctx);
extern uart_io_rx_cb_t rx_cb;

int uart_io_init(void);

int uart_io_send_larg(const uint8_t *buf, uint32_t len, uint8_t xfer_id);

int uart_io_send_buffer(const uint8_t *buf, size_t len, k_timeout_t per_frame_timeout);

int uart_io_send_frame(const uint8_t *payload, uint8_t len, k_timeout_t timeout);

void uart_io_register_rx_cb(uart_io_rx_cb_t uart_io_rx_cb);