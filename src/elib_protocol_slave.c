#include "../include/elib_protocol.h"
#include "elib_protocol_core.h"

elib_protocol_err_t elib_protocol_slave_init(elib_protocol_slave_ctx_t *ctx,
                                              const elib_protocol_ops_t *ops,
                                              const elib_protocol_bufs_t *bufs,
                                              const elib_protocol_slave_cfg_t *cfg)
{
    if (!ctx || !ops || !bufs || !cfg || !cfg->addr) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    ctx->ops         = ops;
    ctx->bufs        = *bufs;
    ctx->tick_ms     = 0;
    ctx->timeout_cfg = cfg->timeout_ms;
    ctx->prev_rx_len = 0;
    memcpy(ctx->self_addr, cfg->addr, 4);
    elib_protocol_ctx_init(ctx);

    return ELIB_PROTOCOL_OK;
}

elib_protocol_err_t elib_protocol_slave_process(elib_protocol_slave_ctx_t *ctx,
                                                 uint32_t dt_ms,
                                                 size_t rx_len,
                                                 size_t *consumed)
{
    return elib_protocol_process(ctx, dt_ms, rx_len, consumed,
                                 ELIB_PROTOCOL_DESC_DIR_DOWN);
}

elib_protocol_err_t elib_protocol_slave_reset(elib_protocol_slave_ctx_t *ctx)
{
    if (!ctx) {
        return ELIB_PROTOCOL_ERR_INVALID_PARAM;
    }

    ctx->tick_ms     = 0;
    ctx->prev_rx_len = 0;
    elib_protocol_ctx_init(ctx);

    return ELIB_PROTOCOL_OK;
}
