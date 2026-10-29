/* elib_protocol_core.c - shared protocol engine
 *
 * Streaming parser: bytes arrive one at a time (array feeder is a
 * loop over the very same path), each byte is consumed exactly once,
 * so there is no head re-scanning and no snapshot bookkeeping. Frame
 * progress is bounded by the LENGTH field: the reassembly buffer only
 * ever stores a frame whose claimed size fit - a bogus or oversized
 * claim never occupies buffer space (anti-clog gate #1).
 *
 * Dropped frames follow two deterministic routines:
 *   bogus claim (LEN < minimum)  - discard the 3 consumed bytes and
 *                                  go back to hunting immediately
 *   rejected claim (LEN > buffer, foreign address) - swallow the
 *                                  remaining quota byte by byte, then
 *                                  resync strictly at the next 0x68
 * The payload is NEVER re-hunted for new heads mid-frame (the wire has
 * no escaping; a 0x68 inside fdata is ordinary data).
 */
#include "elib_protocol_core.h"
#include <string.h>

#define CRC_INIT 0xFFFFu
#define CRC_POLY 0x1021u

/* ---- CRC ---- */

uint16_t elib_protocol_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = CRC_INIT;
    size_t i;
    for (i = 0; i < len; i++) {
        uint16_t j;
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++) {
            crc = (crc & 0x8000u) ? (uint16_t)(crc << 1) ^ CRC_POLY
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ops->crc NULL fallback: the built-in software algorithm, shaped as
 * an elib_protocol_crc_fn (user data ignored) */
static uint16_t crc16_software(void *ud, const uint8_t *data, size_t len) {
    (void)ud;
    return elib_protocol_crc16(data, len);
}

/* ---- engine lifecycle ---- */

void elib_protocol_engine_init(elib_protocol_engine_t *e,
                               const elib_protocol_ops_t *ops,
                               const elib_protocol_bufs_t *bufs,
                               const uint8_t *self_addr) {
    e->ops = ops;
    e->bufs = *bufs;
    e->self_addr = self_addr;
    e->state = ELIB_PROTOCOL_STATE_IDLE;
    e->claim = 0;
    e->filled = 0;
    e->swallow = 0;
    e->crc_rx = 0;
    e->bit_flags.initialized = 1;
    e->bit_flags.crc_ok = 0;
    e->bit_flags.busy = 0;
}

void elib_protocol_engine_reset(elib_protocol_engine_t *e) {
    e->state = ELIB_PROTOCOL_STATE_IDLE;
    e->claim = 0;
    e->filled = 0;
    e->swallow = 0;
    e->crc_rx = 0;
    /* busy stays off: nothing inside a callback survives a reset */
    e->bit_flags.busy = 0;
    e->bit_flags.crc_ok = 0;
}

/* store one byte into the reassembly buffer. The checksum is NOT
 * accumulated byte by byte: the whole coverage region is buffered and
 * verified in one call when the CRC field completes. */
static void store(elib_protocol_engine_t *e, uint8_t byte) {
    e->bufs.frame_buf[e->filled] = byte;
    e->filled++;
}

static elib_protocol_err_t feed_one(elib_protocol_engine_t *e, uint8_t byte) {
    switch (e->state) {

    case ELIB_PROTOCOL_STATE_IDLE:
        /* discarded noise never touches the buffer */
        if (byte == (uint8_t)ELIB_PROTOCOL_FRAME_HEAD) {
            e->bufs.frame_buf[0] = byte;
            e->filled = 1;
            e->state = ELIB_PROTOCOL_STATE_LEN_H;
        }
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_LEN_H:
        store(e, byte);
        e->state = ELIB_PROTOCOL_STATE_LEN_L;
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_LEN_L: {
        uint16_t claim;
        store(e, byte);
        claim = elib_protocol_get_u16(&e->bufs.frame_buf[1]);
        if (claim < ELIB_PROTOCOL_FRAME_LEN_MIN) {
            /* bogus claim: no trustworthy quota to swallow - drop the
             * 3 consumed bytes and hunt from the NEXT 0x68 */
            e->state = ELIB_PROTOCOL_STATE_IDLE;
            e->filled = 0;
            return ELIB_PROTOCOL_ERR_FRAME;
        }
        if (claim > e->bufs.frame_buf_size) {
            /* coherent but unstoreable: swallow the declared quota,
             * then resync strictly */
            e->swallow = (uint16_t)(claim - e->filled);
            e->state = ELIB_PROTOCOL_STATE_SWALLOW;
            e->filled = 0;
            return ELIB_PROTOCOL_ERR_FRAME;
        }
        e->claim = claim;
        e->state = ELIB_PROTOCOL_STATE_ADDR0;
        return ELIB_PROTOCOL_OK;
    }

    case ELIB_PROTOCOL_STATE_ADDR0:
    case ELIB_PROTOCOL_STATE_ADDR1:
    case ELIB_PROTOCOL_STATE_ADDR2:
        store(e, byte);
        e->state = (uint8_t)(e->state + 1u);
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_ADDR3: {
        store(e, byte);
        /* address filter: only own station and broadcast pass */
        if (e->self_addr != NULL) {
            const uint8_t *a = &e->bufs.frame_buf[3];
            uint8_t broadcast = (uint8_t)((a[0] == 0xFFu) && (a[1] == 0xFFu) &&
                                          (a[2] == 0xFFu) && (a[3] == 0xFFu));
            if (!broadcast && memcmp(a, e->self_addr, 4) != 0) {
                e->swallow = (uint16_t)(e->claim - e->filled);
                e->filled = 0;
                e->state = ELIB_PROTOCOL_STATE_SWALLOW;
                return ELIB_PROTOCOL_OK; /* silent: multi-drop normality */
            }
        }
        e->state = ELIB_PROTOCOL_STATE_SEQ;
        return ELIB_PROTOCOL_OK;
    }

    case ELIB_PROTOCOL_STATE_SEQ:
        store(e, byte);
        e->state = ELIB_PROTOCOL_STATE_DESC;
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_DESC:
        store(e, byte);
        e->state = (e->filled == (uint16_t)(e->claim - 3u))
                       ? ELIB_PROTOCOL_STATE_CRC_H   /* empty data */
                       : ELIB_PROTOCOL_STATE_DATA;
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_DATA:
        store(e, byte);
        if (e->filled == (uint16_t)(e->claim - 3u)) {
            e->state = ELIB_PROTOCOL_STATE_CRC_H;
        }
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_CRC_H:
        e->bufs.frame_buf[e->filled++] = byte; /* CRC field, not coverage */
        e->state = ELIB_PROTOCOL_STATE_CRC_L;
        return ELIB_PROTOCOL_OK;

    case ELIB_PROTOCOL_STATE_CRC_L: {
        const elib_protocol_crc_fn crc =
            (e->ops->crc != NULL) ? e->ops->crc : crc16_software;

        e->bufs.frame_buf[e->filled++] = byte;
        e->crc_rx = elib_protocol_get_u16(&e->bufs.frame_buf[e->claim - 3u]);
        /* the frame is fully buffered by now (LEN was validated at
         * LEN_L): one call spans [0, claim-3) = head .. data end,
         * exactly the coverage region */
        e->bit_flags.crc_ok =
            (uint8_t)(crc(e->ops->user_data, e->bufs.frame_buf,
                          (size_t)(e->claim - 3u)) == e->crc_rx);
        e->state = ELIB_PROTOCOL_STATE_TAIL;
        return ELIB_PROTOCOL_OK;
    }

    case ELIB_PROTOCOL_STATE_TAIL: {
        elib_protocol_frame_ctx_t frame;

        e->state = ELIB_PROTOCOL_STATE_IDLE;
        if (byte != (uint8_t)ELIB_PROTOCOL_FRAME_TAIL) {
            return ELIB_PROTOCOL_ERR_FRAME;
        }
        if (!e->bit_flags.crc_ok) {
            return ELIB_PROTOCOL_ERR_CHECKSUM;
        }

        /* full frame: deliver with pointers into the frozen buffer */
        (void)memcpy(frame.addr, &e->bufs.frame_buf[3], 4);
        frame.seq = e->bufs.frame_buf[7];
        frame.desc = e->bufs.frame_buf[8];
        frame.data = &e->bufs.frame_buf[ELIB_PROTOCOL_HDR_SIZE];
        frame.data_len = (uint16_t)(e->claim - 12u);
        e->bit_flags.busy = 1;
        e->ops->on_frame(e->ops->user_data, &frame);
        e->bit_flags.busy = 0;

        /* buffer write position and parse state recycle for the next
         * frame right here - no stale bytes survive the callback */
        e->filled = 0;
        e->claim = 0;
        return ELIB_PROTOCOL_OK;
    }

    case ELIB_PROTOCOL_STATE_SWALLOW:
        if (--e->swallow == 0u) {
            e->state = ELIB_PROTOCOL_STATE_IDLE;
        }
        return ELIB_PROTOCOL_OK;

    default:
        e->state = ELIB_PROTOCOL_STATE_IDLE;
        return ELIB_PROTOCOL_ERR_FRAME;
    }
}

elib_protocol_err_t elib_protocol_engine_feed_byte(elib_protocol_engine_t *e,
                                                   uint8_t byte) {
    if (e == NULL || !e->bit_flags.initialized) {
        return ELIB_PROTOCOL_ERR_NOT_INITIALIZED;
    }
    if (e->bit_flags.busy) {
        return ELIB_PROTOCOL_ERR_BUSY;
    }
    return feed_one(e, byte);
}

elib_protocol_err_t elib_protocol_engine_feed(elib_protocol_engine_t *e,
                                              const uint8_t *bytes,
                                              uint16_t len) {
    elib_protocol_err_t err = ELIB_PROTOCOL_OK;
    uint16_t i;

    if (e == NULL || !e->bit_flags.initialized) {
        return ELIB_PROTOCOL_ERR_NOT_INITIALIZED;
    }
    if (bytes == NULL && len != 0u) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }
    if (e->bit_flags.busy) {
        return ELIB_PROTOCOL_ERR_BUSY;
    }

    for (i = 0; i < len; i++) {
        elib_protocol_err_t one = feed_one(e, bytes[i]);
        if (one != ELIB_PROTOCOL_OK) {
            err = one; /* report the LAST discarded frame in the batch */
        }
    }
    return err;
}

/* ---- builders ---- */

size_t elib_protocol_build_header(uint8_t *buf, size_t buf_size,
                                  const uint8_t *addr, uint8_t seq, uint8_t desc) {
    if (buf == NULL || buf_size < 10u) {
        return 0;
    }
    buf[0] = (uint8_t)ELIB_PROTOCOL_FRAME_HEAD;
    elib_protocol_put_u16(&buf[1], 0); /* placeholder until build_pack */
    if (addr != NULL) {
        (void)memcpy(&buf[3], addr, 4);
    } else {
        (void)memset(&buf[3], 0xFF, 4);
    }
    buf[7] = seq;
    buf[8] = desc;
    buf[9] = 0; /* meta_count placeholder until build_pack */
    return 10;
}

size_t elib_protocol_build_meta(uint8_t *buf, size_t buf_size,
                                uint16_t id, uint8_t type,
                                const void *data, uint16_t len) {
    if (buf == NULL || (len != 0u && data == NULL)) {
        return 0;
    }
    if (buf_size < (size_t)5u + (size_t)len) {
        return 0;
    }
    elib_protocol_put_u16(&buf[0], id);
    buf[2] = type;
    elib_protocol_put_u16(&buf[3], len);
    if (len != 0u) {
        (void)memcpy(&buf[5], data, len);
    }
    return (size_t)5u + (size_t)len;
}

/* shared wrap-up: validate/refuse, fill count/LEN, checksum, tail */
static size_t build_pack_impl(uint8_t *buf, size_t buf_size, size_t offset,
                              elib_protocol_crc_fn crc, void *ud) {
    size_t pos;
    size_t frame_len;
    uint8_t count = 0;

    if (buf == NULL || crc == NULL || offset < 10u || offset > buf_size ||
        buf_size - offset < 3u) {
        return 0;
    }
    frame_len = offset + 3u; /* data tail + CRC(2) + tail(1) */

    /* metadata scan: entries must tile the data region exactly */
    pos = 10;
    while (pos < offset) {
        uint16_t mlen;
        if (offset - pos < 5u) {
            return 0;
        }
        if (count == 0xFFu) {
            return 0; /* meta_count is one byte */
        }
        mlen = elib_protocol_get_u16(&buf[pos + 3]);
        pos += 5u + (size_t)mlen;
        if (pos > offset) {
            return 0;
        }
        count++;
    }

    elib_protocol_put_u8(&buf[9], count);
    elib_protocol_put_u16(&buf[1], (uint16_t)frame_len);
    elib_protocol_put_u16(&buf[offset], crc(ud, buf, offset));
    buf[frame_len - 1u] = (uint8_t)ELIB_PROTOCOL_FRAME_TAIL;
    return frame_len;
}

size_t elib_protocol_build_pack(uint8_t *buf, size_t buf_size, size_t offset) {
    return build_pack_impl(buf, buf_size, offset, crc16_software, NULL);
}

size_t elib_protocol_build_pack_crc(uint8_t *buf, size_t buf_size,
                                    size_t offset,
                                    elib_protocol_crc_fn crc, void *ud) {
    return build_pack_impl(buf, buf_size, offset, crc, ud);
}

/* ---- metadata iterator ---- */

elib_protocol_err_t elib_protocol_get_next_meta(uint16_t *pos,
                                                const uint8_t *data,
                                                uint16_t len,
                                                elib_protocol_meta_t *meta) {
    uint32_t here;
    uint32_t next;

    if (pos == NULL || data == NULL || meta == NULL) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    if (*pos == 0u) {
        if (len < ELIB_PROTOCOL_DATA_MIN) {
            return ELIB_PROTOCOL_ERR_FRAME; /* no meta_count at all */
        }
        *pos = 1; /* skip the meta_count byte */
    }

    here = (uint32_t)*pos;
    if (here + 5u > (uint32_t)len) {
        return ELIB_PROTOCOL_ERR_FRAME;
    }
    meta->id = elib_protocol_get_u16(&data[here]);
    meta->type = data[here + 2u];
    meta->len = elib_protocol_get_u16(&data[here + 3u]);
    next = here + 5u + (uint32_t)meta->len;
    if (next > (uint32_t)len) {
        return ELIB_PROTOCOL_ERR_FRAME;
    }
    meta->fdata = (meta->len != 0u) ? &data[here + 5u] : NULL;
    *pos = (uint16_t)next;
    return ELIB_PROTOCOL_OK;
}
