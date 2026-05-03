# 小机云蓝牙 X-B01 模块指南（电子秤/POS 系统）

## 一、硬件连接

| 引脚 | 名称 | STM32F103C8T6 连接 | 说明 |
|------|------|---------------------|------|
| VCC | 电源正极 | 3.3V | 模块工作电压 |
| GND | 地 | GND | 共地 |
| TXD | 模块发送端 | PA10 (USART1_RX) | STM32 接收 |
| RXD | 模块接收端 | PA9 (USART1_TX) | STM32 发送 |
| GPIO | 模式选择 | GND | LOW=透传模式 |

**串口配置**: USART1, 115200-8-N-1, DMA 循环接收 (CIRCULAR mode)

## 二、文本协议格式

### 2.1 上行数据（STM32 → 手机小程序）

**格式**: `$X#D#key1:val1;key2:val2;&CheckCode\r\n`

**CheckCode**: BCC 异或校验（`&` 前所有字节异或，输出 2 位大写十六进制 ASCII）

**字段定义**（共 5 个字段）：

| 绑定字段 | 组件类型 | 说明 | 数据格式 |
|----------|----------|------|----------|
| `text_category` | 文本 | 商品类别名称 | 字符串，如 "Apple" |
| `number_unit_price` | 数字 | 单价（元/kg） | 保留 2 位小数，如 5.00 |
| `number_weight` | 数字 | 重量（kg） | 保留 3 位小数，如 1.250 |
| `number_total_price` | 数字 | 总价（元） | 保留 2 位小数，如 6.25 |
| `text_status` | 文本 | 状态文本 | "正常" / "超重" / "异常" |

**上行示例**:
```
$X#D#text_category:Apple;number_unit_price:5.00;number_weight:1.250;number_total_price:6.25;text_status:正常&XX\r\n
```

### 2.2 下行命令（手机小程序 → STM32）

| 绑定字段 | 说明 | 示例 |
|----------|------|------|
| `selected_category` | 设置商品类别 | `selected_category:Apple` |
| `number_unit_price` | 修改单价 | `number_unit_price:6.50` |
| `cmd_reweigh` | 触发重称 | `cmd_reweigh:1` |

### 2.3 回复格式

- 成功: `$XA#D#OK&7D\r\n`
- 校验码错误: `$XA#D#ERR:XOR&XX\r\n`
- 数据格式错误: `$XA#D#ERR:D&XX\r\n`

## 三、通信流程

```
┌──────────┐   每 500ms 上行 5 字段   ┌──────────┐
│  STM32   │ ──────────────────────→ │  手机    │
│ (电子秤) │                          │  小程序  │
│          │ ←  下行命令（改类别/单价）│          │
└──────────┘                          └──────────┘
```

### 时序说明

1. **定时上报**: 每 500ms 通过 USART1 DMA 发送当前称重数据
2. **即时上报**: 按键操作（结算/修改/报警）后立即强制上传（hbt.immediate_upload=1）
3. **下行命令**: 收到完整帧（IDLE 中断检测 `\r\n`）后解析并回复
4. **TX 卡死恢复**: 如果 DMA TX 持续 BUSY 超过 2s，强制复位 UART TX 状态机

## 四、代码架构

```
bluetooth.h / bluetooth.c
├── Bluetooth_Init()          → 启动 USART1 DMA 循环接收 + IDLE 中断
├── Bluetooth_SendData()      → 构造文本协议帧并 DMA 发送
├── Bluetooth_ProcessCommand() → 消费 cmd_ready，解析所有完整帧
├── Bluetooth_ParseCommand()  → 校验 BCC，逐字段解析
├── Bluetooth_SetScaleData()  → 更新上行数据字段
├── Bluetooth_RequestUpload() → 设置立即上传标志
└── Bluetooth_CheckTXStuck()  → TX DMA 卡死检测与恢复
```

## 五、关键注意事项

| 要点 | 说明 |
|------|------|
| **DMA 混用问题** | TX/RX 必须全程使用 DMA 模式（`HAL_UART_Transmit_DMA`），不要在同一个 UART 上混用阻塞和 DMA TX，否则 HAL 状态机会不一致 |
| **TC 等待** | 每次启动新 DMA TX 前必须等待 TC（传输完成标志）置位，确保上一帧已从移位寄存器完全移出 |
| **TX 卡死恢复** | 若 gState 持续 BUSY_TX 超 2s，强制 toggle TE 位 + 清除 DMAT，避免硬件移位锁死 |
| **帧长限制** | X-B01 内部缓冲区约 256 字节，超出会被静默丢弃。帧长控制在 200 字节以内 |
| **BCC 校验** | 小程序会校验每条上行数据的 BCC，校验错误不会更新 UI |
| **蓝牙断线** | 模块自动重连，无需 STM32 干预 |
| **串口接线** | TXD→RX, RXD→TX（交叉连接），不要接反 |

## 六、与新项目的适配要点

1. **修改上行字段**: 在 `Bluetooth_SendData()` 中修改 `snprintf` 格式串，适配新项目的字段名和数据类型
2. **修改下行解析**: 在 `parse_field()` 中添加新项目的 `FIELD_MATCH` 分支
3. **结构体扩展**: 在 `Bluetooth_t` 中添加/删除数据字段
4. **上报周期**: 根据数据变化频率调整 `BT_PERIOD_MS`

---

*从 HVAC 项目（温度/阀门控制）迁移至电子秤/POS 项目*
*蓝牙模块不变，引脚不变，协议不变，仅数据字段不同*
