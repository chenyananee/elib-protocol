/* elib_protocol_host.c - host role (thin on the shared engine)
 *
 * The host has no own address: every frame heading up the bus
 * (responses and events) lands in its on_frame callback. */
#include "elib_protocol_core.h"

elib_protocol_err_t elib_protocol_host_init(elib_protocol_host_ctx_t *ctx,
                                            const elib_protocol_ops_t *ops,
                                            const elib_protocol_bufs_t *bufs) {
    if (ctx == NULL || ops == NULL || bufs == NULL || ops->on_frame == NULL ||
        bufs->frame_buf == NULL || bufs->frame_buf_size < ELIB_PROTOCOL_FRAME_LEN_MIN) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    elib_protocol_engine_init(&ctx->core, ops, bufs, NULL /* accept any */);
    return ELIB_PROTOCOL_OK;
}

elib_protocol_err_t elib_protocol_host_feed(elib_protocol_host_ctx_t *ctx,
                                            const uint8_t *bytes, uint16_t len) {
    return elib_protocol_engine_feed(&ctx->core, bytes, len);
}

elib_protocol_err_t elib_protocol_host_feed_byte(elib_protocol_host_ctx_t *ctx,
                                                 uint8_t byte) {
    return elib_protocol_engine_feed_byte(&ctx->core, byte);
}

elib_protocol_err_t elib_protocol_host_reset(elib_protocol_host_ctx_t *ctx) {
    if (ctx == NULL) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }
    elib_protocol_engine_reset(&ctx->core);
    return ELIB_PROTOCOL_OK;
}
