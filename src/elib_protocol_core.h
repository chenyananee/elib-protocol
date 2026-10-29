/* elib_protocol_core.h - internal engine interface (shared by roles) */
#ifndef ELIB_PROTOCOL_CORE_H
#define ELIB_PROTOCOL_CORE_H

#include "elib_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* parse state machine states */
typedef enum {
    ELIB_PROTOCOL_STATE_IDLE = 0,
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
    ELIB_PROTOCOL_STATE_TAIL,
    ELIB_PROTOCOL_STATE_SWALLOW
} elib_protocol_state_t;

/* engine lifecycle: core init/reset, streaming byte feeder.
 * self_addr == NULL turns the receive filter off (host role). */
void elib_protocol_engine_init(elib_protocol_engine_t *e,
                               const elib_protocol_ops_t *ops,
                               const elib_protocol_bufs_t *bufs,
                               const uint8_t *self_addr);

void elib_protocol_engine_reset(elib_protocol_engine_t *e);

elib_protocol_err_t elib_protocol_engine_feed_byte(elib_protocol_engine_t *e,
                                                   uint8_t byte);

/* array feeder: identical to N x feed_byte; returns the drop reason of
 * the last discarded frame in the batch, OK otherwise */
elib_protocol_err_t elib_protocol_engine_feed(elib_protocol_engine_t *e,
                                              const uint8_t *bytes,
                                              uint16_t len);

/* CRC16/CCITT-FALSE helper (whole-buffer version for the builders) */
uint16_t elib_protocol_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_PROTOCOL_CORE_H */
