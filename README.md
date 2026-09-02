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

---

# 主机端使用

## 主机端初始化

```c
#include "elib_protocol.h"

// 定义接收缓冲区
static uint8_t rx_buf[256];

// 定义回调函数：当收到从机数据时调用
static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    // ctx->addr:    从机地址（4字节）
    // ctx->seq:     序列号
    // ctx->desc:    描述符
    // ctx->data:    数据指针
    // ctx->data_len: 数据长度
}

// 初始化主机
elib_protocol_ops_t ops = { .on_frame = on_frame };
elib_protocol_bufs_t bufs = {
    .rx_frame_buf = rx_buf,
    .rx_frame_buf_size = sizeof(rx_buf)
};
elib_protocol_host_cfg_t cfg = {
    .timeout_ms = 100
};

elib_protocol_ctx_t host;
elib_protocol_host_init(&host, &ops, &bufs, &cfg);
```

## 主机端主循环处理

```c
// 在主循环中调用，传入接收数据和长度
size_t consumed = 0;
elib_protocol_host_process(&host, dt_ms, rx_len, &consumed);

// 消费已处理的数据
if (consumed > 0) {
    memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
    rx_len -= consumed;
}
```

## 主机端组包发送

```c
uint8_t tx_buf[128];
uint8_t slave_addr[4] = {0x00, 0x00, 0x00, 0x01};  // 目标从机地址
uint8_t val = 0xFF;

// 1. 生成帧头
size_t offset = elib_protocol_build_header(tx_buf, sizeof(tx_buf), slave_addr, seq, desc);

// 2. 添加元数据（可多次调用）
offset += elib_protocol_build_meta(tx_buf + offset, sizeof(tx_buf) - offset,
                                   0x0001, ELIB_PROTOCOL_DATA_U8, &val, 1);

// 3. 打包完成帧
size_t frame_len = elib_protocol_build_pack(tx_buf, sizeof(tx_buf), offset);

// 4. 发送
send(tx_buf, frame_len);
```

## 主机端解包数据

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

---

# 从机端使用

## 从机端初始化

```c
#include "elib_protocol.h"

// 定义接收缓冲区
static uint8_t rx_buf[256];

// 定义回调函数：当收到主机数据时调用
static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    // ctx->addr:    主机地址（广播时为0xFFFFFFFF）
    // ctx->seq:     序列号
    // ctx->desc:    描述符
    // ctx->data:    数据指针
    // ctx->data_len: 数据长度
}

// 本机地址（从1开始编址）
uint8_t self_addr[4] = {0x00, 0x00, 0x00, 0x01};

// 初始化从机
elib_protocol_ops_t ops = { .on_frame = on_frame };
elib_protocol_bufs_t bufs = {
    .rx_frame_buf = rx_buf,
    .rx_frame_buf_size = sizeof(rx_buf)
};
elib_protocol_slave_cfg_t cfg = {
    .timeout_ms = 100,
    .addr = self_addr
};

elib_protocol_ctx_t slave;
elib_protocol_slave_init(&slave, &ops, &bufs, &cfg);
```

## 从机端主循环处理

```c
// 在主循环中调用，传入接收数据和长度
size_t consumed = 0;
elib_protocol_slave_process(&slave, dt_ms, rx_len, &consumed);

// 消费已处理的数据
if (consumed > 0) {
    memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
    rx_len -= consumed;
}
```

## 从机端组包发送

```c
uint8_t tx_buf[128];
uint8_t master_addr[4] = {0x00, 0x00, 0x00, 0x00};  // 主机地址
uint8_t val = 0xAA;

// 1. 生成帧头
size_t offset = elib_protocol_build_header(tx_buf, sizeof(tx_buf), master_addr, seq, desc);

// 2. 添加元数据（可多次调用）
offset += elib_protocol_build_meta(tx_buf + offset, sizeof(tx_buf) - offset,
                                   0x0001, ELIB_PROTOCOL_DATA_U8, &val, 1);

// 3. 打包完成帧
size_t frame_len = elib_protocol_build_pack(tx_buf, sizeof(tx_buf), offset);

// 4. 发送
send(tx_buf, frame_len);
```

## 从机端解包数据

```c
static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    size_t pos = 0;
    elib_protocol_meta_t *meta;

    while (elib_protocol_get_next_meta(&pos, ctx->data, ctx->data_len, &meta) == ELIB_PROTOCOL_OK) {
        switch (meta->id) {
        case 0x0001:  // 控制指令
            uint8_t cmd = meta->fdata[0];
            break;
        case 0x0002:  // 参数设置
            uint16_t param = (meta->fdata[0] << 8) | meta->fdata[1];
            break;
        }
    }
}
```

---

## 错误处理

- 帧长度 < 12：拒绝
- 帧尾 ≠ 0x16：拒绝
- CRC不匹配：拒绝
- 数据超时：丢弃当前帧
