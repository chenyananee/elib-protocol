/* test_elib_protocol.c - v2 roundtrip + robustness suite
 *
 * Compile-only on this host toolchain (-Werror, no execution); every
 * case is verified by static timeline tracing. Strategy: craft real
 * frames through the public builders, then feed them back through
 * host/slave feeders and assert on the captured on_frame contents and
 * every documented drop path. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "elib_protocol.h"

#define RE_BUF_SIZE 32u   /* reassembly buffer: fits ~30-byte frames */

static uint8_t host_buf[RE_BUF_SIZE];
static uint8_t slave_buf[RE_BUF_SIZE];
static uint8_t tx[64];

/* ---- capture plumbing ---- */

static uint8_t cap_data[64];
static elib_protocol_frame_ctx_t cap;
static uint32_t cap_count;

static void capture_cb(void *ud, const elib_protocol_frame_ctx_t *frame) {
    (void)ud;
    (void)memcpy(cap.addr, frame->addr, 4);
    cap.seq = frame->seq;
    cap.desc = frame->desc;
    cap.data_len = frame->data_len;
    if (frame->data_len != 0u) {
        (void)memcpy(cap_data, frame->data, frame->data_len);
    }
    cap.data = cap_data; /* localize: valid after the callback returns */
    cap_count++;
}

static const elib_protocol_ops_t cap_ops = { NULL, capture_cb, NULL };

/* ---- custom checksum hook ---- */

static struct {
    size_t  saw_len;
    uint8_t saw_first;
    uint8_t saw_last;
} crc_probe;

/* Deliberately different from the built-in algorithm: bit-inverted
 * CRC16. Doubles as a geometry probe - it records the exact slice the
 * engine hands over (which must be [0, frame_len-3) = head .. data
 * end, straight out of the reassembly buffer). */
static uint16_t inverted_crc16(void *ud, const uint8_t *data, size_t len) {
    (void)ud;
    crc_probe.saw_len = len;
    crc_probe.saw_first = data[0];
    crc_probe.saw_last = data[len - 1u];
    return (uint16_t)~elib_protocol_crc16(data, len);
}

static const elib_protocol_ops_t crc_ops = { NULL, capture_cb, inverted_crc16 };

static elib_protocol_host_ctx_t host;
static elib_protocol_slave_ctx_t slave;

static const uint8_t addr_own[4]   = { 0x00u, 0x00u, 0x00u, 0x01u };
static const uint8_t addr_other[4] = { 0x00u, 0x00u, 0x00u, 0x02u };
static const uint8_t addr_bcast[4] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };

static const uint8_t payload3[3] = { 0xA1u, 0xB2u, 0xC3u };
#define PAYLOAD_VALUE 0x11223344u

/* craft one frame: header + [0x0001: u32 value] + [0x0002: array3] */
static uint16_t build_frame(uint8_t *out, size_t out_size,
                            const uint8_t *addr, uint8_t seq, uint8_t desc) {
    uint8_t tmp[4];
    size_t off;
    elib_protocol_put_u32(tmp, PAYLOAD_VALUE);

    off = elib_protocol_build_header(out, out_size, addr, seq, desc);
    off += elib_protocol_build_meta(out + off, out_size - off, 0x0001u,
                                    ELIB_PROTOCOL_DATA_U32, tmp, 4u);
    off += elib_protocol_build_meta(out + off, out_size - off, 0x0002u,
                                    ELIB_PROTOCOL_DATA_ARRAY, payload3, 3u);
    return (uint16_t)elib_protocol_build_pack(out, out_size, off);
}

static void capture_reset(void) {
    (void)memset(&cap, 0, sizeof(cap));
    (void)memset(cap_data, 0, sizeof(cap_data));
    cap_count = 0;
}

static void ctx_reset_all(void) {
    capture_reset();
    (void)memset(host_buf, 0, sizeof(host_buf));
    (void)memset(slave_buf, 0, sizeof(slave_buf));
}

/* iterate the captured DATA region and assert every field */
static void assert_captured_frame(uint8_t seq, const uint8_t *addr) {
    uint16_t pos = 0;
    elib_protocol_meta_t meta;

    assert(cap_count == 1u);
    assert(cap.seq == seq);
    assert(memcmp(cap.addr, addr, 4) == 0);
    assert(elib_protocol_get_u8(cap.data) == 2u); /* meta_count */

    assert(elib_protocol_get_next_meta(&pos, cap.data, cap.data_len, &meta)
           == ELIB_PROTOCOL_OK);
    assert(meta.id == 0x0001u && meta.type == ELIB_PROTOCOL_DATA_U32);
    assert(meta.len == 4u && meta.fdata != NULL);
    assert(elib_protocol_get_u32(meta.fdata) == PAYLOAD_VALUE);

    assert(elib_protocol_get_next_meta(&pos, cap.data, cap.data_len, &meta)
           == ELIB_PROTOCOL_OK);
    assert(meta.id == 0x0002u && meta.type == ELIB_PROTOCOL_DATA_ARRAY);
    assert(meta.len == 3u);
    assert(memcmp(meta.fdata, payload3, 3) == 0);

    assert(elib_protocol_get_next_meta(&pos, cap.data, cap.data_len, &meta)
           == ELIB_PROTOCOL_ERR_FRAME);
}

/* ---- tests ---- */

static void test_init_matrix(void) {
    printf("Test: init rejects bad configurations... ");

    ctx_reset_all();

    assert(elib_protocol_host_init(&host, &cap_ops, NULL)
           == ELIB_PROTOCOL_ERR_INVALID_PARAM);
    {
        elib_protocol_ops_t no_cb = cap_ops;
        elib_protocol_bufs_t small = { host_buf, 11u }; /* < 12 */
        no_cb.on_frame = NULL;
        assert(elib_protocol_host_init(&host, &no_cb, NULL)
               == ELIB_PROTOCOL_ERR_INVALID_PARAM);
        assert(elib_protocol_host_init(&host, &cap_ops, &small)
               == ELIB_PROTOCOL_ERR_INVALID_PARAM);
        assert(elib_protocol_host_init(NULL, &cap_ops, NULL)
               == ELIB_PROTOCOL_ERR_INVALID_PARAM);
    }
    assert(elib_protocol_host_init(&host, &cap_ops, NULL)
           == ELIB_PROTOCOL_ERR_INVALID_PARAM);
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);

        /* slave needs a config with a concrete address */
        assert(elib_protocol_slave_init(&slave, &cap_ops, &good, NULL)
               == ELIB_PROTOCOL_ERR_INVALID_PARAM);
        {
            elib_protocol_slave_cfg_t no_addr = { NULL };
            assert(elib_protocol_slave_init(&slave, &cap_ops, &good, &no_addr)
                   == ELIB_PROTOCOL_ERR_INVALID_PARAM);
        }
        {
            elib_protocol_slave_cfg_t cfg = { addr_own };
            assert(elib_protocol_slave_init(&slave, &cap_ops, &good, &cfg)
                   == ELIB_PROTOCOL_OK);
        }
    }

    printf("PASSED\n");
}

static void test_endian_helpers(void) {
    uint8_t s[8];

    printf("Test: typed accessors roundtrip in the wire order... ");

    elib_protocol_put_u16(s, 0xBEEFu);
    assert(elib_protocol_get_u16(s) == 0xBEEFu);
    elib_protocol_put_u32(s, 0xDEADBEEFu);
    assert(elib_protocol_get_u32(s) == 0xDEADBEEFu);
    elib_protocol_put_u64(s, 0x0102030405060708uLL);
    assert(elib_protocol_get_u64(s) == 0x0102030405060708uLL);
    elib_protocol_put_i16(s, -2);
    assert(elib_protocol_get_i16(s) == -2);
    elib_protocol_put_i32(s, -1234567);
    assert(elib_protocol_get_i32(s) == -1234567);
    elib_protocol_put_i64(s, -9000000000LL);
    assert(elib_protocol_get_i64(s) == -9000000000LL);
    elib_protocol_put_u8(s, 0x42u);
    assert(elib_protocol_get_u8(s) == 0x42u);
    elib_protocol_put_bool(s, 1u);
    assert(elib_protocol_get_bool(s) == 1u);
    elib_protocol_put_bool(s, 0u);
    assert(elib_protocol_get_bool(s) == 0u);
    assert(elib_protocol_swap16(0x1234u) == 0x3412u);

    printf("PASSED\n");
}

static void test_roundtrip_batch_and_bytes(void) {
    uint16_t frame_len;
    uint16_t i;

    printf("Test: builder->feeder roundtrip, batch and one-by-one... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);
    }

    /* batch feed of a complete frame */
    frame_len = build_frame(tx, sizeof(tx), addr_own, 7u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);
    assert(frame_len >= ELIB_PROTOCOL_FRAME_LEN_MIN);
    assert(elib_protocol_build_pack(tx, sizeof(tx), 0) == 0u); /* rebuild refused */
    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert_captured_frame(7u, addr_own); /* implies cap_count == 1 */

    /* identical result byte by byte */
    capture_reset();
    for (i = 0; i < frame_len; i++) {
        assert(elib_protocol_host_feed_byte(&host, tx[i]) == ELIB_PROTOCOL_OK);
    }
    assert_captured_frame(7u, addr_own); /* implies cap_count == 1 */

    /* two back-to-back frames in ONE batch */
    capture_reset();
    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert(cap_count == 2u);
    capture_reset();
    {
        uint8_t pair[2u * 64u];
        (void)memcpy(pair, tx, frame_len);
        (void)memcpy(pair + frame_len, tx, frame_len);
        assert(elib_protocol_host_feed(&host, pair, (uint16_t)(2u * frame_len))
               == ELIB_PROTOCOL_OK);
    }
    assert(cap_count == 2u);

    printf("PASSED\n");
}

static void test_bogus_then_valid_claim(void) {
    uint16_t frame_len;
    uint8_t stream[3u + 64u];
    uint16_t n;

    printf("Test: 68-len(0) 68 -> drops 3 bytes, lands on the second 68... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);
    }

    frame_len = build_frame(tx, sizeof(tx), addr_own, 8u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);
    stream[0] = (uint8_t)ELIB_PROTOCOL_FRAME_HEAD;
    stream[1] = 0x00u; /* LEN = 0x0000 < 12: bogus claim */
    stream[2] = 0x00u;
    (void)memcpy(&stream[3], tx, frame_len);
    n = (uint16_t)(3u + frame_len);

    /* the blink-and-you-miss-it bogus claim surfaces as the batch's
     * LAST drop, the real frame still lands and delivers */
    assert(elib_protocol_host_feed(&host, stream, n)
           == ELIB_PROTOCOL_ERR_FRAME);
    assert_captured_frame(8u, addr_own);

    printf("PASSED\n");
}

static void test_oversized_claim_swallowed(void) {
    uint16_t frame_len;
    uint8_t stream[96]; /* 3 head+claim + 37 spam + full frame */
    uint16_t n;

    printf("Test: oversized claim swallows its quota, then resyncs... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);
    }

    frame_len = build_frame(tx, sizeof(tx), addr_own, 9u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);

    /* claim 40 bytes (> RE_BUF_SIZE = 32), then 37 spam bytes, then a
     * genuine frame */
    stream[0] = (uint8_t)ELIB_PROTOCOL_FRAME_HEAD;
    stream[1] = 0x00u;
    stream[2] = 40u;
    (void)memset(&stream[3], 0x55u, 37u);
    (void)memcpy(&stream[40], tx, frame_len);
    n = (uint16_t)(40u + frame_len);

    assert(elib_protocol_host_feed(&host, stream, n) == ELIB_PROTOCOL_ERR_FRAME);
    assert_captured_frame(9u, addr_own); /* quota never touched the buffer */

    printf("PASSED\n");
}

static void test_crc_and_tail_rejects(void) {
    uint16_t frame_len;
    uint16_t i;

    printf("Test: CRC and tail corruption reject without delivery... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);
    }
    frame_len = build_frame(tx, sizeof(tx), addr_own, 10u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);

    /* corrupt one DATA byte: frame runs to completion, CRC must trip */
    tx[12] ^= 0xFFu; /* [12] is inside the meta data region */
    assert(elib_protocol_host_feed(&host, tx, frame_len)
           == ELIB_PROTOCOL_ERR_CHECKSUM);
    assert(cap_count == 0u);

    /* restore and confirm delivery resumes */
    tx[12] ^= 0xFFu;
    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert_captured_frame(10u, addr_own);

    /* corrupt the tail: malformed frame, no delivery */
    capture_reset();
    i = frame_len - 1u;
    tx[i] = 0x99u;
    assert(elib_protocol_host_feed(&host, tx, frame_len)
           == ELIB_PROTOCOL_ERR_FRAME);
    assert(cap_count == 0u);
    tx[i] = (uint8_t)ELIB_PROTOCOL_FRAME_TAIL;

    printf("PASSED\n");
}

static void test_custom_checksum(void) {
    uint16_t frame_len;
    size_t off;
    uint8_t tmp[4];

    printf("Test: custom checksum hook (roundtrip, probe, mismatch)... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &crc_ops, &good)
               == ELIB_PROTOCOL_OK);
    }

    /* same body as build_frame, but sealed with the custom function */
    elib_protocol_put_u32(tmp, PAYLOAD_VALUE);
    off = elib_protocol_build_header(tx, sizeof(tx), addr_own, 17u,
                                     ELIB_PROTOCOL_DESC_REQ_DOWN);
    off += elib_protocol_build_meta(tx + off, sizeof(tx) - off, 0x0001u,
                                    ELIB_PROTOCOL_DATA_U32, tmp, 4u);
    off += elib_protocol_build_meta(tx + off, sizeof(tx) - off, 0x0002u,
                                    ELIB_PROTOCOL_DATA_ARRAY, payload3, 3u);
    frame_len = (uint16_t)elib_protocol_build_pack_crc(tx, sizeof(tx), off,
                                                       inverted_crc16, NULL);

    /* matching algorithms on both ends: delivered, and the hook saw
     * the exact coverage slice head..data-end */
    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert_captured_frame(17u, addr_own);
    assert(crc_probe.saw_len == (size_t)frame_len - 3u);
    assert(crc_probe.saw_first == (uint8_t)ELIB_PROTOCOL_FRAME_HEAD);
    assert(crc_probe.saw_last == tx[frame_len - 4u]);

    /* same octets sealed with the built-in algorithm instead: the two
     * ends disagree, the frame must die with CHECKSUM */
    capture_reset();
    frame_len = (uint16_t)elib_protocol_build_pack(tx, sizeof(tx), off);
    assert(elib_protocol_host_feed(&host, tx, frame_len)
           == ELIB_PROTOCOL_ERR_CHECKSUM);
    assert(cap_count == 0u);

    printf("PASSED\n");
}

static void test_slave_address_filter(void) {
    uint16_t frame_len;
    elib_protocol_bufs_t good = { slave_buf, (uint16_t)sizeof(slave_buf) };
    elib_protocol_slave_cfg_t cfg = { addr_own };

    printf("Test: slave filters foreign addresses (broadcast passes)... ");

    ctx_reset_all();
    assert(elib_protocol_slave_init(&slave, &cap_ops, &good, &cfg)
           == ELIB_PROTOCOL_OK);

    /* addressed elsewhere: silently swallowed, nothing delivered */
    frame_len = build_frame(tx, sizeof(tx), addr_other, 11u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);
    assert(elib_protocol_slave_feed(&slave, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert(cap_count == 0u);

    /* broadcast: delivered */
    frame_len = build_frame(tx, sizeof(tx), addr_bcast, 12u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);
    assert(elib_protocol_slave_feed(&slave, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert_captured_frame(12u, addr_bcast);

    /* own address: delivered */
    capture_reset();
    frame_len = build_frame(tx, sizeof(tx), addr_own, 13u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);
    assert(elib_protocol_slave_feed(&slave, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert_captured_frame(13u, addr_own);

    printf("PASSED\n");
}

static void test_head_inside_payload_survives(void) {
    uint16_t frame_len;

    printf("Test: 0x68 inside fdata is plain data, never a resync... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);
    }

    /* custom frame: array meta carrying 0x68 and 0x16 inside */
    {
        uint8_t weird[4];
        size_t off;
        uint8_t dirty[3] = { (uint8_t)ELIB_PROTOCOL_FRAME_HEAD,
                             (uint8_t)ELIB_PROTOCOL_FRAME_TAIL, 0x77u };
        off = elib_protocol_build_header(tx, sizeof(tx), addr_own, 14u,
                                         ELIB_PROTOCOL_DESC_REQ_DOWN);
        elib_protocol_put_u32(weird, PAYLOAD_VALUE);
        off += elib_protocol_build_meta(tx + off, sizeof(tx) - off, 0x0005u,
                                        ELIB_PROTOCOL_DATA_U32, weird, 4u);
        off += elib_protocol_build_meta(tx + off, sizeof(tx) - off, 0x0006u,
                                        ELIB_PROTOCOL_DATA_ARRAY, dirty, 3u);
        frame_len = (uint16_t)elib_protocol_build_pack(tx, sizeof(tx), off);
    }

    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert(cap_count == 1u);
    {
        uint16_t pos = 0;
        elib_protocol_meta_t meta;
        assert(elib_protocol_get_next_meta(&pos, cap.data, cap.data_len, &meta)
               == ELIB_PROTOCOL_OK); /* 0x0005 u32 */
        assert(elib_protocol_get_next_meta(&pos, cap.data, cap.data_len, &meta)
               == ELIB_PROTOCOL_OK); /* 0x0006 array with dirty bytes */
        assert(meta.len == 3u);
        assert(meta.fdata[0] == (uint8_t)ELIB_PROTOCOL_FRAME_HEAD);
        assert(meta.fdata[1] == (uint8_t)ELIB_PROTOCOL_FRAME_TAIL);
        assert(meta.fdata[2] == 0x77u);
    }

    printf("PASSED\n");
}

static void test_reset_mid_frame(void) {
    uint16_t frame_len;
    uint16_t i;

    printf("Test: user-invoked reset clears a stuck half-frame... ");

    ctx_reset_all();
    {
        elib_protocol_bufs_t good = { host_buf, (uint16_t)sizeof(host_buf) };
        assert(elib_protocol_host_init(&host, &cap_ops, &good) == ELIB_PROTOCOL_OK);
    }
    frame_len = build_frame(tx, sizeof(tx), addr_own, 15u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);

    /* half the frame: parser parked mid-frame, nothing delivered */
    for (i = 0; i < (uint16_t)(frame_len / 2u); i++) {
        assert(elib_protocol_host_feed_byte(&host, tx[i]) == ELIB_PROTOCOL_OK);
    }
    assert(cap_count == 0u);

    /* the stall detector (user's own policy) fires: clear everything */
    assert(elib_protocol_host_reset(&host) == ELIB_PROTOCOL_OK);

    /* a fresh frame starts clean on the very next feed */
    assert(elib_protocol_host_feed_byte(&host, tx[0]) == ELIB_PROTOCOL_OK);
    assert(elib_protocol_host_feed(&host, tx + 1u, (uint16_t)(frame_len - 1u))
           == ELIB_PROTOCOL_OK);
    assert_captured_frame(15u, addr_own);

    printf("PASSED\n");
}

static elib_protocol_err_t reent_err = ELIB_PROTOCOL_OK;
static uint32_t reent_delivered;

static void reent_cb(void *ud, const elib_protocol_frame_ctx_t *frame) {
    (void)ud;
    (void)frame;
    reent_delivered = 1u;
    /* re-enters its own host context while the busy gate is held */
    reent_err = elib_protocol_host_feed_byte(&host, 0x00u);
}

static void test_reentrancy_guard(void) {
    uint16_t frame_len;
    static uint8_t reent_buf[RE_BUF_SIZE];
    static elib_protocol_bufs_t good = { reent_buf, RE_BUF_SIZE };
    static elib_protocol_ops_t probe_ops = { NULL, reent_cb, NULL };

    printf("Test: on_frame reentering feed is refused with BUSY... ");

    ctx_reset_all();
    assert(elib_protocol_host_init(&host, &probe_ops, &good) == ELIB_PROTOCOL_OK);

    frame_len = build_frame(tx, sizeof(tx), addr_own, 16u,
                            ELIB_PROTOCOL_DESC_REQ_DOWN);
    reent_err = ELIB_PROTOCOL_OK;
    reent_delivered = 0u;
    assert(elib_protocol_host_feed(&host, tx, frame_len) == ELIB_PROTOCOL_OK);
    assert(reent_err == ELIB_PROTOCOL_ERR_BUSY);
    assert(reent_delivered == 1u); /* the probe frame was delivered */

    printf("PASSED\n");
}

static void test_reentry_and_guards(void) {
    printf("Test: reentrancy and argument guards... ");

    ctx_reset_all();
    assert(elib_protocol_host_init(&host, &cap_ops, NULL)
           == ELIB_PROTOCOL_ERR_INVALID_PARAM);

    assert(elib_protocol_get_next_meta(NULL, cap_data, 0, NULL)
           == ELIB_PROTOCOL_ERR_INVALID_PARAM);
    assert(elib_protocol_build_header(NULL, 64u, addr_own, 0u, 0u) == 0u);
    assert(elib_protocol_build_meta(NULL, 64u, 1u, 0u, payload3, 3u) == 0u);
    {   /* NULL data with a non-zero length is refused */
        uint8_t scratch[16];
        assert(elib_protocol_build_meta(scratch, 16u, 1u, 9u, NULL, 4u) == 0u);
    }
    {   /* pack refuses a data region that does not tile exactly */
        uint8_t scratch[64];
        size_t off = elib_protocol_build_header(scratch, 64u, addr_own, 1u, 0u);
        assert(elib_protocol_build_pack(scratch, 64u, off + 4u) == 0u);
    }

    {   /* pack_crc refuses a NULL checksum function outright */
        uint8_t scratch[64];
        size_t off = elib_protocol_build_header(scratch, 64u, addr_own, 1u, 0u);
        assert(elib_protocol_build_pack_crc(scratch, 64u, off, NULL, NULL) == 0u);
    }

    printf("PASSED\n");
}

int main(void) {
    test_init_matrix();
    test_endian_helpers();
    test_roundtrip_batch_and_bytes();
    test_bogus_then_valid_claim();
    test_oversized_claim_swallowed();
    test_crc_and_tail_rejects();
    test_custom_checksum();
    test_slave_address_filter();
    test_head_inside_payload_survives();
    test_reset_mid_frame();
    test_reentrancy_guard();
    test_reentry_and_guards();

    printf("All tests PASSED\n");
    return 0;
}
