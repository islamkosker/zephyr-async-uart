#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#define APP_LOG_MODULE UART_IO
#include "logger.h"
LOG_MODULE_REGISTER(APP_LOG_MODULE, APP_LOG_LEVEL);

#include "framer.h"
#include "uart_io.h"
#include "crc16_ccitt.h"

/* ---- GLOBALS ---- */
static const struct device *uart_dev;

#ifndef UART_DEVICE_NODE
#define UART_DEVICE_NODE DT_ALIAS(uart_com) /* dts: aliases { uart-com = &uart0; }; */
#endif

struct k_work rx_drain_work;

struct k_work_poll uart_rx_wp;
struct k_poll_event uart_rx_pe;

uart_io_rx_cb_t rx_cb = NULL;


RING_BUF_DECLARE(uart_rb, UART_RB_SZ);
static uint8_t uart_rb_mem[UART_RB_SZ];

/* Çift RX buffer (ping-pong) */
static uint8_t async_rx_buffer[2][UART_RX_CHUNK_LEN];
static volatile uint8_t async_idx;

/* RX_RDY event’inde gelen buffer-ofset takibi */
static const uint8_t *rx_buf;
static size_t rx_off;      /* evt->data.rx.offset */
static size_t rx_prev_len; /* aynı buffer için önceki len */

/* ISR hafif tutulsun: decode/parse burada yok; sadece work tetikleyeceğiz */

/* İstatistik (opsiyonel; ISR’de log yok, sadece sayaç) */
static volatile uint32_t stat_drop_bytes;

typedef struct
{
    struct k_sem lock;   /* gönderim sırası: 1 → yalnızca bir thread */
    struct k_sem done;   /* TX_DONE/TX_ABORTED sinyali */
    volatile bool armed; /* aktif bir TX var mı */
} tx_ctx_t;

tx_ctx_t tx_ctx = {
    .armed = false};

static void rx_drain_worker(struct k_work *work)
{

    ARG_UNUSED(work);
    uint8_t tmp[256];
    size_t g;
    do
    {
        g = ring_buf_get(&uart_rb, tmp, sizeof(tmp));
        if (g)
            framer_push_bytes(tmp, g);
    } while (g > 0);
}

static size_t rb_make_room(struct ring_buf *rb, size_t need)
{
    size_t freed = 0;
    uint8_t dump[64];
    while (ring_buf_space_get(rb) < need)
    {
        size_t g = ring_buf_get(rb, dump, sizeof(dump));
        if (g == 0)
            break;
        freed += g;
    }
    stat_drop_bytes += freed;
    return freed;
}

/* ---- Event handler’lar (ISR bağlamı) ---- */
static void on_rx_rdy(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user);

    /* Aynı DMA buffer’ında mıyız? */
    if (evt->data.rx.buf != rx_buf || evt->data.rx.offset != rx_off)
    {
        rx_buf = evt->data.rx.buf;
        rx_off = evt->data.rx.offset;
        rx_prev_len = 0;
    }

    size_t total = evt->data.rx.len; /* bu buffer’daki toplam doldurulmuş uzunluk */
    if (total < rx_prev_len)
    {
        /* Güvenlik: bazı sürücülerde wrap olabilir */
        rx_prev_len = 0;
    }

    size_t delta = total - rx_prev_len; /* yeni gelen kısım */
    if (!delta)
        return;

    const uint8_t *p = evt->data.rx.buf + evt->data.rx.offset + rx_prev_len;

    /* Yer aç; en eskileri at */
    (void)rb_make_room(&uart_rb, delta);

    size_t w = ring_buf_put(&uart_rb, p, delta);
    /* Yer kalmadıysa kalan baytlar düşer; UART’ı kapatmayız */
    if (w < delta)
    {
        stat_drop_bytes += (delta - w);
    }

    rx_prev_len = total;

    /* Thread tarafına tüketim sinyali: ağır iş orada yapılacak */
    k_work_submit(&rx_drain_work);
}

static void on_rx_buf_request(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(evt);
    ARG_UNUSED(user);
    int rc = uart_rx_buf_rsp(dev, async_rx_buffer[async_idx], UART_RX_CHUNK_LEN);
    __ASSERT_NO_MSG(rc == 0);
    (void)rc;
    async_idx ^= 1;
}

static void on_rx_buf_released(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user);
    if (evt->data.rx_buf.buf == rx_buf)
    {
        rx_buf = NULL;
        rx_off = 0;
        rx_prev_len = 0;
    }
    /* Buffer değiştiyse, tüketimi hızlandır */
    k_work_submit(&rx_drain_work);
}

static void on_rx_reenable(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(evt);
    ARG_UNUSED(user);

    /* Hata/stop durumunda temiz başla */
    unsigned int key = irq_lock();
    ring_buf_reset(&uart_rb);
    rx_prev_len = 0;
    rx_buf = NULL;
    rx_off = 0;
    irq_unlock(key);
    framer_reset();

    async_idx = 1;
    (void)uart_rx_enable(dev, async_rx_buffer[0], UART_RX_CHUNK_LEN, 20 /* ms timeout */);
}

static void on_tx_done(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(evt);
    ARG_UNUSED(user);
    if (tx_ctx.armed)
    {
        /* ISR’dan çağrılabilir: k_sem_give güvenli */
        k_sem_give(&tx_ctx.done);
    }
}

static void on_tx_aborted(const struct device *dev, struct uart_event *evt, void *user)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(evt);
    ARG_UNUSED(user);
    if (tx_ctx.armed)
    {
        k_sem_give(&tx_ctx.done);
    }
}

/* ---- Tek callback: uart_handler_cb ---- */
typedef void (*uart_handler_fn_t)(const struct device *, struct uart_event *, void *);
static const uart_handler_fn_t EVT[] = {
    [UART_TX_DONE] = on_tx_done,
    [UART_RX_BUF_REQUEST] = on_rx_buf_request,
    [UART_RX_BUF_RELEASED] = on_rx_buf_released,
    [UART_RX_RDY] = on_rx_rdy,
    [UART_RX_DISABLED] = on_rx_reenable,
    [UART_RX_STOPPED] = on_rx_reenable,
    [UART_TX_ABORTED] = on_tx_aborted,
};

static void uart_handler_cb(const struct device *dev, struct uart_event *evt, void *user)
{
    if (evt->type < ARRAY_SIZE(EVT) && EVT[evt->type])
    {
        EVT[evt->type](dev, evt, user);
    }
    else
    {
        __ASSERT(evt->type < ARRAY_SIZE(EVT), "Invalid UART event type");
    }
}

/* ============================================ * UART TX * ============================================*/

int uart_send_frame(const struct device *uart_dev, const uint8_t *payload, uint8_t len, k_timeout_t timeout)
{
    if (!uart_dev)
        return -ENODEV;
    if (len == 0 || len > UART_MAX_PACKET_SIZE)
        return -EINVAL;

    /* Sıralama: aynı anda tek gönderim */
    if (k_sem_take(&tx_ctx.lock, K_NO_WAIT) != 0)
    {
        return -EBUSY;
    }

    /* Frame’i stack’te kur → TX_DONE’a kadar fonksiyondan çıkmayacağız */
    uint8_t frame[2 + UART_MAX_PACKET_SIZE + 2];
    size_t flen = build_frame(frame, payload, len);

    tx_ctx.armed = true;

    /* uart_tx başlat; buffer TX tamamlanana kadar geçerli kalmalı (bu fonksiyonda bekliyoruz) */
    int rc = uart_tx(uart_dev, frame, flen, SYS_FOREVER_MS);
    if (rc != 0)
    {
        tx_ctx.armed = false;
        k_sem_give(&tx_ctx.lock);
        return rc; /* -EBUSY etc. */
    }

    /* TX_DONE / TX_ABORTED  */
    if (k_sem_take(&tx_ctx.done, timeout) != 0)
    {
        /* zaman aşımı: abort etmeyi dene ve kısa bekle */
        (void)uart_tx_abort(uart_dev);
        (void)k_sem_take(&tx_ctx.done, K_MSEC(100));
        tx_ctx.armed = false;
        k_sem_give(&tx_ctx.lock);
        return -ETIMEDOUT;
    }

    tx_ctx.armed = false;
    k_sem_give(&tx_ctx.lock);
    return 0;
}

int uart_send_buffer(const struct device *uart_dev, const uint8_t *buf, size_t len, k_timeout_t per_frame_timeout)
{
    while (len > 0)
    {
        uint8_t chunk = (len > UART_MAX_PACKET_SIZE) ? UART_MAX_PACKET_SIZE : (uint8_t)len;
        int rc = uart_send_frame(uart_dev, buf, chunk, per_frame_timeout);
        if (rc != 0)
            return rc;
        buf += chunk;
        len -= chunk;
    }
    return 0;
}

/* Büyük buffer’ı küçük frame’lere böler (MAX=64). RAM: sadece küçük bir temp (64B) */
int uart_send_large(const struct device *uart_dev, const uint8_t *buf, uint32_t len, uint8_t xfer_id)
{
    uint16_t off = 0;
    uint8_t frame_payload[UART_MAX_PACKET_SIZE]; /* stack: 64 B */

    while (off < len)
    {
        uint8_t chunk = (uint8_t)MIN((uint16_t)PAYLOAD_MAX, (uint16_t)(len - off));

        /* header'ı yaz */
        seg_hdr_write(frame_payload, SEG_TYP_DATA, xfer_id, len, off, chunk);

        /* veriyi header arkasına koy */
        memcpy(&frame_payload[SEG_HDR_SIZE], &buf[off], chunk);

        /* LEN = header + chunk */
        int rc = uart_send_frame(uart_dev, frame_payload, SEG_HDR_SIZE + chunk, K_SECONDS(1));
        if (rc)
            return rc;

        off += chunk;
    }
    return 0;
}


static void uart_rx_handler(struct k_work *work)
{
    frame_rx_packet_t f;

    while (k_msgq_get(&uart_rx_msg_q, &f, K_NO_WAIT) == 0)
    {


        if (rx_cb && rx_cb != NULL)
        {
            rx_cb((void *)&f);
        }
    }

    k_work_poll_submit(&uart_rx_wp, &uart_rx_pe, 1, K_FOREVER);
}

static void uart_kernel_object_init(void)
{

    k_work_init(&rx_drain_work, rx_drain_worker);

    k_work_poll_init(&uart_rx_wp, uart_rx_handler);
    k_poll_event_init(&uart_rx_pe, K_POLL_TYPE_MSGQ_DATA_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &uart_rx_msg_q);
    k_work_poll_submit(&uart_rx_wp, &uart_rx_pe, 1, K_FOREVER);

    k_sem_init(&tx_ctx.lock, 1, 1);
    k_sem_init(&tx_ctx.done, 0, 1);
    tx_ctx.armed = false;
}

/* ============================================ * GLOBALS * ============================================*/

int uart_io_init(void)
{

    uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
    if (!device_is_ready(uart_dev))
    {
        return -ENODEV;
    }

    uart_kernel_object_init();

    ring_buf_init(&uart_rb, UART_RB_SZ, uart_rb_mem);

    framer_init();

    uart_callback_set(uart_dev, uart_handler_cb, (void *)uart_dev);
    async_idx = 1;
    int ret = uart_rx_enable(uart_dev, async_rx_buffer[0], UART_RX_CHUNK_LEN, 20 /* ms */);
    if (ret)
    {
        return ret;
    }



    LOG_INFO("UART STARTED");

    return 0;
}

int uart_io_send_larg(const uint8_t *buf, uint32_t len, uint8_t xfer_id)
{
    return uart_send_large(uart_dev, buf, len, xfer_id);
}

int uart_io_send_buffer(const uint8_t *buf, size_t len, k_timeout_t per_frame_timeout)
{
    return uart_send_buffer(uart_dev, buf, len, per_frame_timeout);
}

int uart_io_send_frame(const uint8_t *payload, uint8_t len, k_timeout_t timeout)
{
    return uart_send_frame(uart_dev, payload, len, timeout);
}

void uart_io_register_rx_cb(uart_io_rx_cb_t uart_io_rx_cb)
{
    rx_cb = uart_io_rx_cb;
}