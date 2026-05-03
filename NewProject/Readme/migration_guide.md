# HVAC → 电子秤/POS 系统 代码迁移指南

## 概述

本文档说明如何将 HVAC 项目（供暖阀门控制）的硬件驱动代码迁移到新项目（电子秤/POS 称重系统）。

**迁移原则**: 硬件驱动层完全复用，仅修改应用层逻辑。所有引脚连接保持不变。

## 迁移文件对照表

### 蓝牙模块

| 原文件 | 新文件 | 迁移程度 |
|--------|--------|----------|
| `Core/Inc/bluetooth.h` | `NewProject/Bluetooth/bluetooth.h` | 结构体重定义，接口重命名 |
| `Core/Src/bluetooth.c` | `NewProject/Bluetooth/bluetooth.c` | BCC/协议框架复用，数据字段替换 |
| — | `NewProject/Bluetooth/bluetooth_module_guide.md` | 新增：模块使用指南 |

**主要变更**:
- 数据字段：温度/浊度/阀门 → 商品类别/单价/重量/总价
- 字段数量：10 字段 → 5 字段
- `Bluetooth_SetSensorData()` → `Bluetooth_SetScaleData()`
- `parse_field()` 字段映射表完全不同

**完全复用的部分**:
- `BCC_Calculate()` / `BCC_Format()` 校验码生成
- `send_response()` 回复帧构造 + TC 等待
- `Bluetooth_Init()` DMA 循环接收 + IDLE 中断
- `Bluetooth_ProcessCommand()` 帧遍历 + 校验码检查
- `Bluetooth_SendData()` TC 等待 + DMA TX 卡死恢复
- `Bluetooth_CheckTXStuck()` TX 状态机复位

### OLED 模块

| 原文件 | 新文件 | 迁移程度 |
|--------|--------|----------|
| `Core/Inc/oled.h` | `NewProject/OLED/oled.h` | 完全重写 |
| `Core/Src/oled.c` | `NewProject/OLED/oled.c` | 绘图函数复用，页面逻辑重写 |
| `Drivers/Logic/OLED_Dshan/driver_oled.h` | 不变 | 直接复用，无需修改 |
| `Drivers/Logic/OLED_Dshan/driver_oled.c` | 不变 | 直接复用，无需修改 |
| `Drivers/Logic/OLED_Dshan/ascii_font.c` | 不变 | 直接复用，无需修改 |
| — | `NewProject/OLED/oled_module_guide.md` | 新增：模块使用指南 |

**主要变更**:
- 页面：温度/阀门/浊度数据页 → 商品称重页
- 页面：温度/浊度阈值设置页 → 预览编辑页
- 报警页：温度/浊度报警 → 超重/重量异常
- 数据结构：`PageData_t`/`PageSettings_t` → `ScaleData_t` + `Product_t` 商品表
- 浏览模式光标：2 项选择 → 2 项（类别/单价）
- 编辑模式：调节温度/阈值 → 切换商品/调节单价
- 页面切换：长按翻页 → 按键逻辑触发跳转
- 页面枚举：`PAGE_DATA/SETTINGS/ALARM` → `PAGE_WEIGHING/PREVIEW/ALARM`

**完全复用的部分**:
- `fb_draw_char()` / `fb_draw_string()` 帧缓冲绘图
- `fb_draw_pixel()` / `fb_draw_rect()` / `fb_draw_sel_rect()` 图形绘制
- `OLED_ForceRecover()` / `_oled_probe()` I2C BUSY 恢复
- `OLED_UpdateDisplay()` 刷新率控制 + 闪烁 + I2C 故障恢复
- `Boot_Interface_Show()` 开机动画（仅标题文字修改）

### 按键模块

| 原文件 | 新文件 | 迁移程度 |
|--------|--------|----------|
| `Core/Inc/key.h` | `NewProject/Key/key.h` | 几乎不变，仅注释更新 |
| `Core/Src/key.c` | `NewProject/Key/key.c` | 完全不变（仅注释更新）|
| — | `NewProject/Key/key_module_guide.md` | 新增：模块使用指南 |

**主要变更**:
- 按键功能映射表完全不同（见 `oled_module_guide.md`）
- 实际按键行为由 `OLED_HandleKey()` 解释，key.c 零修改

**完全复用的部分**:
- 全部代码：消抖状态机、事件队列、EXTI 中断、轮询回退
- 引脚映射表：PB12/PB13/PB14/PB15 完全不变
- `Key_Scan()` 两级状态机（轮询回退 → 消抖 → 事件判定）
- `Key_GetEvent()` 事件队列消费
- `Key_ISR()` EXTI 中断记录

## 集成到新 STM32CubeMX 项目的步骤

### 1. CubeMX 配置（与 HVAC 项目相同）

| 外设 | 配置 |
|------|------|
| USART1 | 115200-8-N-1, DMA TX+RX (Circular), IDLE interrupt |
| I2C1 | 100kHz, PB6(SCL)/PB7(SDA) |
| GPIO | PB12-PB15 输入上拉, EXTI 下降沿 |
| TIM2 | 1kHz 基准时基（可选，供系统滴答） |

### 2. 复制驱动文件（无需修改）

```
Drivers/Logic/OLED_Dshan/
├── driver_oled.h
├── driver_oled.c
├── ascii_font.c
└── chinese_font.c
```

### 3. 复制应用层文件

```
NewProject/Bluetooth/
├── bluetooth.h     → Core/Inc/
├── bluetooth.c     → Core/Src/
└── bluetooth_module_guide.md  → Readme/

NewProject/OLED/
├── oled.h          → Core/Inc/
├── oled.c          → Core/Src/
└── oled_module_guide.md       → Readme/

NewProject/Key/
├── key.h           → Core/Inc/
├── key.c           → Core/Src/
└── key_module_guide.md        → Readme/
```

### 4. 修改 main.c

参考 HVAC 项目的 `main.c`，调整以下内容：

```c
// 头文件
#include "sensor.h"    // 如使用 HX711 称重传感器
#include "key.h"
#include "oled.h"
#include "bluetooth.h"

// main() 初始化顺序
Sensor_Init();           // 如 HX711 初始化
Key_Init();
OLED_Setup();
Boot_Interface_Show();
Bluetooth_Init();

// 主循环时间片
if (now - last_key_scan >= 10) {
    Key_Scan();
    while (Key_GetEvent(&kid, &is_long)) {
        OLED_HandleKey(kid, is_long);
        Bluetooth_RequestUpload();  // 按键操作即时上报
    }
    // 读取传感器数据并更新
    Sensor_Update();
    OLED_UpdateScaleData(sensor_weight);
}

if (now - last_oled >= (holog.edit_mode ? 100 : 500)) {
    OLED_UpdateDisplay(now);
}

if (hbt.cmd_ready) {
    Bluetooth_ProcessCommand();
}

if (now - last_bt >= BT_PERIOD_MS || hbt.immediate_upload) {
    if (huart1.gState == HAL_UART_STATE_READY) {
        Bluetooth_SendData();
    } else {
        Bluetooth_CheckTXStuck(now);
    }
}
```

### 5. 添加 EXTI 回调（main.c USER CODE 4）

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    KeyId id;
    switch (GPIO_Pin) {
        case GPIO_PIN_12: id = KEY_K1; break;
        case GPIO_PIN_13: id = KEY_K2; break;
        case GPIO_PIN_14: id = KEY_K3; break;
        case GPIO_PIN_15: id = KEY_K4; break;
        default: return;
    }
    Key_ISR(id);
}
```

### 6. 添加 USART1 IDLE 中断处理（stm32f1xx_it.c）

```c
void USART1_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart1);

    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);
        hbt.cmd_ready = 1;
    }
}
```

### 7. 添加系统滴答（TIM3 中断回调）

```c
volatile uint32_t sys_tick_ms = 0;

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM3) {
        sys_tick_ms++;
    }
}
```

## 需要额外开发的模块

以下模块需要从零开发（HVAC 项目无对应代码）：

| 模块 | 说明 |
|------|------|
| **HX711 驱动** | 称重传感器 ADC 驱动（24 位 Σ-Δ ADC） |
| **蜂鸣器控制** | GPIO 高低电平控制有源蜂鸣器 |
| **重量滤波** | 滑动平均滤波，去抖动 |

### 重量报警阈值

| 阈值 | 宏定义 | 含义 |
|------|--------|------|
| 超重 | WEIGHT_MAX = 30.0f | 超过量程 |
| 异常 | WEIGHT_MIN = 0.005f | 空载或传感器断线 |

## 已知陷阱（来自 HVAC 项目实战经验）

1. **GPIO_PIN_x 是位掩码**: 不要用 `PIN_12 + i`，必须用数组 `{PIN_12, PIN_13, PIN_14, PIN_15}`
2. **I2C BUSY 锁死**: STM32F1 I2C 有硬件缺陷，已内置 FORCE_RESET 恢复机制
3. **DMA TX/RX 不能混用阻塞模式**: 同一 UART 上混用会破坏 HAL 状态机
4. **TC 必须等待**: 不同长度帧连续发送时，不等 TC 则移位时序锁死
5. **看门狗复位后 static 变量不重置**: 所有需复位的数据必须放在全局结构体中
6. **蓝牙帧长超 256 字节会被模块丢弃**: 压缩状态字符串，控制字段数量

## 引脚速查表

| 外设 | 引脚 | 功能 |
|------|------|------|
| USART1 TX | PA9 | 蓝牙 RXD |
| USART1 RX | PA10 | 蓝牙 TXD |
| I2C1 SCL | PB6 | OLED SCL |
| I2C1 SDA | PB7 | OLED SDA |
| KEY0 | PB12 | 独立按键 1 |
| KEY1 | PB13 | 独立按键 2 |
| KEY2 | PB14 | 独立按键 3 |
| KEY3 | PB15 | 独立按键 4 |
| HX711 DT | 待定 | 称重传感器数据 |
| HX711 SCK | 待定 | 称重传感器时钟 |
| Buzzer | 待定 | 蜂鸣器 |

---

*从 HVAC 项目迁移至电子秤/POS 新项目*
*迁移日期：2026-05-02*
