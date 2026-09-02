# elib-protocol

## 简介

elib-protocol是一个轻量级嵌入式通信协议库，专为主从架构设计。

## 特性

- 零平台依赖、零动态内存、零文件修改
- 主从双角色，同一套代码
- CRC16校验，序列号支持帧匹配
- 元数据ID机制，灵活扩展

---

## 帧格式

```
[0x68] [LEN_H] [LEN_L] [ADDR0] [ADDR1] [ADDR2] [ADDR3] [SEQ] [DESC] [DATA...] [CRC_H] [CRC_L] [0x16]
```

### 帧头（0x68）

固定值 `0x68`，标识帧起始。

### 帧长度（LEN）

2字节，大端序，表示整个帧的长度（包含帧头和帧尾）。

- 最小帧长：12字节（无数据）
- 最大帧长：由缓冲区大小决定

### 从机地址（ADDR）

4字节，大端序，用于多机通信识别。

| 地址 | 用途 |
|------|------|
| 0x00000000 | 中央主机 |
| 0x00000001 ~ 0xFFFFFFFE | 从机地址（从1开始编址） |
| 0xFFFFFFFF | 广播地址（所有从机接收） |

- 主机发送请求时，指定目标从机地址
- 从机响应或主动上报时，携带自身地址
- 广播地址时，所有从机接收处理

### 序列号（SEQ）

1字节，范围 0-255，循环使用。

- 用于帧匹配和去重
- 请求和响应使用相同序列号

### 描述符（DESC）

1字节，按位定义帧属性。

```
bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
  0     0     0     0    FRAG  TYPE1 TYPE0  DIR
```

| 位 | 名称 | 说明 |
|----|------|------|
| bit0 | DIR | 方向：0=下行(host→slave)，1=上行(slave→host) |
| bit1-2 | TYPE | 类型：00=请求，01=响应，10=事件 |
| bit3 | FRAG | 分片：0=无分片，1=有分片 |
| bit4-7 | 保留 | 固定为0 |

#### 方向（DIR）

- 下行帧：主机发送，从机接收
- 上行帧：从机发送，主机接收

#### 类型（TYPE）

- 00：请求帧（Request）- 主机发起请求
- 01：响应帧（Response）- 从机响应请求
- 10：事件帧（Event）- 从机主动上报

### 数据单元（DATA）

可变长度，包含一个或多个元数据条目。

#### 数据单元格式

```
[meta_count] [ID_H] [ID_L] [TYPE] [LEN_H] [LEN_L] [fdata...] ...
```

#### 元数据计数（meta_count）

1字节，表示数据单元中元数据条目的数量。

#### 元数据条目

每个元数据条目包含：

| 字段 | 长度 | 说明 |
|------|------|------|
| ID | 2字节 | 数据ID（主键，大端序） |
| TYPE | 1字节 | 数据类型 |
| LEN | 2字节 | 数据长度（大端序） |
| fdata | LEN字节 | 数据内容 |

#### 数据ID（ID）

2字节，大端序，用户自定义的标识符。

- ID是解析数据的主要依据
- 用户根据ID识别数据内容

#### 数据类型（TYPE）

1字节，标识数据格式。

| 值 | 类型 | 说明 |
|----|------|------|
| 0 | uint8 | 无符号8位整数 |
| 1 | uint16 | 无符号16位整数 |
| 2 | uint32 | 无符号32位整数 |
| 3 | uint64 | 无符号64位整数 |
| 4 | int8 | 有符号8位整数 |
| 5 | int16 | 有符号16位整数 |
| 6 | int32 | 有符号32位整数 |
| 7 | int64 | 有符号64位整数 |
| 8 | bool | 布尔值 |
| 9 | array | 字节数组 |

TYPE仅作为辅助参考，用户主要根据ID解析数据。

#### 数据长度（LEN）

2字节，大端序，表示后续fdata的字节数。

#### 数据内容（fdata）

LEN字节，实际数据。

### CRC校验（CRC）

2字节，大端序，CRC16/CCITT校验值。

- 多项式：0x1021
- 初始值：0xFFFF
- 校验范围：从帧头0x68到最后一个数据字节（不含CRC和帧尾）

### 帧尾（0x16）

固定值 `0x16`，标识帧结束。

---

## 字节序转换

协议使用大端序（网络字节序），提供以下转换函数：

```c
uint16_t elib_protocol_swap16(uint16_t val);
uint32_t elib_protocol_swap32(uint32_t val);
uint64_t elib_protocol_swap64(uint64_t val);
```

使用示例：
```c
// 写入16位大端序数据
uint16_t val = 0x1234;
buf[0] = (val >> 8) & 0xFF;
buf[1] = val & 0xFF;

// 或使用转换函数
uint16_t be_val = elib_protocol_swap16(val);
memcpy(buf, &be_val, 2);
```

## 错误处理

### 无效帧

- 帧长度 < 12：帧过短，丢弃
- 帧尾 ≠ 0x16：帧尾错误，丢弃
- CRC不匹配：校验错误，丢弃

### 超时

帧接收过程中，若数据长度长时间不变，触发超时。超时后丢弃当前帧，重置解析状态。

### 缓冲区溢出

帧长度超过缓冲区大小时，拒绝该帧。

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
uint8_t self_addr[4] = {0x00, 0x00, 0x00, 0x01};  // 自身地址
uint8_t val = 0xAA;

// 1. 生成帧头
size_t offset = elib_protocol_build_header(tx_buf, sizeof(tx_buf), self_addr, seq, desc);

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
