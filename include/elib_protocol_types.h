#ifndef ELIB_PROTOCOL_TYPES_H
#define ELIB_PROTOCOL_TYPES_H

#include "elib_protocol_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Data types
 */
typedef enum {
    ELIB_PROTOCOL_DATA_U8    = 0,
    ELIB_PROTOCOL_DATA_U16   = 1,
    ELIB_PROTOCOL_DATA_U32   = 2,
    ELIB_PROTOCOL_DATA_U64   = 3,
    ELIB_PROTOCOL_DATA_I8    = 4,
    ELIB_PROTOCOL_DATA_I16   = 5,
    ELIB_PROTOCOL_DATA_I32   = 6,
    ELIB_PROTOCOL_DATA_I64   = 7,
    ELIB_PROTOCOL_DATA_BOOL  = 8,
    ELIB_PROTOCOL_DATA_ARRAY = 9
} elib_protocol_data_type_t;

/**
 * Metadata entry - overlays buffer, fdata[] is variable length
 */
typedef struct {
    uint16_t id;
    uint8_t  type;
    uint16_t len;
    uint8_t  fdata[];
} elib_protocol_meta_t;

/**
 * Frame context - passed to on_frame callback
 */
typedef struct {
    uint8_t        addr[4];
    uint8_t        seq;
    uint8_t        desc;
    const uint8_t *data;
    size_t         data_len;
} elib_protocol_frame_ctx_t;

/**
 * Platform ops - user registers callbacks
 */
typedef struct {
    void (*on_frame)(const elib_protocol_frame_ctx_t *ctx);
} elib_protocol_ops_t;

/**
 * Static buffers - user pre-allocates
 */
typedef struct {
    uint8_t *rx_frame_buf;
    size_t   rx_frame_buf_size;
} elib_protocol_bufs_t;

/**
 * Common context - shared by host and slave
 */
typedef struct {
    const elib_protocol_ops_t *ops;
    elib_protocol_bufs_t       bufs;
    uint32_t                   tick_ms;
    uint32_t                   timeout_cfg;
    size_t                     prev_rx_len;
    /* parse state */
    uint8_t                    parse_state;
    uint16_t                   parse_length;
    uint16_t                   parse_rx_cnt;
    uint8_t                    parse_addr[4];
    uint8_t                    parse_seq;
    uint8_t                    parse_desc;
    size_t                     frame_pos;
} elib_protocol_ctx_t;

/**
 * Host context (alias for common context)
 */
typedef elib_protocol_ctx_t elib_protocol_host_ctx_t;

/**
 * Host config
 */
typedef struct {
    uint32_t polling_interval_ms;
    uint32_t timeout_ms;
} elib_protocol_host_cfg_t;

/**
 * Slave context (alias for common context)
 */
typedef elib_protocol_ctx_t elib_protocol_slave_ctx_t;

/**
 * Slave config
 */
typedef struct {
    uint32_t polling_interval_ms;
    uint32_t timeout_ms;
} elib_protocol_slave_cfg_t;

#ifdef __cplusplus
}
#endif

#endif /* ELIB_PROTOCOL_TYPES_H */
