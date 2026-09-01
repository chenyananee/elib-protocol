#include "../include/elib_protocol.h"
#include <stdio.h>
#include <string.h>

static int frame_received = 0;

static void on_frame_received(const elib_protocol_frame_ctx_t *ctx)
{
    frame_received = 1;
    (void)ctx;
}

static uint8_t rx_frame_buf[256];

static uint16_t crc16_calc(const uint8_t *data, size_t len)
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

static int test_valid_frame(void)
{
    elib_protocol_ops_t ops = { .on_frame = on_frame_received };
    elib_protocol_bufs_t bufs = {
        .rx_frame_buf = rx_frame_buf, .rx_frame_buf_size = sizeof(rx_frame_buf)
    };
    elib_protocol_host_cfg_t cfg = { .polling_interval_ms = 10, .timeout_ms = 100 };

    elib_protocol_host_ctx_t ctx;
    elib_protocol_host_init(&ctx, &ops, &bufs, &cfg);

    uint8_t frame[] = {
        0x68,
        0x00, 0x0F,
        0x01,
        0x02,
        0x01,
        0x00, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x00,
        0x16
    };

    size_t crc_data_len = sizeof(frame) - 3;
    uint16_t crc = crc16_calc(frame, crc_data_len);
    frame[crc_data_len] = (crc >> 8) & 0xFF;
    frame[crc_data_len + 1] = crc & 0xFF;

    memcpy(rx_frame_buf, frame, sizeof(frame));
    frame_received = 0;

    size_t consumed = 0;
    elib_protocol_host_process(&ctx, 10, sizeof(frame), &consumed);

    return frame_received ? 0 : 1;
}

static int test_invalid_crc(void)
{
    elib_protocol_ops_t ops = { .on_frame = on_frame_received };
    elib_protocol_bufs_t bufs = {
        .rx_frame_buf = rx_frame_buf, .rx_frame_buf_size = sizeof(rx_frame_buf)
    };
    elib_protocol_host_cfg_t cfg = { .polling_interval_ms = 10, .timeout_ms = 100 };

    elib_protocol_host_ctx_t ctx;
    elib_protocol_host_init(&ctx, &ops, &bufs, &cfg);

    uint8_t frame[] = {
        0x68,
        0x00, 0x0F,
        0x01,
        0x02,
        0x01,
        0x00, 0x01, 0x00, 0x00, 0x01,
        0x00, 0x00,
        0x16
    };

    frame[12] = 0xFF;
    frame[13] = 0xFF;

    memcpy(rx_frame_buf, frame, sizeof(frame));
    frame_received = 0;

    size_t consumed = 0;
    elib_protocol_host_process(&ctx, 10, sizeof(frame), &consumed);

    return frame_received ? 1 : 0;
}

int main(void)
{
    int fails = 0;
    fails += test_valid_frame();
    fails += test_invalid_crc();
    printf("crc test: %d/%d passed\n", 2 - fails, 2);
    return fails;
}
