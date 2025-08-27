
#include <zephyr/kernel.h>

#define APP_LOG_MODULE MAIN
#include "logger.h"
LOG_MODULE_REGISTER(APP_LOG_MODULE, APP_LOG_LEVEL);

#include "uart_io.h"

typedef struct
{
    uint8_t len;
    uint8_t data[64];
} uart_pkt_t;

typedef struct
{
    uint8_t samefield; // change for your protocol
    uart_pkt_t pkt;
} uart_payload_t;

static void uart_rx_cb(void *ctx)
{
    if (!ctx)
        return;

    uart_payload_t p = {0};
    p.pkt = *(uart_pkt_t *)ctx;

    LOG_INFO("OK. PACKET LEN:%d", p.pkt.len);
    k_msleep(1000);
    LOG_INFO("ECHO.");
    const char echo[] = "This is an echo message!";
    uart_io_send_frame((const uint8_t *)echo, strlen(echo), K_MSEC(200));
}

int main(void)
{
    int ret = 0;

    ret = uart_io_init();

    if (ret < 0)
    {
        LOG_INFO("UART initilaizing faild err=%d", ret);
    }

    uart_io_register_rx_cb(uart_rx_cb);
}