**简体中文** | [English](README.en.md)

# elib-protocol

轻量级主从（master/slave）总线协议 —— C99、零动态内存、零全局状态、时间零感知。

主机与从机是真分离的两个角色（各自的 ctx 与 API），共享同一个流式解析引擎。
字节流式喂入，完整帧通过回调浮出。适用于 UART / RS485 / RS232 等任意
字节流物理层，**不附带任何驱动**——收发由你自己的 BSP 完成，本库只管协议本身。

## 特性

- **帧格式严格定长前缀**：一眼定位全帧长度，接收端永不解析无效字节
- **流式接收**：每个字节恰好喂一次，无重扫、无回看、无 consumed 账本
- **校验可外挂**：自定义 16 位校验函数整段一次调用（硬件 CRC 外设直通），
  ops.crc 不提供则内置软件 CRC16/CCITT-FALSE
- **防淤积四重防护**：任何脏数据流下接收器都有界收敛，永不卡死
- **真多实例**：全部状态在 ctx 里，回调带 `user_data`，库中无任何全局变量
- **配置端序**：全库线缆字节序默认大端，`-D` 一个宏整体翻转为小端
- **全套类型读写助手**：u8/u16/u32/u64/i8/i16/i32/i64/bool，逐字节移位实现，
  与机器端序无关、无对齐炸弹
- **断流检测归用户**：库对时间零感知，`reset()` 任何时机可调
- **零动态内存**：ctx 与重组缓冲均由用户静态分配

## 帧格式

```
0x68 | LEN_H LEN_L | ADDR0 ADDR1 ADDR2 ADDR3 | SEQ | DESC | DATA ... | CRC_H CRC_L | 0x16
```

| 字段 | 大小 | 说明 |
|------|------|------|
| HEAD | 1 | 固定 `0x68` |
| LEN  | 2 | **全帧字节数**（含头尾 12B 开销），最小 12，按配置端序 |
| ADDR | 4 | 目标站地址，按配置端序；`0xFFFFFFFF` = 广播 |
| SEQ  | 1 | 序号，回环不透明（请求-响应关联由应用层自查） |
| DESC | 1 | 描述字，位域见下 |
| DATA | ≥1 | 元数据区：`meta_count` + N × 条目（见下） |
| CRC  | 2 | CRC16/CCITT-FALSE（poly `0x1021`，初值 `0xFFFF`）**默认算法，可外挂自定义**（见"自定义校验算法"），覆盖从头到数据末（`[0, LEN-3)`），不含 CRC 域与帧尾，按配置端序 |
| TAIL | 1 | 固定 `0x16`，**不做转义**（见"鲁棒性承诺"） |

**DESC 位域**：

```
bit0      : DIR   0=下行（主机→从机） 1=上行（从机→主机）
bit1..2   : TYPE  0x00=REQ  0x02=RESP  0x04=EVENT
bit3      : FRAG  0x00=整帧 0x08=分片
其余位    : 保留，置 0
```

现成组合宏：`ELIB_PROTOCOL_DESC_REQ_DOWN` / `DESC_RESP_UP` / `DESC_EVENT_UP`。

**DATA 区（元数据列表）**：

```
meta_count(1) + N × [ ID(2) | TYPE(1) | LEN(2) | fdata(LEN) ]
```

- `meta_count`：条目数（1 字节，≤255）
- `ID`：应用自定义的键，按配置端序
- `TYPE`：建议性载荷类型（`elib_protocol_data_type_t`，ARRAY `0x09` 为裸字节串）
- `LEN`：fdata 字节数，按配置端序
- `fdata`：原始载荷字节；数值解析一律走端序助手

帧内所有数值字段（LEN/ADDR/meta ID/meta LEN/CRC/fdata 数值）全部按**配置端序**
解释，且由助手统一读写。

## 端序配置

```c
/* 默认：ELIB_PROTOCOL_ORDER_BIG —— 全库按大端解线缆字节 */
/* 翻转：编译时加 -DELIB_PROTOCOL_ORDER_LITTLE */
```

- 总线两端必须一致：要么都默认，要么都加 `-D` 翻转
- 头文件用 `__BYTE_ORDER__` 检测机器端序并导出
  `ELIB_PROTOCOL_MACHINE_BIG/LITTLE`（检测不到默认大端，可 `-D` 覆盖），
  仅供用户代码判断自身
- **助手不依赖机器端序**：`get/put` 系列按配置端序逐字节移位，任何平台结果
  一致，天然无对齐问题（指针可指向任意奇偶偏移）

助手清单：`elib_protocol_get/put_u8 u16 u32 u64 i8 i16 i32 i64 bool`
（bool 为 1 字节，0 或 1），另有 `swap16/32/64` 纯值交换器。

## 快速上手

### 1. 声明与初始化（零动态内存）

```c
#include "elib_protocol.h"

#define FRAME_MAX 64u

static uint8_t rx_buf[FRAME_MAX];                    /* 帧重组缓冲 */

static void on_frame(void *ud, const elib_protocol_frame_ctx_t *f) {
    (void)ud;
    uint16_t pos = 0;
    elib_protocol_meta_t m;

    while (elib_protocol_get_next_meta(&pos, f->data, f->data_len, &m)
           == ELIB_PROTOCOL_OK) {
        switch (m.id) {
        case 0x0001:  /* u32 温度 */
            if (m.type == ELIB_PROTOCOL_DATA_U32 && m.len == 4u) {
                uint32_t temp = elib_protocol_get_u32(m.fdata);
                /* ... */
            }
            break;
        default:
            break;
        }
    }
    /* f->data 指向重组缓冲，回调返回即失效；要长期持有请自行拷贝 */
}

/* ---- 从机 ---- */
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
/* ---- 主机（无自地址：一切上行帧全收） ---- */
static elib_protocol_host_ctx_t host;

void host_setup(void) {
    static const elib_protocol_ops_t ops = { NULL, on_frame, NULL };
    static const elib_protocol_bufs_t bufs = { rx_buf, FRAME_MAX };

    (void)elib_protocol_host_init(&host, &ops, &bufs);
}
```

### 2. 收：把收到的字节喂进来（驱动无关）

```c
/* UART RX 中断/DMA/轮询里拿到 n 字节 */
void uart_on_rx(const uint8_t *bytes, uint16_t n) {
    elib_protocol_slave_feed(&slave, bytes, n);
    /* 返回值 = 本批最后一个被弃帧的丢弃原因（无弃帧则 OK）；
     * 所有喂入的字节恒定全部消费 */
}

/* 或逐字节（内部与批量同一路径） */
void uart_on_rx_byte(uint8_t b) {
    (void)elib_protocol_slave_feed_byte(&slave, b);
}
```

### 3. 发：组帧三段式

```c
uint8_t tx[64];
size_t  off;
uint8_t temp_buf[4];

elib_protocol_put_u32(temp_buf, 2580u);   /* 数值过线必走助手 */

off = elib_protocol_build_header(tx, sizeof(tx), SELF_ADDR /* NULL=广播 */,
                                 seq++, ELIB_PROTOCOL_DESC_REQ_DOWN);
off += elib_protocol_build_meta(tx + off, sizeof(tx) - off,
                                0x0001u, ELIB_PROTOCOL_DATA_U32,
                                temp_buf, 4u);
off += elib_protocol_build_meta(tx + off, sizeof(tx) - off,
                                0x0002u, ELIB_PROTOCOL_DATA_ARRAY, NULL, 0u);
{
    size_t frame_len = elib_protocol_build_pack(tx, sizeof(tx), off);
    if (frame_len != 0u) {
        uart_send(tx, (uint16_t)frame_len);   /* 你的物理层 */
    }
}
```

三段式说明：

- `build_header` 写入 10 字节（头 + LEN 占位 + 地址 + SEQ + DESC + meta_count 占位），返回 10
- `build_meta` 在写入位置追加一条元数据（ID/TYPE/LEN/载荷），返回新写入位置；`data` 为 NULL 仅允许 `len==0`
- `build_pack` 回扫元数据区填 meta_count/全帧长/CRC/帧尾，**元数据区必须被条目严格铺满**（每条 5+len 且无剩余），否则返回 0 拒绝封帧——坏帧出不了炉

### 4. 断流检测（完全归用户）

库不管理时间。你的流断策略（空闲超时、看门狗、链路状态……）自行触发：

```c
/* 任何时机可调：清零全部解析状态，回到"猎头"态 */
(void)elib_protocol_slave_reset(&slave);
```

推荐模式：记录"距上次收到任意字节的时长"，超过阈值即 `reset()`——半截帧、
弃帧配额、吞字节计数一并清空，下一帧干净起步。

## 自定义校验算法（硬件 CRC 外设）

默认使用内置软件 CRC16/CCITT-FALSE。若你的 MCU 带硬件 CRC 外设，
在 `ops.crc` 里提供自定义函数，即可整段一次调用直通外设。

自定义校验函数共一个形态，收发两侧必认：

```c
typedef uint16_t (*elib_protocol_crc_fn)(void *ud, const uint8_t *data,
                                         size_t len);
```

约定：

- **调用时机与切片**：每个候选帧的 CRC 域收满时，库对重组缓冲里的
  覆盖区 `buf[0..LEN-3)`（帧头..数据末）**整段一次性调用**——帧此时必然
  完整在缓冲内（LEN 已在 LEN_L 态核过容量），无需硬件外设担心 seed 跨字节携带
- `ud` 透传 `ops.user_data`（多 CRC 单元或外设句柄场景用）
- 须返回 16 位值，库按配置端序与线上 CRC 域比对；算法自含初值/多项式定义
- 组帧侧用 `elib_protocol_build_pack_crc(buf, size, offset, crc, ud)` 封帧
  （原 `build_pack` 恒走内置算法，签名不变，向后兼容）
- 内置软件算法即公开的 `elib_protocol_crc16(data, len)`，你的自定义
  实现里可直接复用（比如做位反变体或紧盯外设差异的对照探针）

以 STM32 硬件 CRC 为例（伪代码示意）：

```c
static uint16_t hw_crc16(void *ud, const uint8_t *data, size_t len) {
    CRC_HandleTypeDef *hcrc = ud;   /* 与 ops.user_data 对上 */

    __HAL_CRC_DR_RESET(hcrc);      /* 外设复位（含初始值设置） */
    for (size_t i = 0; i < len; i++) {
        (void)HAL_CRC_Accumulate(hcrc, (uint32_t *)&data[i], 1); /* 或 DMA 一次整段 */
    }
    return (uint16_t)hcrc->Instance->DR;
}

static const elib_protocol_ops_t ops = { &hcrc, on_frame, hw_crc16 };
```

**红字：总线两端必须同算法。**收端配了自定义算法，发端就必须用
`build_pack_crc` 带同一函数，否则每个帧都会被对面以 `CHECKSUM` 裁掉。
发端忘记换变体是此功能唯一的高频踩坑点。多实例各自带各自的
外设句柄（`user_data`），互不干扰。

## 错误码

| 错误码 | 含义 |
|--------|------|
| `OK` | 成功 |
| `INVALID_PARAM` | init/喂入参数非法（NULL、批量 feed 的 bytes==NULL 且 len!=0） |
| `NOT_INITIALIZED` | 未 init 就调用 |
| `BUFFER_TOO_SMALL` | 组帧：输出缓冲不够（builder 返回 0） |
| `CHECKSUM` | 收：CRC 不符，整帧弃 |
| `FRAME` | 收：坏帧（帧尾错误）；组帧：元数据回扫不合规，拒绝封帧 |
| `BUSY` | on_frame 回调执行期间重入 feed/reset |

`feed` 批量返回值 = **本批最后一个被弃帧的丢弃原因**；单帧被地址过滤属多机常态，
不计为弃帧。喂入的字节无论如何全部消费。

## 接收管线

```
IDLE      : 等 0x68（垃圾字节直接丢弃，不进缓冲；命中 → 存头）
LEN_H/L   : LEN 两字节到手即核：
            ├ < 12（假 LEN）  → 丢弃这 3 字节，回 IDLE 等头部之后最近的 0x68
            ├ > 缓冲（超大）  → SWALLOW 吞掉余下定额，回 IDLE
            └ 合法            → 继续装帧，缓冲溢出结构性不可能
ADDR0..3  : 存 4 字节；从机收满即过滤（本机/广播才放行，其余静默吞噬余下定额；
            主机无自地址 → 全收）
SEQ/DESC  : 存；DESC 后若数据为空（LEN==12）→ 直接进 CRC
DATA      : 存字节；数据区 0x68 是普通载荷，绝不重搜
CRC_H/L   : CRC 域存入后即对 buf[0..LEN-3)（头..数据末）整段
            一次性调用校验函数（ops.crc 自定义或内置软件版）
            → 得到期望值，TAIL 时与线缆域比对
TAIL      : ≠ 0x16 →FRAME 弃帧；CRC 不符 →CHECKSUM 弃帧；
            均过 → on_frame(user_data, frame) → 复位回 IDLE 接下一帧
SWALLOW   : 计数递减到 0 回 IDLE；期间字节一律不解读
```

## 鲁棒性承诺

**防淤积四重防护：**

1. **缓冲溢出结构性不可能**——缓冲只装"经 LEN 认证"的帧段，LEN 先核后装
2. **脏数据定额收敛**——弃帧一律按已知定额丢弃：假 LEN 丢 3 字节即刻回头、
   真弃帧/地址过滤吞完余下配额严格回 IDLE；期满后只在**真实** 0x68 处重同步
3. **断流检测归用户**——`reset()` 任何时机可调，把一切中态清空
4. **回调重入闸**——on_frame 执行期间任何 feed/reset 返回 `BUSY`

**错位碰撞的有界代价**（连续流丢字节导致错位时）：每次碰撞最多损失
"旧帧剩余配额 + 一个伪头 3 字节"，CRC 必裁一切误拼，IDLE 严格重同步，
**永不卡死**。

**为什么不做转义**：帧格式定长前缀 + LEN 一眼定位，无需转义即可保证正确性；
转义/COBS 引入成对代价（带宽、实现复杂度），收益为零。载荷 `fdata` 含 `0x68`
是合法数据，接收端在 DATA 态绝不重搜帧头。

## 多实例

全部状态（解析状态机、CRC、缓冲游标、吞字节定额、重入闸）都在 ctx 里；
回调经 `ops.user_data` 原样注入使用方上下文。多路 UART 各自一对
ctx + 缓冲 + ops 即可，互不干扰，库内无任何全局/静态可变状态。

```c
static elib_protocol_host_ctx_t  host_uart1;
static elib_protocol_host_ctx_t  host_uart2;   /* 再来一路，一样用 */
```

## 重组缓冲与回调语义

- `bufs.frame_buf` 是用户预分配的**帧重组缓冲**，尺寸 ≥ 单帧最大长度
- 回调期间缓冲被库冻结：`frame->data` 指向缓冲内 DATA 区
  （首字节 = meta_count），`data_len` 含 meta_count 字节
- 回调返回即刻复位服役下一帧——要长期持有数据请回调内自行拷贝，
  现用现取则零拷贝
- `frame->addr/seq/desc`：地址四字节、序号、描述字已抠出

## 移植指南

- **语言标准**：C99；仅依赖 `<stdint.h> <stddef.h> <string.h>`
- **无动态内存、无递归、无全局可变状态**：所有对象用户分配
- **无 ISR 特殊形态**：中断里逐字节 `*_feed_byte`，或攒批 `*_feed`
  （返回值语义只有弃帧报告，不影响消费——字节恒定全部吃掉）
- **无需对齐处理**：读帧数据请全部走 `elib_protocol_get_*` 助手，
  指针可指向任意偏移；**不要**对 `fdata` 做结构体 overlay（端序与对齐都不可移植）
- **线缆端序协商**：两端同一宏取值；从机地址主机下发的 LEN/ADDR/CRC
  全部随库端序走，无逐字段的例外
- **可裁剪**：只用主机则不编译 slave.c，反之亦然

## 目录

```
include/elib_protocol.h     唯一公共头（端序/助手/类型/API）
src/elib_protocol_core.h/.c 引擎（解析状态机 + 增量 CRC + 重组）、组帧、迭代器
src/elib_protocol_host.c    主机角色（薄）
src/elib_protocol_slave.c   从机角色（薄，加地址过滤）
test/test_elib_protocol.c   roundtrip + 鲁棒性测试集
```

构建（示例 -Werror 一等构造）：

```
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c src/elib_protocol_core.c
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c src/elib_protocol_host.c
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c src/elib_protocol_slave.c
arm-none-eabi-gcc -std=c99 -Wall -Wextra -Werror -Iinclude -c test/test_elib_protocol.c
```

## 与 v1 的关系

线缆帧格式与 v1 完全一致（互不破坏既有对端）；API 为 v2 全新重构，
不兼容 v1 旧头文件（`types.h`/`err.h` 已并入单头伞）。v2 修复了 v1
接收路径的三处灾难级缺陷（CRC 断值持久化缺失、全缓冲重扫污染中态帧、
元数据端序按本机 overlay）并删除全部时间相关参数（断流检测归用户）。
