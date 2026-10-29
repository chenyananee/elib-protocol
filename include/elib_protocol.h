/* elib_protocol.h - Lightweight master/slave communication protocol
 * (single-umbrella public API)
 *
 * Frame format (strict, unescaped):
 *   0x68 | LEN(2) | ADDR(4) | SEQ | DESC | DATA... | CRC16(2) | 0x16
 *   DATA = meta_count + N x [ ID(2) | TYPE | LEN(2) | fdata ]
 *   CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over head..data.
 *
 * Rules of the game:
 *  - zero dynamic memory: user allocates the context and the frame
 *    reassembly buffer; the library allocates nothing
 *  - host and slave are separate roles sharing one streaming engine:
 *    bytes feed in (batch or one at a time), frames surface through
 *    the on_frame callback with data pointing into the reassembly
 *    buffer
 *  - every multi-byte field is interpreted in the configured wire
 *    byte order; typed get/put helpers below are the only sanctioned
 *    way to build or read values
 *  - robustness promise: a misalignment collision costs at most one
 *    frame quota plus one bogus head, then the parser resyncs at the
 *    strictly-next real 0x68 - a torn line never clogs the receiver
 *  - stall handling is the user's job via reset(); the library knows
 *    nothing about time
 */
#ifndef ELIB_PROTOCOL_H
#define ELIB_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ELIB_PROTOCOL_VERSION "1.0.0"

/* ---- wire byte order configuration ----
 *
 * Default is big-endian (all wire fields, including LEN, ADDR, meta
 * id/len, CRC and every numeric fdata value). Define
 * ELIB_PROTOCOL_ORDER_LITTLE (-D) to flip the whole library to little
 * endianness for your own deployment. Exactly one of the two must be
 * in force at any translation unit; keep the choice consistent
 * between every party of the bus. */
#if !defined(ELIB_PROTOCOL_ORDER_LITTLE) && !defined(ELIB_PROTOCOL_ORDER_BIG)
#define ELIB_PROTOCOL_ORDER_BIG
#endif
#if defined(ELIB_PROTOCOL_ORDER_LITTLE) && defined(ELIB_PROTOCOL_ORDER_BIG)
#error "elib_protocol: choose exactly one of ELIB_PROTOCOL_ORDER_BIG / LITTLE"
#endif

/* ---- machine endianness detection ----
 *
 * Derived from the compiler where possible (__BYTE_ORDER__ thanks to
 * GCC/Clang), defaulted to big-endian for unknown toolchains, and
 * overridable via -DELIB_PROTOCOL_MACHINE_LITTLE. Exposed for user
 * code that needs to know its own byte order; the get/put helpers
 * below do NOT depend on it (they reassemble values byte by byte in
 * the configured wire order, which is portable everywhere). */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define ELIB_PROTOCOL_MACHINE_BIG
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define ELIB_PROTOCOL_MACHINE_LITTLE
#elif !defined(ELIB_PROTOCOL_MACHINE_LITTLE)
#define ELIB_PROTOCOL_MACHINE_BIG
#endif

/* ---- error codes ---- */

typedef enum {
    ELIB_PROTOCOL_OK = 0,
    ELIB_PROTOCOL_ERR_INVALID_PARAM,    /* init/idle-reject: bad arguments */
    ELIB_PROTOCOL_ERR_NOT_INITIALIZED,  /* API called before a valid init */
    ELIB_PROTOCOL_ERR_BUFFER_TOO_SMALL, /* builders: output buffer too small */
    ELIB_PROTOCOL_ERR_CHECKSUM,         /* rx: frame completed with bad CRC */
    ELIB_PROTOCOL_ERR_FRAME,            /* rx: malformed frame (bad tail, bogus
                                             or oversized length claim);
                                             builders: meta scan off-spec */
    ELIB_PROTOCOL_ERR_BUSY              /* reentrant call during on_frame */
} elib_protocol_err_t;

/* ---- typed wire-order accessors ----
 *
 * All de/serialization goes through these: they read/write the
 * configured wire order by plain byte shifts, so they work on any
 * machine endianness without alignment hazards. bool is one byte
 * (0 or 1). */
static inline uint8_t elib_protocol_get_u8(const uint8_t *b) {
    return b[0];
}

static inline void elib_protocol_put_u8(uint8_t *b, uint8_t v) {
    b[0] = v;
}

static inline uint16_t elib_protocol_get_u16(const uint8_t *b) {
#ifdef ELIB_PROTOCOL_ORDER_LITTLE
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
#else
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
#endif
}

static inline void elib_protocol_put_u16(uint8_t *b, uint16_t v) {
#ifdef ELIB_PROTOCOL_ORDER_LITTLE
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)(v >> 8);
#else
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)(v & 0xFFu);
#endif
}

static inline uint32_t elib_protocol_get_u32(const uint8_t *b) {
    uint32_t v = 0;
    uint16_t i;
    for (i = 0; i < 4; i++) {
#ifdef ELIB_PROTOCOL_ORDER_LITTLE
        v |= ((uint32_t)b[i]) << (8u * i);
#else
        v = (v << 8) | (uint32_t)b[i];
#endif
    }
    return v;
}

static inline void elib_protocol_put_u32(uint8_t *b, uint32_t v) {
    uint16_t i;
    for (i = 0; i < 4; i++) {
#ifdef ELIB_PROTOCOL_ORDER_LITTLE
        b[i] = (uint8_t)(v >> (8u * i));
#else
        b[i] = (uint8_t)(v >> (8u * (3u - i)));
#endif
    }
}

static inline uint64_t elib_protocol_get_u64(const uint8_t *b) {
    uint64_t v = 0;
    uint16_t i;
    for (i = 0; i < 8; i++) {
#ifdef ELIB_PROTOCOL_ORDER_LITTLE
        v |= ((uint64_t)b[i]) << (8u * (uint32_t)i);
#else
        v = (v << 8) | (uint64_t)b[i];
#endif
    }
    return v;
}

static inline void elib_protocol_put_u64(uint8_t *b, uint64_t v) {
    uint16_t i;
    for (i = 0; i < 8; i++) {
#ifdef ELIB_PROTOCOL_ORDER_LITTLE
        b[i] = (uint8_t)(v >> (8u * (uint32_t)i));
#else
        b[i] = (uint8_t)(v >> (8u * (7u - (uint32_t)i)));
#endif
    }
}

static inline int16_t elib_protocol_get_i16(const uint8_t *b) {
    return (int16_t)elib_protocol_get_u16(b);
}

static inline void elib_protocol_put_i16(uint8_t *b, int16_t v) {
    elib_protocol_put_u16(b, (uint16_t)v);
}

static inline int32_t elib_protocol_get_i32(const uint8_t *b) {
    return (int32_t)elib_protocol_get_u32(b);
}

static inline void elib_protocol_put_i32(uint8_t *b, int32_t v) {
    elib_protocol_put_u32(b, (uint32_t)v);
}

static inline int64_t elib_protocol_get_i64(const uint8_t *b) {
    return (int64_t)elib_protocol_get_u64(b);
}

static inline void elib_protocol_put_i64(uint8_t *b, int64_t v) {
    elib_protocol_put_u64(b, (uint64_t)v);
}

/* bool travels as one byte: 1 = true, 0 = false */
static inline uint8_t elib_protocol_get_bool(const uint8_t *b) {
    return (uint8_t)(elib_protocol_get_u8(b) != 0u);
}

static inline void elib_protocol_put_bool(uint8_t *b, uint8_t v) {
    elib_protocol_put_u8(b, (uint8_t)(v != 0u));
}

/* byte-order swap helpers (platform-agnostic value swappers) */
static inline uint16_t elib_protocol_swap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t elib_protocol_swap32(uint32_t v) {
    return ((v >> 24) & 0x000000FFu) |
           ((v >>  8) & 0x0000FF00u) |
           ((v <<  8) & 0x00FF0000u) |
           ((v << 24) & 0xFF000000u);
}

static inline uint64_t elib_protocol_swap64(uint64_t v) {
    return ((v >> 56) & 0x00000000000000FFuLL) |
           ((v >> 40) & 0x000000000000FF00uLL) |
           ((v >> 24) & 0x0000000000FF0000uLL) |
           ((v >>  8) & 0x00000000FF000000uLL) |
           ((v <<  8) & 0x000000FF00000000uLL) |
           ((v << 24) & 0x0000FF0000000000uLL) |
           ((v << 40) & 0x00FF000000000000uLL) |
           ((v << 56) & 0xFF00000000000000uLL);
}

/* ---- public types ---- */

/* Fixed byte offsets in the frame (wire-order independent) */
#define ELIB_PROTOCOL_FRAME_HEAD     0x68u
#define ELIB_PROTOCOL_FRAME_TAIL     0x16u
#define ELIB_PROTOCOL_HDR_SIZE       9u    /* head + len2 + addr4 + seq + desc */
#define ELIB_PROTOCOL_FRAME_LEN_MIN  12u   /* empty data (count byte only) */
#define ELIB_PROTOCOL_DATA_MIN       1u    /* meta_count byte */

/* DESC bit fields */
#define ELIB_PROTOCOL_DESC_TYPE_MASK 0x06u
#define ELIB_PROTOCOL_DESC_TYPE_REQ   0x00u
#define ELIB_PROTOCOL_DESC_TYPE_RESP  0x02u
#define ELIB_PROTOCOL_DESC_TYPE_EVENT 0x04u
#define ELIB_PROTOCOL_DESC_FRAG_MASK 0x08u
#define ELIB_PROTOCOL_DESC_FRAG_YES   0x08u
#define ELIB_PROTOCOL_DESC_FRAG_NO    0x00u
#define ELIB_PROTOCOL_DESC_DIR_MASK  0x01u
#define ELIB_PROTOCOL_DESC_DIR_UP     0x01u
#define ELIB_PROTOCOL_DESC_DIR_DOWN   0x00u

/* built frame descriptor byte helpers */
#define ELIB_PROTOCOL_DESC_REQ_DOWN \
    (ELIB_PROTOCOL_DESC_TYPE_REQ | ELIB_PROTOCOL_DESC_DIR_DOWN)
#define ELIB_PROTOCOL_DESC_RESP_UP \
    (ELIB_PROTOCOL_DESC_TYPE_RESP | ELIB_PROTOCOL_DESC_DIR_UP)
#define ELIB_PROTOCOL_DESC_EVENT_UP \
    (ELIB_PROTOCOL_DESC_TYPE_EVENT | ELIB_PROTOCOL_DESC_DIR_UP)

static inline uint8_t elib_protocol_desc_get_type(uint8_t d) {
    return (uint8_t)(d & ELIB_PROTOCOL_DESC_TYPE_MASK);
}

static inline uint8_t elib_protocol_desc_get_frag(uint8_t d) {
    return (uint8_t)(d & ELIB_PROTOCOL_DESC_FRAG_MASK);
}

static inline uint8_t elib_protocol_desc_get_dir(uint8_t d) {
    return (uint8_t)(d & ELIB_PROTOCOL_DESC_DIR_MASK);
}

/* data types carried by a metadata entry (advisory metadata for the
 * ID-based application layer) */
typedef enum {
    ELIB_PROTOCOL_DATA_U8 = 0,
    ELIB_PROTOCOL_DATA_U16,
    ELIB_PROTOCOL_DATA_U32,
    ELIB_PROTOCOL_DATA_U64,
    ELIB_PROTOCOL_DATA_I8,
    ELIB_PROTOCOL_DATA_I16,
    ELIB_PROTOCOL_DATA_I32,
    ELIB_PROTOCOL_DATA_I64,
    ELIB_PROTOCOL_DATA_BOOL,
    ELIB_PROTOCOL_DATA_ARRAY
} elib_protocol_data_type_t;

/* One decoded metadata entry (values resolved through the accessors;
 * fdata stays a raw pointer into the reassembly buffer) */
typedef struct {
    uint16_t       id;
    uint8_t        type;
    uint16_t       len;
    const uint8_t *fdata;
} elib_protocol_meta_t;

/* Frame handed to the on_frame callback. data points at the DATA
 * region inside the user's reassembly buffer (starting at the
 * meta_count byte); the buffer is frozen until the callback returns. */
typedef struct {
    uint8_t        addr[4];
    uint8_t        seq;
    uint8_t        desc;
    const uint8_t *data;     /* [0] = meta_count, then meta entries */
    uint16_t       data_len; /* includes the meta_count byte */
} elib_protocol_frame_ctx_t;

/* user callbacks (user_data forwarded untouched - no globals needed) */
typedef struct {
    void *user_data;
    void (*on_frame)(void *ud, const elib_protocol_frame_ctx_t *frame);
} elib_protocol_ops_t;

/* user-provided static reassembly buffer (>= largest expected frame) */
typedef struct {
    uint8_t *frame_buf;
    uint16_t frame_buf_size;
} elib_protocol_bufs_t;

/* ---- contexts (user statically allocates; multiple instances fine) */

typedef struct {
    const elib_protocol_ops_t *ops;
    elib_protocol_bufs_t       bufs;
    const uint8_t             *self_addr;  /* NULL = accept any (host mode) */
    uint16_t                   claim;      /* validated byte quota of frame */
    uint16_t                   filled;     /* bytes stored in frame_buf */
    uint16_t                   swallow;    /* pending ignored byte count */
    uint16_t                   crc_run;    /* running CRC over stored bytes */
    uint16_t                   crc_rx;     /* CRC field being received */
    uint8_t                    state;
    struct {
        uint8_t initialized : 1;
        uint8_t busy        : 1; /* on_frame is in progress */
    } bit_flags;
} elib_protocol_engine_t;    /* common engine; layout shared by both roles */

typedef struct {
    elib_protocol_engine_t core;
} elib_protocol_host_ctx_t;

typedef struct {
    elib_protocol_engine_t core;
    uint8_t                self_addr[4]; /* stable storage for the address */
} elib_protocol_slave_ctx_t;

/* ---- slave config ---- */
typedef struct {
    const uint8_t *addr;   /* 4-byte self address, must not be NULL */
} elib_protocol_slave_cfg_t;

/* ---- host API ---- */

elib_protocol_err_t elib_protocol_host_init(elib_protocol_host_ctx_t *ctx,
                                            const elib_protocol_ops_t *ops,
                                            const elib_protocol_bufs_t *bufs);

/* Process a batch of received bytes; every byte is consumed. The
 * return value reports the drop reason of the LAST discarded frame in
 * this batch (ELIB_PROTOCOL_OK when every fed byte led to either a
 * good frame or an ordinary silent drop such as foreign addresses). */
elib_protocol_err_t elib_protocol_host_feed(elib_protocol_host_ctx_t *ctx,
                                            const uint8_t *bytes, uint16_t len);

/* Single-byte convenience (has exactly the one calling feed(...,1)) */
elib_protocol_err_t elib_protocol_host_feed_byte(elib_protocol_host_ctx_t *ctx,
                                                 uint8_t byte);

/* Return every parse state to the fresh "hunting for 0x68" condition.
 * The user's own stall detector decides when to call this. */
elib_protocol_err_t elib_protocol_host_reset(elib_protocol_host_ctx_t *ctx);

/* ---- slave API (same shape, plus address filtering) ---- */

elib_protocol_err_t elib_protocol_slave_init(elib_protocol_slave_ctx_t *ctx,
                                             const elib_protocol_ops_t *ops,
                                             const elib_protocol_bufs_t *bufs,
                                             const elib_protocol_slave_cfg_t *cfg);

elib_protocol_err_t elib_protocol_slave_feed(elib_protocol_slave_ctx_t *ctx,
                                             const uint8_t *bytes, uint16_t len);

elib_protocol_err_t elib_protocol_slave_feed_byte(elib_protocol_slave_ctx_t *ctx,
                                                  uint8_t byte);

elib_protocol_err_t elib_protocol_slave_reset(elib_protocol_slave_ctx_t *ctx);

/* ---- frame builders (three-step chaining) ----
 *
 * build_header writes bytes [0..9) (meta_count placeholder included)
 * and returns 10; build_meta appends one entry at the given write
 * position and returns the new one; build_pack fills in meta_count,
 * frame length, CRC and tail, and returns the full frame length. */

size_t elib_protocol_build_header(uint8_t *buf, size_t buf_size,
                                  const uint8_t *addr /* NULL = broadcast */,
                                  uint8_t seq, uint8_t desc);

size_t elib_protocol_build_meta(uint8_t *buf, size_t buf_size,
                                uint16_t id, uint8_t type,
                                const void *data /* NULL only with len 0 */,
                                uint16_t len);

/* meta scan must land exactly on "offset" with well-formed entries or
 * the frame is refused (returns 0) - no half-crafted frames out */
size_t elib_protocol_build_pack(uint8_t *buf, size_t buf_size, size_t offset);

/* ---- metadata iterator over the frame DATA region ----
 *
 * Initialize *pos to 0; each call decodes the next entry (values
 * through the byte-order accessors) and returns ELIB_PROTOCOL_OK.
 * ELIB_PROTOCOL_ERR_FRAME when the region is exhausted or off-spec. */
elib_protocol_err_t elib_protocol_get_next_meta(uint16_t *pos,
                                                const uint8_t *data,
                                                uint16_t len,
                                                elib_protocol_meta_t *meta);

#ifdef __cplusplus
}
#endif

#endif /* ELIB_PROTOCOL_H */
