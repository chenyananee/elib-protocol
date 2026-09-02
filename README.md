# elib-protocol

## 简介

elib-protocol是一个轻量级嵌入式通信协议库，专为主从架构设计。协议采用帧结构进行数据传输，支持主机和从机双角色，适用于各种嵌入式通信场景，如工业控制、传感器网络、设备互联等。

## 特性

- **零平台依赖**：所有IO操作通过用户回调实现，不依赖特定硬件或操作系统
- **零动态内存**：所有缓冲区由用户预分配，避免内存碎片和分配失败
- **零文件修改**：通过API配置所有参数，无需修改库文件
- **主从双角色**：同一套代码支持主机和从机角色，通过编译选项或API选择
- **可靠传输**：CRC16校验确保数据完整性，序列号支持帧匹配和去重
- **灵活数据**：支持多种数据类型，元数据ID机制便于扩展
- **错误处理**：完善的错误检测和超时机制，确保通信稳定性

## 协议规范

### 帧格式

```
[0x68] [LEN_H] [LEN_L] [ADDR0] [ADDR1] [ADDR2] [ADDR3] [SEQ] [DESC] [DATA...] [CRC_H] [CRC_L] [0x16]
```

| 字段 | 长度 | 说明 |
|------|------|------|
| 0x68 | 1 | 帧头，固定值 |
| LEN_H | 1 | 帧长度高字节 |
| LEN_L | 1 | 帧长度低字节 |
| ADDR0-3 | 4 | 从机地址码（4字节） |
| SEQ | 1 | 序列号，0-255循环 |
| DESC | 1 | 描述符 |
| DATA | N | 数据单元 |
| CRC_H | 1 | CRC16高字节 |
| CRC_L | 1 | CRC16低字节 |
| 0x16 | 1 | 帧尾，固定值 |

### 帧长度

`LEN = N + 12`

- N为数据单元长度
- 12为帧头(1) + 长度(2) + 地址(4) + 序列号(1) + 描述符(1) + CRC(2) + 帧尾(1)
- 最小帧长：12（无数据）
- 最大帧长：由缓冲区大小决定

### 从机地址（ADDR）

- 4字节地址码，用于多机通信
- 主机请求从机时，指定目标从机地址
- 从机响应或主动上报时，携带自身地址
- 广播地址：0x00000000（所有从机接收）

### 描述符（DESC）

```
bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
  0     0     0     0    FRAG  TYPE1 TYPE0  DIR
```

| 位 | 名称 | 说明 |
|----|------|------|
| bit0 | DIR | 方向：0=下行(host→slave)，1=上行(slave→host) |
| bit1-2 | TYPE | 类型：00=请求，01=响应，10=事件 |
| bit3 | FRAG | 分片：0=无分片，1=有分片 |

### 方向（DIR）

- **下行帧**：主机发送，从机接收
- **上行帧**：从机发送，主机接收

### 类型（TYPE）

- 00：请求帧（Request）
- 01：响应帧（Response）
- 10：事件帧（Event）

### CRC校验

算法：CRC16/CCITT（多项式0x1021，初始值0xFFFF）

校验范围：从帧头0x68到最后一个数据字节（不含CRC和帧尾）

```
CRC = CRC16/CCITT([0x68][LEN_H][LEN_L][SEQ][DESC][DATA...])
```

### 数据单元格式

```
[meta_count] [ID_H ID_L TYPE LEN_H LEN_L fdata...] ...
```

| 字段 | 长度 | 说明 |
|------|------|------|
| meta_count | 1 | 元数据条目数量 |
| ID_H | 1 | 数据ID高字节（主键） |
| ID_L | 1 | 数据ID低字节（主键） |
| TYPE | 1 | 数据类型（辅助） |
| LEN_H | 1 | 数据长度高字节 |
| LEN_L | 1 | 数据长度低字节 |
| fdata | LEN | 数据内容 |

> **注意**：ID是用户解析数据的主要依据，TYPE仅作为辅助参考。

### 数据类型

| 值 | 类型 |
|----|------|
| 0 | uint8 |
| 1 | uint16 |
| 2 | uint32 |
| 3 | uint64 |
| 4 | int8 |
| 5 | int16 |
| 6 | int32 |
| 7 | int64 |
| 8 | bool |
| 9 | array |

## 库使用方法

### 头文件

```c
#include "elib_protocol.h"
```

### 主机端初始化

```c
// 1. 定义缓冲区
static uint8_t rx_frame_buf[256];

// 2. 定义回调
static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    // 处理接收到的帧
    // ctx->seq: 序列号
    // ctx->desc: 描述符
    // ctx->data: 数据指针
    // ctx->data_len: 数据长度
}

// 3. 初始化
elib_protocol_ops_t ops = { .on_frame = on_frame };
elib_protocol_bufs_t bufs = {
    .rx_frame_buf = rx_frame_buf,
    .rx_frame_buf_size = sizeof(rx_frame_buf)
};
elib_protocol_host_cfg_t cfg = {
    .polling_interval_ms = 10,
    .timeout_ms = 100
};

elib_protocol_host_ctx_t host;
elib_protocol_host_init(&host, &ops, &bufs, &cfg);
```

### 主机端处理

```c
// 在主循环中调用
size_t consumed = 0;
elib_protocol_host_process(&host, dt_ms, rx_len, &consumed);

// 处理消费的数据
if (consumed > 0) {
    memmove(rx_buf, rx_buf + consumed, rx_len - consumed);
    rx_len -= consumed;
}
```

### 从机端初始化

```c
elib_protocol_slave_ctx_t slave;
elib_protocol_slave_init(&slave, &ops, &bufs, &cfg);
```

### 从机端处理

```c
size_t consumed = 0;
elib_protocol_slave_process(&slave, dt_ms, rx_len, &consumed);
```

### 组包发送

```c
uint8_t tx_buf[128];
uint8_t val = 0xFF;
uint8_t addr[4] = {0x00, 0x00, 0x00, 0x01};  // 从机地址

// 1. 生成帧头（10字节）
size_t offset = elib_protocol_build_header(tx_buf, sizeof(tx_buf), addr, seq, desc);

// 2. 添加元数据（可多次调用）
offset += elib_protocol_build_meta(tx_buf + offset, sizeof(tx_buf) - offset,
                                   0x0001, ELIB_PROTOCOL_DATA_U8, &val, 1);

// 3. 打包完成帧
size_t frame_len = elib_protocol_build_pack(tx_buf, sizeof(tx_buf), offset - 10);

// 4. 发送
send_data(tx_buf, frame_len);
```

### 解包数据

```c
static void on_frame(const elib_protocol_frame_ctx_t *ctx) {
    size_t pos = 0;
    elib_protocol_meta_t *meta;

    // 遍历所有元数据
    while (elib_protocol_get_next_meta(&pos, ctx->data, ctx->data_len, &meta) == ELIB_PROTOCOL_OK) {
        // 根据ID识别数据（主键）
        switch (meta->id) {
        case 0x0001:  // 温度
            float temp = *(float *)meta->fdata;
            break;
        case 0x0002:  // 湿度
            uint16_t humidity = (meta->fdata[0] << 8) | meta->fdata[1];
            break;
        case 0x0003:  // 状态
            uint8_t status = meta->fdata[0];
            break;
        }

        // TYPE仅作为辅助参考，确认数据格式
        if (meta->type == ELIB_PROTOCOL_DATA_FLOAT) {
            // 按float解析
        }
    }
}
```

## 错误处理

### 无效帧

- 长度 < 12：帧过短
- 帧尾 ≠ 0x16：帧尾错误
- CRC不匹配：校验错误

### 超时

- 帧接收过程中，若数据长度长时间不变，触发超时
- 超时后丢弃当前帧，重置解析状态

### 缓冲区溢出

- 帧长度超过缓冲区大小：拒绝该帧
- 帧起始位置 + 帧长度 > 缓冲区大小：拒绝该帧

## 示例帧

### 无数据帧

```
[0x68][0x00][0x0C][0x00][0x00][0x00][0x01][0x01][0x02][CRC_H][CRC_L][0x16]
```

- 长度 = 12（无数据）
- 地址 = 0x00000001
- SEQ = 1
- DESC = 0x02（响应，下行）

### 有数据帧

```
[0x68][0x00][0x13][0x00][0x00][0x00][0x01][0x02][0x01][0x01][0x00][0x01][0x00][0x01][0xFF][CRC_H][CRC_L][0x16]
```

- 长度 = 19（数据7字节）
- 地址 = 0x00000001
- SEQ = 2
- DESC = 0x01（请求，上行）
- meta_count = 1
- 数据：1个元数据条目，ID=1，类型=uint8，长度=1，值=0xFF

## 许可证

MIT
