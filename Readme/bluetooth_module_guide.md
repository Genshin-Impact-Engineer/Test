# 小机云蓝牙 X-B01 模块指南（电子秤/POS 系统）

## 一、硬件连接

| 引脚 | 名称 | STM32F103C8T6 连接 | 说明 |
| TXD | 模块发送端 | PA10 (USART1_RX) | STM32 接收 |
| RXD | 模块接收端 | PA9 (USART1_TX) | STM32 发送 |
| GPIO | 模式选择 | GND | LOW=透传模式 |

**串口配置**: USART1, 115200-8-N-1, DMA 循环接收 (CIRCULAR mode)

## 二、文本协议格式

### 2.1 上行数据（STM32 → 手机小程序）

**格式**: `$X#D#key1:val1;key2:val2;&CheckCode\r\n`
$ 符号：协议开始符号
& 符号：内容分割符
符号：协议结束符，十六进制0x0A 0x0D
# 符号：内容分割符，对协议内容进行分段。每段内容描述如下：
$X：协议固定开头
Cmd：协议功能，如Cmd为DATA为上传数据。部分指令功能码有简写，如DATA，简写为D。可减小发送数据量
Content：协议数据，数据格式见协议详情，注意数据中不可以出现#号。如果没有内容可以不需要该字段。
CheckCode：校验码，&前所有字符进行异或得到 1 个字节的十六进制数，以2 个ASCII 字符形式输出。如以下协议：$X#Cmd#Timestamp#Content&CheckCode\r\n &之前部分异或得到校验码为0x12（BCC校验(异或校验)），则CheckCode位填12

**CheckCode**: BCC 异或校验（`&` 前所有字节异或，输出 2 位大写十六进制 ASCII）
# 协议回复格式
$XA#Cmd#Content&CheckCode\r\n
如果无特殊说明，指令统一回复：
成功：$XA#Cmd#OK&CheckCode
失败：$XA#Cmd#ERR:message&CheckCode
常见的错误回复
ERR:CRC：未进行KEY_CONTENT校验，详见本文发送校验内容一节
ERR:XOR：指令校验码错误
ERR:D：数据格式错误
LOAD_FAIL：加载错误


件	组件属性	默认绑定字段	描述	预期值
文本	  text	    text	  显示的文字内容	    任意字符
选择按钮	items	   items	 单选：单个数值，默认绑定字段为item; 多选：逗号隔开，默认绑定字段为items。如：items:0,1,2,3,4,5,6	

微信小程序属性对应的的组件表如下
组件	    组件属性	 绑定字段     	    类型              备注
商品名      selected    selected_Goods    整型          程序、按键实时交互更（一位整型）
单价值      number      number_Price      浮点型          程序、按键实时交互更新（两位小数）
重量值      text        text_float_Weight   字符串          据程序测量值上传（两位小数）
去皮重量值   text       text_NetWeight    字符串          据程序测量去皮值上传（两位小数）
总价值      text        text_float_Total        字符串         据程序测量值与预
系统状态     text        text_state       字符串         无相关操作或非报警状态，什么也不显示
 商品单价表如下：
商品名selected_Goods(选项)    对应值    单价值(number_Price)
苹果                            0       8.00           
香蕉                            1       7.00
菠萝                            2       8.00   
葡萄                            3       6.00
西瓜                            4       4.00
梨子                            5       12.00
哈密瓜                          6       12.00
总价单位

工程运行后（除了oled开机加载那段时间）随后将对应的数据，数据（该数据线上线下保持一致，注意交互），通过串口发送给小程序（频率自设，尽量延迟低，让系统流畅）
线下控制按键，进行数值更改/模式改变操作，系统状态快速上传数据至小程序

系统状态 （text_state）
系统状态分为操作和报警两种
操作
开始调整：
Adjusting, please reweigh
调整完成：
Change completed
取消调整：
Change cancelled
去皮：
Tare
取消去皮：
Tare Off
称量完成：
Weighing complete
报警分为两种：
超重
应上传字符为：
Overweight！
Please measure again
重量抖动
应上传字符为：
Weight abnormal!
Please measure again
系统状态正常时什么也不显示
当程序切换水果、oled上也要快速更改对应数据，并快速上传数据使小程序变为对应值

```

```

### 2.2 下行命令（手机小程序 → STM32）

| 绑定字段 | 说明 | 示例 |
|----------|------|------|
| `selected_Goods` | 设置商品类别（整型索引 0~6） | `selected_Goods:0` |
| `number_Price` | 修改单价 | `number_Price:6.50` |

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

## 六、与新项目的适配要点

1. **修改上行字段**: 在 `Bluetooth_SendData()` 中修改 `snprintf` 格式串，适配新项目的字段名和数据类型
2. **修改下行解析**: 在 `parse_field()` 中添加新项目的 `FIELD_MATCH` 分支
3. **结构体扩展**: 在 `Bluetooth_t` 中添加/删除数据字段
4. **上报周期**: 根据数据变化频率调整 `BT_PERIOD_MS`

