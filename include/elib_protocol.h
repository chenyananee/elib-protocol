#ifndef ELIB_PROTOCOL_H
#define ELIB_PROTOCOL_H

#include "elib_protocol_types.h"
#include "elib_protocol_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ELIB_PROTOCOL_VERSION "0.1.0"

/**
 * Host API
 */
elib_protocol_err_t elib_protocol_host_init(elib_protocol_host_ctx_t *ctx,
                                             const elib_protocol_ops_t *ops,
                                             const elib_protocol_bufs_t *bufs,
                                             const elib_protocol_host_cfg_t *cfg);

elib_protocol_err_t elib_protocol_host_process(elib_protocol_host_ctx_t *ctx,
                                                 uint32_t dt_ms,
                                                 size_t rx_len,
                                                 size_t *consumed);

elib_protocol_err_t elib_protocol_host_reset(elib_protocol_host_ctx_t *ctx);

/**
 * Slave API
 */
elib_protocol_err_t elib_protocol_slave_init(elib_protocol_slave_ctx_t *ctx,
                                              const elib_protocol_ops_t *ops,
                                              const elib_protocol_bufs_t *bufs,
                                              const elib_protocol_slave_cfg_t *cfg);

elib_protocol_err_t elib_protocol_slave_process(elib_protocol_slave_ctx_t *ctx,
                                                  uint32_t dt_ms,
                                                  size_t rx_len,
                                                  size_t *consumed);

elib_protocol_err_t elib_protocol_slave_reset(elib_protocol_slave_ctx_t *ctx);

/**
 * Meta iterator - get next metadata from frame data
 * @param pos     in/out: current position, init to 0
 * @param data    frame data pointer
 * @param len     frame data length
 * @param meta    output: pointer to metadata entry
 * @return ELIB_PROTOCOL_OK on success, ELIB_PROTOCOL_ERR_FRAME when no more
 */
elib_protocol_err_t elib_protocol_get_next_meta(size_t *pos,
                                                 const uint8_t *data,
                                                 size_t len,
                                                 elib_protocol_meta_t **meta);

/**
 * Frame builder - build frame header
 * @param buf       output buffer
 * @param buf_size  output buffer size
 * @param addr      slave address (4 bytes, NULL for broadcast 0xFFFFFFFF)
 * @param seq       sequence number (0-255)
 * @param desc      descriptor byte
 * @return bytes written (10), or 0 on error
 */
size_t elib_protocol_build_header(uint8_t *buf, size_t buf_size,
                                  const uint8_t *addr,
                                  uint8_t seq, uint8_t desc);

/**
 * Frame builder - add metadata entry
 * @param buf       output buffer (after previous data)
 * @param buf_size  remaining buffer size
 * @param id        data ID
 * @param type      data type
 * @param data      data pointer (can be NULL if len == 0)
 * @param len       data length (max 65535)
 * @return bytes written, or 0 on error
 */
size_t elib_protocol_build_meta(uint8_t *buf, size_t buf_size,
                                uint16_t id, uint8_t type,
                                const void *data, uint16_t len);

/**
 * Frame builder - finalize frame (calculate length, CRC, add tail)
 * @param buf       output buffer (starting from header)
 * @param buf_size  buffer size
 * @param data_len  data unit length (excluding meta_count)
 * @return total frame length, or 0 on error
 */
size_t elib_protocol_build_pack(uint8_t *buf, size_t buf_size, size_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_PROTOCOL_H */
