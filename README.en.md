[简体中文](README.md) | **English**

# elib-protocol

A lightweight master/slave bus protocol — C99, zero dynamic memory, zero global state, zero awareness of time.

Master and slave are two genuinely separate roles (each with its own ctx and API) sharing a single streaming parser engine. Bytes are fed in as a stream and complete frames surface through a callback. It suits any byte-stream physical layer such as UART / RS485 / RS232 and **ships no driver at all** — your own BSP handles the transceiver while this library handles the protocol itself.

## Features

- **Strict fixed-length prefix frame format**: the full frame length is located at a glance and the receiver never parses invalid bytes
- **Streaming receive**: every byte is fed exactly once — no rescan, no look-back, no consumed ledger
- **Pluggable checksum**: a custom 16-bit checksum function is called once for the whole span (a hardware CRC peripheral wired straight through); when ops.crc is not supplied, the built-in software CRC16/CCITT-FALSE is used
- **Four-fold anti-clog protection**: under any dirty byte stream the receiver converges in bounded time and never locks up
- **True multiple instances**: all state lives in the ctx, callbacks carry `user_data`, and there is not a single global variable in the library
- **Configurable endianness**: wire byte order is big-endian by default library-wide; one `-D` macro flips everything to little-endian
- **A full set of typed read/write helpers**: u8/u16/u32/u64/i8/i16/i32/i64/bool, implemented with byte-by-byte shifts, independent of machine endianness and free of alignment traps
- **Idle detection left to the user**: the library has zero awareness of time and `reset()` may be called at any moment
- **Zero dynamic memory**: the ctx and the reassembly buffer are both statically allocated by the user

## Frame Format

```
0x68 | LEN_H LEN_L | ADDR0 ADDR1 ADDR2 ADDR3 | SEQ | DESC | DATA ... | CRC_H CRC_L | 0x16
```

| Field | Size | Description |
|------|------|------|
| HEAD | 1 | Fixed `0x68` |
| LEN  | 2 | **Total frame byte count** (including the 12 B head/tail overhead), minimum 12, in the configured endianness |
| ADDR | 4 | Target station address, in the configured endianness; `0xFFFFFFFF` = broadcast |
| SEQ  | 1 | Sequence number, opaque on the wire (request-response correlation is up to the application) |
| DESC | 1 | Descriptor, bit fields below |
| DATA | ≥1 | Metadata area: `meta_count` + N × entries (see below) |
| CRC  | 2 | CRC16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) **as the default algorithm, a custom one may be plugged in** (see "Custom Checksum"), covering from the head through the end of data (`[0, LEN-3)`), excluding the CRC field and the tail, in the configured endianness |
| TAIL | 1 | Fixed `0x16`, **no escaping** (see "Robustness Guarantees") |

**DESC bit fields**:

```
bit0      : DIR   0=downlink (master -> slave) 1=uplink (slave -> master)
bit1..2   : TYPE  0x00=REQ  0x02=RESP  0x04=EVENT
bit3      : FRAG  0x00=whole frame 0x08=fragment
other bits: reserved, set to 0
```

Ready-made combined macros: `ELIB_PROTOCOL_DESC_REQ_DOWN` / `DESC_RESP_UP` / `DESC_EVENT_UP`.

**DATA area (metadata list)**:

```
meta_count(1) + N × [ ID(2) | TYPE(1) | LEN(2) | fdata(LEN) ]
```

- `meta_count`: number of entries (1 byte, ≤255)
- `ID`: application-defined key, in the configured endianness
- `TYPE`: advisory payload type (`elib_protocol_data_type_t`; ARRAY `0x09` is a raw byte string)
- `LEN`: fdata byte count, in the configured endianness
- `fdata`: raw payload bytes; every numeric parse goes through the endianness helpers

Every numeric field inside a frame (LEN/ADDR/meta ID/meta LEN/CRC/numeric fdata) is interpreted in the **configured endianness** and is read and written exclusively through the helpers.

## Endianness Configuration

```c
/* Default: ELIB_PROTOCOL_ORDER_BIG -- the whole library decodes wire bytes as big-endian */
/* Flip it: add -DELIB_PROTOCOL_ORDER_LITTLE at build time */
```

- Both ends of the bus must agree: either both keep the default or both are built with the `-D` flip
- The header detects the machine endianness with `__BYTE_ORDER__` and exports `ELIB_PROTOCOL_MACHINE_BIG/LITTLE` (defaulting to big-endian when detection fails, overridable with `-D`); this is for your own code to inspect itself only
- **The helpers do not depend on machine endianness**: the `get/put` family shifts byte by byte in the configured endianness, so results are identical on any platform and there is inherently no alignment problem (a pointer may target any odd or even offset)

Helper list: `elib_protocol_get/put_u8 u16 u32 u64 i8 i16 i32 i64 bool` (bool is 1 byte, 0 or 1), plus the `swap16/32/64` pure-value swappers.

## Quick Start

### 1. Declare and Initialize (Zero Dynamic Memory)

```c
#include "elib_protocol.h"

#define FRAME_MAX 64u

static uint8_t rx_buf[FRAME_MAX];                    /* frame reassembly buffer */

static void on_frame(void *ud, const elib_protocol_frame_ctx_t *f) {
    (void)ud;
    uint16_t pos = 0;
    elib_protocol_meta_t m;

    while (elib_protocol_get_next_meta(&pos, f->data, f->data_len, &m)
           == ELIB_PROTOCOL_OK) {
        switch (m.id) {
        case 0x0001:  /* u32 temperature */
            if (m.type == ELIB_PROTOCOL_DATA_U32 && m.len == 4u) {
                uint32_t temp = elib_protocol_get_u32(m.fdata);
                /* ... */
            }
            break;
        default:
            break;
        }
    }
    /* f->data points into the reassembly buffer and dies when the callback returns;
     * copy it yourself if you need to keep it around */
}

/* ---- Slave ---- */
static const uint8_t SELF_ADDR[4] = { 0x00, 0x00, 0x00, 0x01 };

static elib_protocol_slave_ctx_t slave;

void slave_setup(void) {
    static const elib_protocol_ops_t ops = { NULL, on_frame, NULL };
    static const elib_protocol_bufs_t bufs = { rx_buf, FRAME_MAX };
    static const elib_protocol_slave_cfg_t cfg = { SELF_ADDR };

    (void)elib_protocol_slave_init(&slave, &ops, &bufs, &cfg);
}
```

```c
/* ---- Master (no self address: every uplink frame is accepted) ---- */
static elib_protocol_host_ctx_t host;

void host_setup(void) {
    static const elib_protocol_ops_t ops = { NULL, on_frame, NULL };
    static const elib_protocol_bufs_t bufs = { rx_buf, FRAME_MAX };

    (void)elib_protocol_host_init(&host, &ops, &bufs);
}
```

### 2. Receive: Feed Incoming Bytes (Driver-Agnostic)

```c
/* n bytes obtained in the UART RX interrupt / DMA / polling path */
void uart_on_rx(const uint8_t *bytes, uint16_t n) {
    elib_protocol_slave_feed(&slave, bytes, n);
    /* return value = the drop reason of the last dropped frame in this batch (OK if none);
     * every byte fed in is always consumed */
}

/* or byte by byte (the same internal path as the batch call) */
void uart_on_rx_byte(uint8_t b) {
    (void)elib_protocol_slave_feed_byte(&slave, b);
}
```

### 3. Send: Three-Stage Framing

```c
uint8_t tx[64];
size_t  off;
uint8_t temp_buf[4];

elib_protocol_put_u32(temp_buf, 2580u);   /* values crossing the wire must go through the helpers */

off = elib_protocol_build_header(tx, sizeof(tx), SELF_ADDR /* NULL=broadcast */,
                                 seq++, ELIB_PROTOCOL_DESC_REQ_DOWN);
off += elib_protocol_build_meta(tx + off, sizeof(tx) - off,
                                0x0001u, ELIB_PROTOCOL_DATA_U32,
                                temp_buf, 4u);
off += elib_protocol_build_meta(tx + off, sizeof(tx) - off,
                                0x0002u, ELIB_PROTOCOL_DATA_ARRAY, NULL, 0u);
{
    size_t frame_len = elib_protocol_build_pack(tx, sizeof(tx), off);
    if (frame_len != 0u) {
        uart_send(tx, (uint16_t)frame_len);   /* your physical layer */
    }
}
```

The three stages:

- `build_header` writes 10 bytes (head + LEN placeholder + address + SEQ + DESC + meta_count placeholder) and returns 10
- `build_meta` appends one metadata entry (ID/TYPE/LEN/payload) at the write position and returns the new write position; `data` may be NULL only when `len==0`
- `build_pack` rescans the metadata area to fill in meta_count / total frame length / CRC / tail; **the metadata area must be exactly tiled by entries** (each 5+len with no remainder), otherwise it returns 0 and refuses to seal the frame — a bad frame never leaves the factory

### 4. Idle Detection (Entirely Up to the User)

The library does not manage time. Your stream-loss strategy (idle timeout, watchdog, link state, ...) triggers it however it likes:

```c
/* callable at any moment: clears all parse state and returns to the "hunt for the head" state */
(void)elib_protocol_slave_reset(&slave);
```

Recommended pattern: track "how long since any byte was last received" and call `reset()` once it exceeds a threshold — the partial frame, the drop quota and the swallow counters are cleared together and the next frame starts clean.

## Custom Checksum (Hardware CRC Peripheral)

The built-in software CRC16/CCITT-FALSE is used by default. If your MCU has a hardware CRC peripheral, supply a custom function in `ops.crc` and the whole span can be passed to the peripheral in a single call.

A custom checksum function has exactly one shape, and both the sending and receiving sides must honour it:

```c
typedef uint16_t (*elib_protocol_crc_fn)(void *ud, const uint8_t *data,
                                         size_t len);
```

Contract:

- **Call timing and slicing**: once the CRC field of each candidate frame is complete, the library calls the covering region `buf[0..LEN-3)` of the reassembly buffer (frame head..end of data) **once for the whole span** — the frame is necessarily complete inside the buffer by then (LEN was already checked against capacity back in the LEN_L state), so a hardware peripheral never has to worry about a seed carried across bytes
- `ud` is `ops.user_data` passed straight through (for multiple CRC units or a peripheral handle)
- It must return a 16-bit value; the library compares it against the on-wire CRC field in the configured endianness, and the algorithm defines its own init value and polynomial
- On the framing side, seal the frame with `elib_protocol_build_pack_crc(buf, size, offset, crc, ud)` (the original `build_pack` always uses the built-in algorithm, its signature is unchanged and it stays backward compatible)
- The built-in software algorithm is the public `elib_protocol_crc16(data, len)`, which your custom implementation may reuse directly (for example to build a bit-reversed variant or a probe that pins down peripheral differences)

Taking the STM32 hardware CRC as an example (pseudo-code sketch):

```c
static uint16_t hw_crc16(void *ud, const uint8_t *data, size_t len) {
    CRC_HandleTypeDef *hcrc = ud;   /* matches ops.user_data */

    __HAL_CRC_DR_RESET(hcrc);      /* peripheral reset (includes setting the init value) */
    for (size_t i = 0; i < len; i++) {
        (void)HAL_CRC_Accumulate(hcrc, (uint32_t *)&data[i], 1); /* or one whole span via DMA */
    }
    return (uint16_t)hcrc->Instance->DR;
}

static const elib_protocol_ops_t ops = { &hcrc, on_frame, hw_crc16 };
```

**In red: both ends of the bus must use the same algorithm.** If the receiving side has a custom algorithm configured, the sending side must use `build_pack_crc` with that same function, otherwise every frame gets cut down by the other end as `CHECKSUM`. Forgetting to switch the variant on the sending side is the one high-frequency pitfall of this feature. Multiple instances each carry their own peripheral handle (`user_data`) and do not interfere.

## Error Codes

| Error code | Meaning |
|--------|------|
| `OK` | Success |
| `INVALID_PARAM` | Illegal init/feed argument (NULL; batch feed with bytes==NULL and len!=0) |
| `NOT_INITIALIZED` | Called without init |
| `BUFFER_TOO_SMALL` | Framing: output buffer too small (the builder returned 0) |
| `CHECKSUM` | Receive: CRC mismatch, the whole frame is dropped |
| `FRAME` | Receive: bad frame (wrong tail); framing: metadata rescan non-conforming, sealing refused |
| `BUSY` | feed/reset re-entered while the on_frame callback is running |

The batch `feed` return value = **the drop reason of the last dropped frame in this batch**; a frame filtered out by address is normal multi-drop behaviour and is not counted as a drop. Fed bytes are consumed in full no matter what.

## Receive Pipeline

```
IDLE      : wait for 0x68 (garbage bytes are discarded outright and never enter the buffer; a hit -> store the head)
LEN_H/L   : as soon as the two LEN bytes are in, they are checked:
            |- < 12 (fake LEN)  -> discard these 3 bytes, back to IDLE, wait for the nearest 0x68 after the head
            |- > buffer (oversized) -> SWALLOW the rest of the quota, back to IDLE
            `- legal            -> keep assembling, a buffer overflow is structurally impossible
ADDR0..3  : store 4 bytes; the slave filters as soon as they are complete (only local/broadcast pass, everything
            else silently swallows the rest of the quota; the master has no self address -> accepts all)
SEQ/DESC  : store; after DESC, if the data is empty (LEN==12) -> go straight to CRC
DATA      : store bytes; 0x68 in the data area is ordinary payload and is never re-searched
CRC_H/L   : once the CRC field is stored, call the checksum function (custom ops.crc or the built-in software one)
            once for the whole span buf[0..LEN-3) (head..end of data)
            -> yields the expected value, compared at TAIL with the on-wire field
TAIL      : != 0x16 ->FRAME drop; CRC mismatch ->CHECKSUM drop;
            both pass -> on_frame(user_data, frame) -> reset to IDLE for the next frame
SWALLOW   : count down to 0 then back to IDLE; bytes in the meantime are never interpreted
```

## Robustness Guarantees

**Four-fold anti-clog protection:**

1. **A buffer overflow is structurally impossible** — the buffer only holds frame segments "certified by LEN", and LEN is checked before storing
2. **Dirty data converges by quota** — every drop discards a known quota: a fake LEN drops 3 bytes and turns back immediately, a real drop / address filter swallows its whole remaining quota and returns strictly to IDLE; after the quota expires it resynchronises only on a **real** 0x68
3. **Idle detection belongs to the user** — `reset()` is callable at any time and clears every intermediate state
4. **Callback reentrancy gate** — any feed/reset during on_frame returns `BUSY`

**The bounded cost of misalignment collisions** (when a continuous stream loses a byte and slips out of sync): each collision costs at most "the rest of the old frame's quota + 3 bytes of a false head"; CRC always cuts down any mis-assembly and IDLE resynchronises strictly, so it **never locks up**.

**Why no escaping**: the fixed-length prefix format plus LEN locates a frame at a glance, so correctness needs no escaping; escaping/COBS buys a paired cost (bandwidth, implementation complexity) for zero gain. A payload `fdata` containing `0x68` is legal data, and the receiver never re-searches for a frame head in the DATA state.

## Multiple Instances

All state (parser state machine, CRC, buffer cursor, swallow quota, reentrancy gate) lives in the ctx; callbacks are injected verbatim into the caller's context through `ops.user_data`. Several UARTs simply need one pair of ctx + buffer + ops each, with no interference, and there is no global or static mutable state anywhere in the library.

```c
static elib_protocol_host_ctx_t  host_uart1;
static elib_protocol_host_ctx_t  host_uart2;   /* one more channel, used the same way */
```

## Reassembly Buffer and Callback Semantics

- `bufs.frame_buf` is the user-preallocated **frame reassembly buffer**, sized ≥ the maximum length of a single frame
- During the callback the buffer is frozen by the library: `frame->data` points into the DATA area of the buffer (its first byte = meta_count) and `data_len` includes the meta_count byte
- The moment the callback returns the buffer is reset for the next frame — copy the data inside the callback if you need to keep it; taking it on the spot is zero-copy
- `frame->addr/seq/desc`: the four address bytes, the sequence number and the descriptor are already extracted

## Porting Guide

- **Language standard**: C99; depends only on `<stdint.h> <stddef.h> <string.h>`
- **No dynamic memory, no recursion, no global mutable state**: every object is allocated by the user
- **No special ISR shape**: either `*_feed_byte` per byte in the interrupt, or batching with `*_feed` (the return value only reports drops and does not affect consumption — bytes are always eaten in full)
- **No alignment handling needed**: read frame data exclusively through the `elib_protocol_get_*` helpers, with pointers allowed at any offset; do **not** overlay a struct on `fdata` (neither endianness nor alignment is portable that way)
- **Wire endianness negotiation**: both ends take the same macro value; the LEN/ADDR/CRC the master sends to a slave address all follow the library endianness, with no per-field exceptions
- **Trimmable**: if you only use the master, do not compile slave.c, and vice versa

## Directory Layout

```
include/elib_protocol.h     the only public header (endianness/helpers/types/API)
src/elib_protocol_core.h/.c engine (parse state machine + incremental CRC + reassembly), framing, iterator
src/elib_protocol_host.c    master role (thin)
src/elib_protocol_slave.c   slave role (thin, adds address filtering)
test/test_elib_protocol.c   roundtrip + robustness test set
```

Build (example, with -Werror as a first-class construct):

```
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c src/elib_protocol_core.c
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c src/elib_protocol_host.c
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c src/elib_protocol_slave.c
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c test/test_elib_protocol.c
```

## Relationship to v1

The wire frame format is identical to v1 (existing peers are not broken); the API is an entirely new v2 refactor and is not compatible with the old v1 headers (`types.h`/`err.h` are merged into the single umbrella header). v2 fixes three catastrophic defects in the v1 receive path (a missing persistence of the partial CRC value, a full-buffer rescan polluting intermediate frames, and metadata endianness interpreted by host overlay) and removes every time-related parameter (idle detection belongs to the user).
