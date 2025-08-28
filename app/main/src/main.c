
#include <zephyr/kernel.h>

#define APP_LOG_MODULE MAIN
#include "logger.h"
LOG_MODULE_REGISTER(APP_LOG_MODULE, APP_LOG_LEVEL);

#include "uart_io.h"
#include "tlv.h"

static void uart_rx_cb(uart_frame_t *frame)
{
    if (!frame || frame->len == 0)
        return;

    tlv_packet_t tlv_pack = {0};
    int ret = tlv_decode(&tlv_pack, frame);
    
    LOG_INFO("ret:%d ", ret);

    LOG_HEXDUMP_INF(tlv_pack.value, tlv_pack.len, "TLV VALUE");
    LOG_INFO("ID:%d ", tlv_pack.id);
    LOG_INFO("LEN:%d ", tlv_pack.len);

    k_msleep(1000);
    // LOG_INFO("ECHO.");
    // const char echo[] = "This is an echo message!";
    // uart_io_send_frame((const uint8_t *)echo, strlen(echo), K_MSEC(200));
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