#pragma once
#include <zephyr/sys/util.h>      // BUILD_ASSERT()
#include <zephyr/sys/byteorder.h> // sys_put_be16, sys_get_be16

/* EDIIT YOUR APPICATION REQUIREMENTS*/

#ifndef CONFIG_CUSTOM_UART_RX_STACK_SIZE
#define CONFIG_CUSTOM_UART_RX_STACK_SIZE        64
#endif

#ifndef CONFIG_CUSTOM_UART_RX_CHUNK_SIZE
#define CONFIG_CUSTOM_UART_RX_CHUNK_SIZE        64 /* DMA/ping-pong section */
#endif

#ifndef CONFIG_CUSTOM_UART_SYNC_BYTE
#define CONFIG_CUSTOM_UART_SYNC_BYTE            0xAA
#endif

#ifndef UART_MSGQ_DEPTH
#define UART_MSGQ_DEPTH                         4
#endif

#ifndef UART_CRC_INT
#define UART_CRC_INT                            0xFFFF
#endif

#define UART_MAX_PACKET_SIZE                    CONFIG_CUSTOM_UART_RX_STACK_SIZE
#define UART_RX_CHUNK_LEN                       CONFIG_CUSTOM_UART_RX_CHUNK_SIZE
#define UART_RB_SZ                              (UART_RX_CHUNK_LEN * 4)
#define UART_SYNC_BYTE                          CONFIG_CUSTOM_UART_SYNC_BYTE

enum
{
    SYNC_BYTE = UART_SYNC_BYTE
};

/* segment config*/

typedef struct __packed
{
    uint8_t typ;          /* 1 */
    uint8_t xid;          /* 1 */
    uint8_t total_be[2];  /* 2: toplam uzunluk (wire: BE16) */
    uint8_t offset_be[2]; /* 2: bu parçanın ofseti (wire: BE16) */
    uint8_t clen;         /* 1: bu parçanın veri uzunluğu */
} seg_wire_hdr_t;

#define SEG_TYP_DATA 0x01

#define SEG_HDR_SIZE (sizeof(seg_wire_hdr_t)) /* şu an 7 */
BUILD_ASSERT(SEG_HDR_SIZE >= 5, "segment header too small?");
BUILD_ASSERT(SEG_HDR_SIZE <= 64, "segment header unexpectedly large?");

/* Frame LEN = header + chunk; MAX_PACKET_SIZE frame'in LEN üst sınırıdır */
BUILD_ASSERT(UART_MAX_PACKET_SIZE <= 255, "LEN is 1 byte; UART_MAX_PACKET_SIZE must be <= 255");
BUILD_ASSERT(SEG_HDR_SIZE < UART_MAX_PACKET_SIZE, "header must fit inside frame LEN");

/* Uygulamanın taşıyabileceği net parça boyutu */
#define PAYLOAD_MAX (UART_MAX_PACKET_SIZE - SEG_HDR_SIZE)
BUILD_ASSERT(PAYLOAD_MAX > 0, "PAYLOAD_MAX must be > 0");

/* (Bilgi amaçlı) Toplam frame üst sınırı: SYNC + LEN + DATA + CRC(2) */
#define FRAME_OVERHEAD_BYTES (1u /*SYNC*/ + 1u /*LEN*/ + 2u /*CRC*/)
#define FRAME_MAX_TOTAL (FRAME_OVERHEAD_BYTES + UART_MAX_PACKET_SIZE)

static inline void seg_hdr_write(uint8_t *dst, uint8_t typ, uint8_t xid,
                                 uint16_t total, uint16_t offset, uint8_t clen)
{
    /* dst en az SEG_HDR_SIZE kadar olmalı */
    dst[0] = typ;
    dst[1] = xid;
    sys_put_be16(total, &dst[2]);  /* total_be */
    sys_put_be16(offset, &dst[4]); /* offset_be */
    dst[6] = clen;
}

static inline void seg_hdr_read(const uint8_t *src, uint8_t *typ, uint8_t *xid,
                                uint16_t *total, uint16_t *offset, uint8_t *clen)
{
    *typ = src[0];
    *xid = src[1];
    *total = sys_get_be16(&src[2]);
    *offset = sys_get_be16(&src[4]);
    *clen = src[6];
}
