#include "elib_protocol_core.h"
#include <string.h>

void elib_protocol_parse_init(elib_protocol_parse_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->state  = ELIB_PROTOCOL_STATE_IDLE;
    ctx->length = 0;
    ctx->rx_cnt = 0;
    ctx->crc    = 0;
}

uint16_t elib_protocol_crc16_calc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint8_t elib_protocol_desc_get_type(uint8_t desc)
{
    return desc & ELIB_PROTOCOL_DESC_TYPE_MASK;
}

uint8_t elib_protocol_desc_get_frag(uint8_t desc)
{
    return desc & ELIB_PROTOCOL_DESC_FRAG_MASK;
}

uint8_t elib_protocol_desc_get_dir(uint8_t desc)
{
    return desc & ELIB_PROTOCOL_DESC_DIR_MASK;
}

elib_protocol_err_t elib_protocol_parse_byte(elib_protocol_parse_ctx_t *ctx,
                                               uint8_t byte,
                                               uint8_t *frame_addr,
                                               uint8_t *frame_seq,
                                               uint8_t *frame_desc,
                                               size_t *frame_data_len)
{
    if (!ctx) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    switch (ctx->state) {
    case ELIB_PROTOCOL_STATE_IDLE:
        if (byte == ELIB_PROTOCOL_FRAME_HEAD) {
            ctx->state  = ELIB_PROTOCOL_STATE_LEN_H;
            ctx->rx_cnt = 0;
        }
        break;

    case ELIB_PROTOCOL_STATE_LEN_H:
        ctx->length = (uint16_t)byte << 8;
        ctx->state  = ELIB_PROTOCOL_STATE_LEN_L;
        break;

    case ELIB_PROTOCOL_STATE_LEN_L:
        ctx->length |= byte;
        if (ctx->length < ELIB_PROTOCOL_FRAME_LEN_MIN) {
            ctx->state = ELIB_PROTOCOL_STATE_IDLE;
        } else {
            ctx->state  = ELIB_PROTOCOL_STATE_ADDR0;
            ctx->rx_cnt = 0;
        }
        break;

    case ELIB_PROTOCOL_STATE_ADDR0:
        ctx->addr[0] = byte;
        ctx->state   = ELIB_PROTOCOL_STATE_ADDR1;
        break;

    case ELIB_PROTOCOL_STATE_ADDR1:
        ctx->addr[1] = byte;
        ctx->state   = ELIB_PROTOCOL_STATE_ADDR2;
        break;

    case ELIB_PROTOCOL_STATE_ADDR2:
        ctx->addr[2] = byte;
        ctx->state   = ELIB_PROTOCOL_STATE_ADDR3;
        break;

    case ELIB_PROTOCOL_STATE_ADDR3:
        ctx->addr[3] = byte;
        ctx->state   = ELIB_PROTOCOL_STATE_SEQ;
        break;

    case ELIB_PROTOCOL_STATE_SEQ:
        ctx->seq    = byte;
        ctx->state  = ELIB_PROTOCOL_STATE_DESC;
        break;

    case ELIB_PROTOCOL_STATE_DESC:
        ctx->desc   = byte;
        ctx->state  = ELIB_PROTOCOL_STATE_DATA;
        ctx->rx_cnt = 0;
        break;

    case ELIB_PROTOCOL_STATE_DATA:
        ctx->rx_cnt++;
        if (ctx->rx_cnt >= ctx->length - ELIB_PROTOCOL_FRAME_OVERHEAD) {
            ctx->state = ELIB_PROTOCOL_STATE_CRC_H;
        }
        break;

    case ELIB_PROTOCOL_STATE_CRC_H:
        ctx->crc   = (uint16_t)byte << 8;
        ctx->state = ELIB_PROTOCOL_STATE_CRC_L;
        break;

    case ELIB_PROTOCOL_STATE_CRC_L:
        ctx->crc |= byte;
        ctx->state = ELIB_PROTOCOL_STATE_TAIL;
        break;

    case ELIB_PROTOCOL_STATE_TAIL:
        ctx->state = ELIB_PROTOCOL_STATE_IDLE;
        if (byte != ELIB_PROTOCOL_FRAME_TAIL) {
            return ELIB_PROTOCOL_ERR_FRAME;
        }
        if (frame_addr) {
            memcpy(frame_addr, ctx->addr, 4);
        }
        if (frame_seq) {
            *frame_seq = ctx->seq;
        }
        if (frame_desc) {
            *frame_desc = ctx->desc;
        }
        if (frame_data_len) {
            *frame_data_len = ctx->rx_cnt;
        }
        return ELIB_PROTOCOL_OK;

    default:
        ctx->state = ELIB_PROTOCOL_STATE_IDLE;
        break;
    }

    (void)buf_size;

    return ELIB_PROTOCOL_ERR_FRAME;
}

uint8_t elib_protocol_meta_get_count(const uint8_t *data, size_t len)
{
    if (!data || len < 1) {
        return 0;
    }
    return data[0];
}

elib_protocol_err_t elib_protocol_get_next_meta(size_t *pos,
                                                 const uint8_t *data,
                                                 size_t len,
                                                 elib_protocol_meta_t **meta)
{
    if (!pos || !data || !meta) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    if (*pos == 0) {
        *pos = 1;
    }

    if (*pos >= len) {
        return ELIB_PROTOCOL_ERR_FRAME;
    }

    if (*pos + 5 > len) {
        return ELIB_PROTOCOL_ERR_FRAME;
    }

    *meta = (elib_protocol_meta_t *)&data[*pos];
    *pos += 5 + (*meta)->len;

    if (*pos > len) {
        return ELIB_PROTOCOL_ERR_FRAME;
    }

    return ELIB_PROTOCOL_OK;
}

void elib_protocol_ctx_init(elib_protocol_ctx_t *ctx)
{
    ctx->parse_state  = ELIB_PROTOCOL_STATE_IDLE;
    ctx->parse_length = 0;
    ctx->parse_rx_cnt = 0;
    memset(ctx->parse_addr, 0, 4);
    ctx->frame_pos    = 0;
}

elib_protocol_err_t elib_protocol_process(elib_protocol_ctx_t *ctx,
                                           uint32_t dt_ms,
                                           size_t rx_len,
                                           size_t *consumed,
                                           uint8_t dir)
{
    if (!ctx) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    if (consumed) {
        *consumed = 0;
    }

    if (rx_len != ctx->prev_rx_len) {
        ctx->tick_ms     = 0;
        ctx->prev_rx_len = rx_len;

        for (size_t i = 0; i < rx_len; i++) {
            if (ctx->parse_state == ELIB_PROTOCOL_STATE_IDLE) {
                if (ctx->bufs.rx_frame_buf[i] != ELIB_PROTOCOL_FRAME_HEAD) {
                    continue;
                }
                ctx->frame_pos = i;
            }

            elib_protocol_parse_ctx_t p = {
                .state  = ctx->parse_state,
                .length = ctx->parse_length,
                .rx_cnt = ctx->parse_rx_cnt,
                .seq    = ctx->parse_seq,
                .desc   = ctx->parse_desc,
                .crc    = 0
            };

            elib_protocol_err_t err = elib_protocol_parse_byte(
                &p, ctx->bufs.rx_frame_buf[i],
                ctx->parse_addr, &ctx->parse_seq, &ctx->parse_desc, NULL);

            ctx->parse_state  = p.state;
            ctx->parse_length = p.length;
            ctx->parse_rx_cnt = p.rx_cnt;
            memcpy(ctx->parse_addr, p.addr, 4);

            if (ctx->parse_state == ELIB_PROTOCOL_STATE_SEQ &&
                ctx->frame_pos + ctx->parse_length > ctx->bufs.rx_frame_buf_size) {
                ctx->parse_state = ELIB_PROTOCOL_STATE_IDLE;
                continue;
            }

            if (err == ELIB_PROTOCOL_OK) {
                uint16_t crc_calc = elib_protocol_crc16_calc(
                    &ctx->bufs.rx_frame_buf[ctx->frame_pos],
                    ctx->parse_length - 2);
                if (crc_calc != p.crc) {
                    err = ELIB_PROTOCOL_ERR_CHECKSUM;
                }
            }

            if (err == ELIB_PROTOCOL_OK) {
                if (ctx->ops->on_frame &&
                    elib_protocol_desc_get_dir(ctx->parse_desc) == dir) {
                    elib_protocol_frame_ctx_t frame = {
                        .addr     = {ctx->parse_addr[0], ctx->parse_addr[1],
                                     ctx->parse_addr[2], ctx->parse_addr[3]},
                        .seq      = ctx->parse_seq,
                        .desc     = ctx->parse_desc,
                        .data     = &ctx->bufs.rx_frame_buf[ctx->frame_pos + ELIB_PROTOCOL_FRAME_OVERHEAD],
                        .data_len = ctx->parse_rx_cnt
                    };
                    ctx->ops->on_frame(&frame);
                }

                if (consumed) {
                    *consumed = i + 1;
                }
            }
            elib_protocol_ctx_init(ctx);
            break;
        }

        if (consumed && *consumed == 0) {
            *consumed = ctx->frame_pos;
        }
    } else {
        ctx->tick_ms += dt_ms;

        if (ctx->tick_ms >= ctx->timeout_cfg) {
            if (consumed) {
                *consumed = rx_len;
            }
            ctx->tick_ms     = 0;
            ctx->prev_rx_len = 0;
            elib_protocol_ctx_init(ctx);
    return ELIB_PROTOCOL_OK;
}

size_t elib_protocol_build_header(uint8_t *buf, size_t buf_size,
                                  const uint8_t *addr,
                                  uint8_t seq, uint8_t desc)
{
    if (!buf || buf_size < ELIB_PROTOCOL_FRAME_OVERHEAD + 1) {
        return 0;
    }

    buf[0] = ELIB_PROTOCOL_FRAME_HEAD;
    buf[1] = 0;
    buf[2] = 0;
    if (addr) {
        memcpy(&buf[3], addr, 4);
    } else {
        memset(&buf[3], 0, 4);
    }
    buf[7] = seq;
    buf[8] = desc;
    buf[9] = 0;

    return 10;
}

size_t elib_protocol_build_meta(uint8_t *buf, size_t buf_size,
                                uint16_t id, uint8_t type,
                                const void *data, uint16_t len)
{
    if (!buf || buf_size < 5 + len) {
        return 0;
    }

    buf[0] = (id >> 8) & 0xFF;
    buf[1] = id & 0xFF;
    buf[2] = type;
    buf[3] = (len >> 8) & 0xFF;
    buf[4] = len & 0xFF;

    if (len > 0 && data) {
        memcpy(&buf[5], data, len);
    }

    return 5 + len;
}

size_t elib_protocol_build_pack(uint8_t *buf, size_t buf_size, size_t data_len)
{
    if (!buf || buf_size < ELIB_PROTOCOL_FRAME_OVERHEAD + data_len) {
        return 0;
    }

    size_t frame_len = ELIB_PROTOCOL_FRAME_OVERHEAD + data_len;
    buf[1] = (frame_len >> 8) & 0xFF;
    buf[2] = frame_len & 0xFF;

    uint8_t meta_count = 0;
    size_t pos = 10;
    while (pos < frame_len - 3) {
        meta_count++;
        uint16_t meta_len = ((uint16_t)buf[pos + 3] << 8) | buf[pos + 4];
        pos += 5 + meta_len;
    }
    buf[9] = meta_count;

    uint16_t crc = elib_protocol_crc16_calc(buf, frame_len - 3);
    buf[frame_len - 3] = (crc >> 8) & 0xFF;
    buf[frame_len - 2] = crc & 0xFF;

    buf[frame_len - 1] = ELIB_PROTOCOL_FRAME_TAIL;

    return frame_len;
}
    }

    return ELIB_PROTOCOL_OK;
}
