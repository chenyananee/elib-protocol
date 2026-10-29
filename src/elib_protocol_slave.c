/* elib_protocol_slave.c - slave role (thin on the shared engine)
 *
 * The slave filters incoming frames: only those addressed to its own
 * 4-byte station address (decoded strictly in the wire order) or to
 * the broadcast address pass; anything else is swallowed silently. */
#include "elib_protocol_core.h"
#include <string.h>

elib_protocol_err_t elib_protocol_slave_init(elib_protocol_slave_ctx_t *ctx,
                                             const elib_protocol_ops_t *ops,
                                             const elib_protocol_bufs_t *bufs,
                                             const elib_protocol_slave_cfg_t *cfg) {
    if (ctx == NULL || ops == NULL || bufs == NULL || cfg == NULL ||
        cfg->addr == NULL || ops->on_frame == NULL ||
        bufs->frame_buf == NULL || bufs->frame_buf_size < ELIB_PROTOCOL_FRAME_LEN_MIN) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    (void)memcpy(ctx->self_addr, cfg->addr, 4);
    elib_protocol_engine_init(&ctx->core, ops, bufs, ctx->self_addr);
    return ELIB_PROTOCOL_OK;
}

elib_protocol_err_t elib_protocol_slave_feed(elib_protocol_slave_ctx_t *ctx,
                                             const uint8_t *bytes, uint16_t len) {
    return elib_protocol_engine_feed(&ctx->core, bytes, len);
}

elib_protocol_err_t elib_protocol_slave_feed_byte(elib_protocol_slave_ctx_t *ctx,
                                                  uint8_t byte) {
    return elib_protocol_engine_feed_byte(&ctx->core, byte);
}

elib_protocol_err_t elib_protocol_slave_reset(elib_protocol_slave_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }
    elib_protocol_engine_reset(&ctx->core);
    return ELIB_PROTOCOL_OK;
}
