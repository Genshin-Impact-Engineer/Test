# 项目缺陷检查与修正记录

检查日期：2026-05-02
检查范围：设计方案.md 对照 main.c、oled.c/h、bluetooth.c/h、key.c/h、stm32f1xx_it.c 等全部 Core 源码

---

## 一、主程序空实现（后期实现）

**问题**：main.c 的 while(1) 主循环完全为空，未调用任何模块函数（Key_Scan、OLED_UpdateDisplay、Bluetooth_SendData/ProcessCommand 等）。

**处理**：后期实现，将各模块串联到主循环时间片中。

---

## 二、缺失 HX711 驱动（后期实现）

**问题**：设计方案核心传感器 HX711 无任何驱动代码，缺少：SCK/DOUT 时序模拟、24位数据读取、一阶滞后滤波（α=0.3）、重量稳定判定（连续10次波动<5g）。

**说明**：PA6(SCK/推挽输出) 和 PA7(DOUT/上拉输入) 已在 MX_GPIO_Init 中正确配置，仅缺应用层驱动。

**处理**：后期实现。

---

## 三、缺失 ASR-PRO 语音模块驱动（后期实现）

**问题**：UART2(PA2/PA3) 硬件已初始化，但无应用层语音指令发送逻辑（结算播报、调整完成、商品过重、重量异常等）。

**处理**：后期实现。

---

## 四、缺失蜂鸣器控制逻辑（后期实现）

**问题**：PA0 已配置为推挽输出，但无控制函数（按键提示音 50ms 短鸣、报警间歇鸣叫 300ms/300ms 循环、报警优先级高于按键提示音）。

**处理**：后期实现。

---

## 五、sys_tick_ms 时钟链路未实现（后期实现）

**问题**：key.c 和 bluetooth.c 中引用 `extern volatile uint32_t sys_tick_ms`，但该变量未被定义、未被初始化、未被递增。TIM2 已初始化但未调用 HAL_TIM_Base_Start_IT() 启动。USART1 IDLE 中断未在 stm32f1xx_it.c 中实现帧检测。HAL_GPIO_EXTI_Callback 未在 main.c 中重写，按键中断无法传递到 Key_ISR()。

**处理**：后期实现。需要在 main.c 中：
- 定义 `volatile uint32_t sys_tick_ms = 0`
- 在 HAL_TIM_PeriodElapsedCallback 中递增 sys_tick_ms
- 启动 TIM2 中断
- 实现 USART1_IRQHandler 中的 IDLE 帧检测
- 重写 HAL_GPIO_EXTI_Callback 分发按键事件

---

## 六、i2c.h 头文件缺失（已修正）

**问题**：oled.c 第17行 `#include "i2c.h"`，项目中无此文件。

**处理**：已更新（用户确认）。

---

## 七、按键物理映射（以代码为准）

**说明**：设计方案中 KEY1~KEY4 的编号顺序（PB15→KEY1, PB12→KEY4）与代码中 KeyId 枚举（KEY_K1=0→PB12, KEY_K4=3→PB15）呈镜像关系。物理引脚未变，按键功能在 OLED_HandleKey() 中分配正确。

**处理**：以代码为准，设计方案中的按键编号仅供参考。物理功能对应如下：

| 物理引脚 | 代码 KeyId | 功能 |
|---------|-----------|------|
| PB12 | KEY_K1 | 光标上移（设计方案 KEY1 功能）|
| PB13 | KEY_K2 | 光标下移（设计方案 KEY2 功能）|
| PB14 | KEY_K3 | 结算/确认（设计方案 KEY3 功能）|
| PB15 | KEY_K4 | 重称/取消（设计方案 KEY4 功能）|

---

## 八、Apple 默认单价不一致（已修正）

**问题**：设计方案第4.4节要求默认苹果单价 7.00 元/kg，代码 product_table 中 Apple 单价为 5.0f。

**修正**：oled.c 第32行 Apple 默认单价由 `5.0f` 改为 `7.0f`。✅

---

## 九、报警逻辑偏差（后期补充）

**说明**：设计方案强调"超过量程"和"5秒内重量严重抖动"两种报警条件，当前代码只实现了静态阈值（WEIGHT_MAX=30.0kg, WEIGHT_MIN=0.005kg），缺少长时间抖动超时检测。

**处理**：后期补充重量稳定超时检测逻辑。

---

## 十、文档内部 I2C 引脚矛盾（已修正）

**问题**：oled_module_guide.md 和 migration_guide.md 中记载 I2C1 引脚为 PB6/PB7（HVAC 项目旧值），但设计方案和实际代码均为 PB8/PB9。

**修正**：
- oled_module_guide.md：SCL=PB6→PB8, SDA=PB7→PB9 ✅
- migration_guide.md 引脚速查表：PB6→PB8, PB7→PB9 ✅
- migration_guide.md HX711 引脚由"待定"更新为 PA6(SCK)/PA7(DOUT) ✅

---

## 十一、I2C 时钟频率（用户已修正）

**说明**：原配置 400kHz，migration_guide 建议 100kHz 以避免 STM32F1 I2C BUSY 锁死。

**处理**：用户已自行更新。✅

---

## 十二、PA6 引脚分析（无误）

**说明**：PA6 在 MX_GPIO_Init 中配置为推挽输出，结合 HX711.md 要求（PD_SCK → 推挽输出 GPIO，DOUT → 上拉输入 GPIO）以及 PA7 的上拉输入配置，确认：

| 引脚 | 方向 | 用途 |
|------|------|------|
| PA6 | 推挽输出 | HX711 SCK（时钟） |
| PA7 | 上拉输入 | HX711 DOUT（数据） |

**结论**：PA6 配置正确，无错误。✅

---

## 修正汇总

| 编号 | 类别 | 状态 |
|------|------|------|
| 一~五 | 主循环/时钟/HX711/语音/蜂鸣器 | 后期实现 |
| 六 | i2c.h 缺失 | 已修正 |
| 七 | 按键映射 | 以代码为准 |
| 八 | Apple 默认单价 5.0→7.0 | 已修正 |
| 九 | 报警逻辑补充 | 后期实现 |
| 十 | 文档引脚更新 | 已修正 |
| 十一 | I2C 时钟频率 | 已修正 |
| 十二 | PA6 误初始化嫌疑 | 确认无误 |
