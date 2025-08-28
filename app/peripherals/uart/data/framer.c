#include <string.h>

#include "framer.h"
#include "crc16_ccitt.h"
#include "uart_frame.h"

K_MSGQ_DEFINE(uart_rx_msg_q, sizeof(uart_frame_t), 4, 4);


typedef enum { PARSER_SYNC, PARSER_LEN, PARSER_DATA, PARSER_CRC_H, PARSER_CRC_L } parse_state_t;

typedef struct
{
    parse_state_t st;
    uint8_t len, pos, crc_hi_tmp;
    uint16_t crc_calc;
    uint16_t budget;      
    bool drop_until_sync; 
    uart_frame_t frame;
} parser_t;

static parser_t Q;
static uint32_t stat_ok, stat_len_err, stat_crc_err, stat_budget = 0;

static inline void q_reset(parser_t *p)
{
    p->st = PARSER_SYNC;
    p->len = p->pos = p->crc_hi_tmp = 0;
    p->crc_calc = 0xFFFF;
    p->budget = 0;
    p->drop_until_sync = false;
}
static inline void q_start(parser_t *p)
{
    p->st = PARSER_LEN;
    p->len = p->pos = p->crc_hi_tmp = 0;
    p->crc_calc = 0xFFFF;
    p->budget = 1; /* SYNC okundu */
}

/* Hata olduğunda hızlı toparlanma: bir SYNC görene kadar at */
static inline void set_resync(parser_t *p)
{
    p->drop_until_sync = true;
    p->st = PARSER_SYNC;
}

static void q_push_sync(parser_t *p, uint8_t b)
{
    if (b == SYNC_BYTE) q_start(p);
}

static void q_push_len(parser_t *p, uint8_t b)
{
    p->budget++;
    if (b == 0 || b > UART_MAX_PACKET_SIZE) { stat_len_err++; set_resync(p); return; }
    p->len = b; p->frame.len = b;
    p->crc_calc = crc16_ccitt_step(UART_CRC_INT, b); /* LEN dahil */
    p->pos = 0; p->st = PARSER_DATA;
}

static void q_push_data(parser_t *p, uint8_t b)
{
    p->budget++;
    p->frame.data[p->pos++] = b;
    p->crc_calc = crc16_ccitt_step(p->crc_calc, b);
    if (p->pos == p->len) p->st = PARSER_CRC_H;
    if (p->budget > (uint16_t)(1 + 1 + UART_MAX_PACKET_SIZE + 2)) { stat_budget++; set_resync(p); }
}

static void q_push_crc(parser_t *p, uint8_t b)
{
    p->budget++;
    p->crc_hi_tmp = b; p->st = PARSER_CRC_L;
}

static void q_push_l(parser_t *p, uint8_t b)
{
    p->budget++;
    uint16_t recv_crc = ((uint16_t)p->crc_hi_tmp << 8) | b;
    if (recv_crc == p->crc_calc) { (void)k_msgq_put(&uart_rx_msg_q, &p->frame, K_NO_WAIT); stat_ok++; }
    else { stat_crc_err++; }
    q_reset(p);
}

typedef void (*q_push_byte_fn_t)(parser_t *p, uint8_t b);
static const q_push_byte_fn_t P[] = {
    [PARSER_SYNC] = q_push_sync,
    [PARSER_LEN] = q_push_len,
    [PARSER_DATA] = q_push_data,
    [PARSER_CRC_H] = q_push_crc,
    [PARSER_CRC_L] = q_push_l,
};

/* Tek bayt ilerlet (korumalarla) */
static void q_push_byte(parser_t *p, uint8_t b)
{
    /* Hızlı yeniden senkron modu: SYNC’e kadar at */
    if (p->drop_until_sync)
    {
        if (b == SYNC_BYTE)
        {
            p->drop_until_sync = false;
            q_start(p);
        }
        return;
    }

#ifdef ALLOW_MIDFRAME_SYNC_RESTART
    if (p->st != PARSER_SYNC && b == SYNC_BYTE)
    {
        /* Orta akışta SYNC: mevcut çerçeveyi iptal edip yeniye başla */
        q_start(p);
        return;
    }
#endif

    if (p->st < ARRAY_SIZE(P) && P[(int)p->st])
    {
        P[p->st](p, b);
    }
    else
    {
        __ASSERT(p->st < ARRAY_SIZE(P), "Invalid parser state");
    }
}

void framer_init(void) { q_reset(&Q); }
void framer_reset(void) { q_reset(&Q); }

void framer_push_bytes(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        q_push_byte(&Q, buf[i]);
}


#define APP_LOG_MODULE UART_FRAMER
#include "logger.h"
LOG_MODULE_REGISTER(APP_LOG_MODULE, APP_LOG_LEVEL);
void framer_dump_stats(void)
{
    LOG_INFO("[FRAMER] ok=%u len_err=%u crc_err=%u budget=%u",
           stat_ok, stat_len_err, stat_crc_err, stat_budget);
}

