#ifndef ELIB_PROTOCOL_CORE_H
#define ELIB_PROTOCOL_CORE_H

#include "../include/elib_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ELIB_PROTOCOL_FRAME_HEAD       0x68
#define ELIB_PROTOCOL_FRAME_TAIL       0x16
#define ELIB_PROTOCOL_FRAME_OVERHEAD   12
#define ELIB_PROTOCOL_FRAME_LEN_MIN    12
#define ELIB_PROTOCOL_ADDR_LEN         4

#define ELIB_PROTOCOL_DESC_TYPE_MASK   0x06
#define ELIB_PROTOCOL_DESC_TYPE_REQ    0x00
#define ELIB_PROTOCOL_DESC_TYPE_RESP   0x02
#define ELIB_PROTOCOL_DESC_TYPE_EVENT  0x04

#define ELIB_PROTOCOL_DESC_FRAG_MASK   0x08
#define ELIB_PROTOCOL_DESC_FRAG_YES    0x08
#define ELIB_PROTOCOL_DESC_FRAG_NO     0x00

#define ELIB_PROTOCOL_DESC_DIR_MASK    0x01
#define ELIB_PROTOCOL_DESC_DIR_UP      0x01
#define ELIB_PROTOCOL_DESC_DIR_DOWN    0x00

typedef enum {
    ELIB_PROTOCOL_STATE_IDLE,
    ELIB_PROTOCOL_STATE_LEN_H,
    ELIB_PROTOCOL_STATE_LEN_L,
    ELIB_PROTOCOL_STATE_ADDR0,
    ELIB_PROTOCOL_STATE_ADDR1,
    ELIB_PROTOCOL_STATE_ADDR2,
    ELIB_PROTOCOL_STATE_ADDR3,
    ELIB_PROTOCOL_STATE_SEQ,
    ELIB_PROTOCOL_STATE_DESC,
    ELIB_PROTOCOL_STATE_DATA,
    ELIB_PROTOCOL_STATE_CRC_H,
    ELIB_PROTOCOL_STATE_CRC_L,
    ELIB_PROTOCOL_STATE_TAIL
} elib_protocol_state_t;

typedef struct {
    elib_protocol_state_t state;
    uint16_t length;
    uint16_t rx_cnt;
    uint8_t  seq;
    uint8_t  desc;
    uint16_t crc;
} elib_protocol_parse_ctx_t;

void elib_protocol_parse_init(elib_protocol_parse_ctx_t *ctx);

uint16_t elib_protocol_crc16_calc(const uint8_t *data, size_t len);

uint8_t elib_protocol_desc_get_type(uint8_t desc);

uint8_t elib_protocol_desc_get_frag(uint8_t desc);

uint8_t elib_protocol_desc_get_dir(uint8_t desc);

uint8_t elib_protocol_meta_get_count(const uint8_t *data, size_t len);

elib_protocol_err_t elib_protocol_parse_byte(elib_protocol_parse_ctx_t *ctx,
                                               uint8_t byte,
                                               uint8_t *frame_seq,
                                               uint8_t *frame_desc,
                                               size_t *frame_data_len);

elib_protocol_err_t elib_protocol_get_next_meta(size_t *pos,
                                                 const uint8_t *data,
                                                 size_t len,
                                                 elib_protocol_meta_t **meta);

void elib_protocol_ctx_init(elib_protocol_ctx_t *ctx);

elib_protocol_err_t elib_protocol_process(elib_protocol_ctx_t *ctx,
                                           uint32_t dt_ms,
                                           size_t rx_len,
                                           size_t *consumed,
                                           uint8_t dir);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_PROTOCOL_CORE_H */
