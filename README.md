# elib-protocol

## 简介

elib-protocol是一个轻量级嵌入式通信协议库，专为主从架构设计。

## 特性

- 零平台依赖、零动态内存、零文件修改
- 主从双角色，同一套代码
- CRC16校验，序列号支持帧匹配
- 元数据ID机制，灵活扩展

## 协议规范

### 帧格式

```
[0x68] [LEN_H] [LEN_L] [ADDR0-3] [SEQ] [DESC] [DATA...] [CRC_H] [CRC_L] [0x16]
```

| 字段 | 长度 | 说明 |
|------|------|------|
| 0x68 | 1 | 帧头 |
| LEN | 2 | 帧长度（含自身） |
| ADDR | 4 | 从机地址 |
| SEQ | 1 | 序列号 0-255 |
| DESC | 1 | 描述符 |
| DATA | N | 数据单元 |
| CRC | 2 | CRC16/CCITT |
| 0x16 | 1 | 帧尾 |

### 地址分配

| 地址 | 用途 |
|------|------|
| 0x00000000 | 中央主机 |
| 0x00000001 ~ 0xFFFFFFFE | 从机地址 |
| 0xFFFFFFFF | 广播 |

### 描述符（DESC）

| 位 | 说明 |
|----|------|
| bit0 | 方向：0=下行，1=上行 |
| bit1-2 | 类型：00=请求，01=响应，10=事件 |
| bit3 | 分片：0=无，1=有 |

### 数据单元

```
[meta_count] [ID_H] [ID_L] [TYPE] [LEN_H] [LEN_L] [fdata...] ...
```

ID是主键，TYPE仅辅助参考。

## 使用方法

### 主机端

```c
// 1. 定义资源
static uint8_t rx_buf[256];

static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    // ctx->addr: 从机地址
    // ctx->data: 数据指针
    // ctx->data_len: 数据长度
}

// 2. 初始化
elib_protocol_ops_t ops = { .on_frame = on_frame };
elib_protocol_bufs_t bufs = { .rx_frame_buf = rx_buf, .rx_frame_buf_size = sizeof(rx_buf) };
elib_protocol_host_cfg_t cfg = { .timeout_ms = 100 };

elib_protocol_ctx_t ctx;
elib_protocol_host_init(&ctx, &ops, &bufs, &cfg);

// 3. 主循环处理
size_t consumed = 0;
elib_protocol_host_process(&ctx, dt_ms, rx_len, &consumed);
if (consumed > 0) {
    memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
    rx_len -= consumed;
}
```

### 从机端

```c
// 1. 定义资源（同主机）

// 2. 初始化（多一个地址参数）
uint8_t slave_addr[4] = {0x00, 0x00, 0x00, 0x01};
elib_protocol_slave_cfg_t cfg = { .timeout_ms = 100, .addr = slave_addr };

elib_protocol_ctx_t ctx;
elib_protocol_slave_init(&ctx, &ops, &bufs, &cfg);

// 3. 主循环处理（同主机，函数名不同）
elib_protocol_slave_process(&ctx, dt_ms, rx_len, &consumed);
```

### 组包发送

```c
uint8_t tx_buf[128];
uint8_t addr[4] = {0x00, 0x00, 0x00, 0x01};
uint8_t val = 0xFF;

size_t offset = elib_protocol_build_header(tx_buf, sizeof(tx_buf), addr, seq, desc);
offset += elib_protocol_build_meta(tx_buf + offset, sizeof(tx_buf) - offset,
                                   0x0001, ELIB_PROTOCOL_DATA_U8, &val, 1);
size_t frame_len = elib_protocol_build_pack(tx_buf, sizeof(tx_buf), offset);

send(tx_buf, frame_len);
```

### 解包数据

```c
static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    size_t pos = 0;
    elib_protocol_meta_t *meta;

    while (elib_protocol_get_next_meta(&pos, ctx->data, ctx->data_len, &meta) == ELIB_PROTOCOL_OK) {
        switch (meta->id) {
        case 0x0001:  // 温度
            float temp = *(float *)meta->fdata;
            break;
        case 0x0002:  // 湿度
            uint16_t humidity = (meta->fdata[0] << 8) | meta->fdata[1];
            break;
        }
    }
}
```

## 错误处理

- 帧长度 < 12：拒绝
- 帧尾 ≠ 0x16：拒绝
- CRC不匹配：拒绝
- 数据超时：丢弃当前帧
