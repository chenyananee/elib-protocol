#include "../include/elib_protocol.h"
#include <stdio.h>
#include <string.h>

static void null_on_frame(const elib_protocol_frame_ctx_t *ctx)
{
    (void)ctx;
}

static uint8_t rx_frame_buf[256];

static int test_host_init(void)
{
    elib_protocol_ops_t ops = { .on_frame = null_on_frame };
    elib_protocol_bufs_t bufs = {
        .rx_frame_buf = rx_frame_buf, .rx_frame_buf_size = sizeof(rx_frame_buf)
    };
    elib_protocol_host_cfg_t cfg = { .polling_interval_ms = 10, .timeout_ms = 100 };

    elib_protocol_host_ctx_t ctx;
    elib_protocol_err_t err = elib_protocol_host_init(&ctx, &ops, &bufs, &cfg);
    return (err == ELIB_PROTOCOL_OK) ? 0 : 1;
}

static int test_host_init_invalid(void)
{
    elib_protocol_err_t err = elib_protocol_host_init(NULL, NULL, NULL, NULL);
    return (err == ELIB_PROTOCOL_ERR_INVALID_PARAM) ? 0 : 1;
}

static int test_slave_init(void)
{
    elib_protocol_ops_t ops = { .on_frame = null_on_frame };
    elib_protocol_bufs_t bufs = {
        .rx_frame_buf = rx_frame_buf, .rx_frame_buf_size = sizeof(rx_frame_buf)
    };
    elib_protocol_slave_cfg_t cfg = { .polling_interval_ms = 10, .timeout_ms = 100 };

    elib_protocol_slave_ctx_t ctx;
    elib_protocol_err_t err = elib_protocol_slave_init(&ctx, &ops, &bufs, &cfg);
    return (err == ELIB_PROTOCOL_OK) ? 0 : 1;
}

static int test_slave_init_invalid(void)
{
    elib_protocol_err_t err = elib_protocol_slave_init(NULL, NULL, NULL, NULL);
    return (err == ELIB_PROTOCOL_ERR_INVALID_PARAM) ? 0 : 1;
}

int main(void)
{
    int fails = 0;
    fails += test_host_init();
    fails += test_host_init_invalid();
    fails += test_slave_init();
    fails += test_slave_init_invalid();
    printf("protocol: %d/%d passed\n", 4 - fails, 4);
    return fails;
}
