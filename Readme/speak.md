# 项目问题记录与解决方案

## 1. 按键 GPIO 引脚掩码计算错误

**问题描述：** 只有 K1 按键正常，K2、K3、K4 按键无反应。

**错误原因：** 按键扫描函数 Key_Sacn() 使用 `GPIO_PIN_12 + i` 计算引脚掩码。GPIO_PIN_x 在 STM32 HAL 库中定义为 `(1 << x)`，因此 GPIO_PIN_12 + 1 = 0x1001（即 GPIO_PIN_0 | GPIO_PIN_12），而非期望的 GPIO_PIN_13。这导致 K2-K4 读取到错误的 GPIO 引脚状态。

**修复方案：** 使用静态常量数组存储正确的引脚号：
```c
static const uint16_t key_pins[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};
```
通过数组索引访问对应引脚，K1=PB12、K2=PB13、K3=PB14、K4=PB15。

**涉及文件：** Core/Src/key.c

---

## 2. PAGE_ALARM 页面自动返回问题

**问题描述：** 通过长按按键手动导航到第三页（报警/状态页面）时，页面短暂停留后自动跳回之前的页面。

**错误原因：** OLED_HandleKey() 中 PAGE_ALARM 的 case 无条件执行 Alarm_Acknowledge() 并返回 prev_page，即任何按键操作都会触发返回。未区分"报警自动跳入"和"用户手动导航"两种场景。

**修复方案：** 在 PAGE_ALARM 处理逻辑中增加条件判断，仅在报警激活状态（halarm.alarm_active == 1）时执行确认和返回操作，手动导航时不自动返回：
```c
case PAGE_ALARM:
    if (halarm.alarm_active) {
        Alarm_Acknowledge();
        holog.current_page = holog.prev_page;
    }
    break;
```

**涉及文件：** Core/Src/oled.c

---

## 3. Alarm_Process 错误覆盖 prev_page

**问题描述：** 当用户已经手动导航到 PAGE_ALARM 页面时，报警触发会错误地将 prev_page 覆盖为 PAGE_ALARM，导致报警确认后跳转到错误页面（或卡死在报警页面）。

**错误原因：** Alarm_Process() 中无条件执行 `holog.prev_page = holog.current_page`，未检查用户当前是否已在报警页面。

**修复方案：** 仅在用户不在报警页面时保存 prev_page：
```c
if (holog.current_page != PAGE_ALARM)
    holog.prev_page = holog.current_page;
```

**涉及文件：** Core/Src/alarm.c

---

## 4. I2C 总线静默失败导致显示卡死

**问题描述：** 系统运行一段时间后 OLED 显示卡死，数据不再更新，但心跳灯继续闪烁。按键操作无效，但通过 LED 诊断发现按键检测也已停止工作。

**错误原因（显示部分）：** STM32F1 系列 I2C 外设存在硬件缺陷，当 I2C 总线上的传输被意外中断时（如中断优先级冲突、DMA 竞争），I2C 状态寄存器会进入 BUSY 状态且无法自动清除。OLED 驱动函数（OLED_WriteNBytes）忽略所有 HAL_I2C_Mem_Write 的返回值，即使总线已锁定也继续执行，导致显示数据无法到达 OLED，界面看似"卡死"。

**修复方案：**
1. 增加 I2C 总线探测函数 _oled_probe()：发送一个命令字节并检查返回值
2. 增加 OLED_ForceRecover()：执行完整的外设复位 → 重新初始化 → OLED 重新初始化
3. 每次刷新前探测总线，发现异常立即恢复
4. 添加定期强制复位机制（每 200 次刷新 ≈ 100 秒执行一次完整恢复）

**涉及文件：** Core/Src/oled.c

---

## 5. 按键静态变量导致看门狗/软件复位后按键检测永久失效

**问题描述：** 系统运行中随机出现按键无反应的故障，且一旦出现就会持续到下次完全断电重启。用 LED 诊断确认按键检测完全停止（LED 不再随按键突变）。

**错误原因：** Key_Scan() 中使用 `static uint32_t last_debounce_tick[4]` 静态局部变量记录按键消抖完成时间。当系统发生非电源复位（如 I2C 卡死后触发看门狗复位、软件复位、调试器复位）时，SRAM 数据保持原值，静态变量不会被清零。如果 reset 前 last_debounce_tick[i] 为非零值（表示按键已确认），复位后该值导致按键检测状态机认为按键仍在消抖完成状态，轮询回退机制被阻塞，无法重新检测按键按下。而 Key_Init() 只能重置结构体成员，无法重置函数内部的静态局部变量。

**修复方案：** 将 last_debounce_tick 从 Key_Scan() 的静态局部变量移到 Key_t 结构体中，并在 Key_Init() 中统一清零：
```c
// key.h - Key_t 结构体
typedef struct {
    ...
    uint32_t last_debounce_tick[4];  // 移至结构体
} Key_t;

// key.c - Key_Init()
void Key_Init(void) {
    for (int i = 0; i < 4; i++) {
        hkey.last_debounce_tick[i] = 0;  // 可被重置
        ...
    }
}
```

**涉及文件：** Core/Src/key.c、Core/Inc/key.h

---

## 6. ADC DMA 转换完成回调未实现

**问题描述：** ADC 的 DMA 循环模式启动后，传感器数据始终为 0。

**错误原因：** HAL_ADC_ConvCpltCallback() 回调函数未在 stm32f1xx_it.c 中实现，DMA 传输完成后没有通知应用程序读取数据。ADC 使用 DMA 循环模式连续转换两个通道（温度 + 浊度），但转换完成标志从未设置。

**修复方案：** 在 stm32f1xx_it.c 的 USER CODE 中添加回调实现：
```c
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    if (hadc->Instance == ADC1) {
        hsensor.dma_flag = 1;
    }
}
```

**涉及文件：** Core/Src/stm32f1xx_it.c

---

## 7. EXTI15_10_IRQHandler 未实现导致按键触发 HardFault

**问题描述：** 按下任意按键后系统进入 HardFault。

**错误原因：** STM32CubeMX 配置了 PB12-PB15 为 EXTI 中断模式，但未在 stm32f1xx_it.c 中生成 EXTI15_10_IRQHandler。中断向量表中的对应条目指向 Default_Handler，导致中断触发时进入无限循环。

**修复方案：** 添加中断处理函数，调用 HAL 库的 EXTI 中断处理函数：
```c
void EXTI15_10_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}
```

**涉及文件：** Core/Src/stm32f1xx_it.c

---

## 8. USART1 IDLE 中断未启用导致蓝牙命令无响应

**问题描述：** 手机 APP 发送命令到蓝牙模块后系统无响应。

**错误原因：** USART1 的 IDLE 线中断未启用，蓝牙模块发送的 JSON 数据到达后，系统无法检测到数据帧结束（IDLE 信号），cmd_ready 标志始终为 0，Bluetooth_ProcessCommand() 永远不会执行。

**修复方案：** 在 Bluetooth_Init() 中启用 IDLE 中断：
```c
__HAL_UART_CLEAR_IDLEFLAG(&huart1);
__HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
```
在 USART1_IRQHandler 中检测 IDLE 标志：
```c
if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)) {
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    hbt.cmd_ready = 1;
}
```

**涉及文件：** Core/Src/bluetooth.c、Core/Src/stm32f1xx_it.c

---

## 9. USART TX DMA 模式设置错误

**问题描述：** 蓝牙或语音模块发送数据时，第一次发送正常，后续发送卡死。

**错误原因：** CubeMX 生成的 DMA 初始化代码将 USART1_TX 和 USART2_TX 的 DMA 通道模式设置为 CIRCULAR（循环模式）。但应用程序中使用 DMA 发送是一次性操作（调用 HAL_UART_Transmit_DMA），循环模式在传输完成后不会自动停止，导致后续传输无法启动。

**修复方案：** 在 HAL_UART_MspInit() 中将 TX DMA 通道改为 NORMAL 模式：
```c
/* Fix: TX DMA must be NORMAL mode for one-shot transmits */
hdma_usart1_tx.Init.Mode = DMA_NORMAL;
HAL_DMA_Init(&hdma_usart1_tx);
```

**涉及文件：** Core/Src/usart.c

---

## 10. 启动画面显示异常（花屏）

**问题描述：** 上电后 OLED 启动画面出现随机花屏/乱码。

**错误原因：** 键盘初始化（Key_Init()）在 OLED 初始化之前执行，此外设间的相互影响导致 I2C 总线出现异常状态。

**修复方案：** 调整 main.c 中的初始化顺序：确保 OLED 初始化先于按键初始化。将 OLED_Setup() 和 Boot_Interface_Show() 放在 Key_Init() 之前执行。

**涉及文件：** Core/Src/main.c

---

## 11. 步进电机档位范围不一致

**问题描述：** OLED 页面可设置的档位范围为 0-5，但 Stepper_SetGear() 的档位判定逻辑与实际设置不一致，且步进电机目标位置计算与手动设置的预期不符。

**错误原因：** 代码中多处使用不同的限值常量（STEPPER_MAX_GEAR、GEAR_MAX、宏定义中的局部最大值等），未统一为单一数据来源。

**修复方案：** 统一使用 STEPPER_MAX_GEAR 作为唯一档位上限，删除重复的 GEAR_MAX 宏定义，确保 OLED 编辑页面和 Stepper_SetGear() 使用相同的限值逻辑。

**涉及文件：** Core/Src/stepper.c、Core/Inc/oled.h

---

## 12. PAGE_DATA 编辑模式 K3 按键 handler 被误删

**问题描述：** Page1 手动模式 Setting 编辑模式下，K3/K4 无法调节档位值。K3 无任何响应，K4 正常。同时 K1/K2 退出编辑模式时会错误地减小档位值或预设温度。

**错误原因：** 之前使用 sed 批量修改 oled.c 时代码行号偏移，执行 `sed -i '442,448d'` 误删了 PAGE_DATA edit mode 中的 `else if (id == KEY_K3)` 处理分支和 `if (holog.selected_item == 1)` 条件判断。导致：
1. K3 handler 完全丢失 → K3 按键在编辑模式下无任何作用
2. 递减代码（preset_temp -= 0.5f, water_valve--）被合并到 K1/K2 处理块中 → 每次退出编辑模式时错误地修改了数值

**修复方案：** 恢复 PAGE_DATA edit mode 的完整按键处理结构：
```c
if (id == KEY_K1 || id == KEY_K2) {
    holog.edit_mode = 0;  // 仅退出编辑，不修改任何值
} else if (id == KEY_K3) {
    // 递减操作
    if (holog.selected_item == 1) preset_temp -= 0.5f;
    else if (holog.selected_item == 2 && 手动模式) water_valve--, Stepper_SetGear();
} else if (id == KEY_K4) {
    // 递增操作
    if (holog.selected_item == 1) preset_temp += 0.5f;
    else if (holog.selected_item == 2 && 手动模式) water_valve++, Stepper_SetGear();
}
```

**涉及文件：** Core/Src/oled.c

---

## 13. 步进电机相位表 GPIO 位错误

**问题描述：** 步进电机全程不工作，无论设置什么档位，电机没有任何动作。

**错误原因：** 28BYJ-48 步进电机的 4 相（A-D）通过 ULN2003 驱动，连接在 STM32 的 PA4-PA7 引脚（对应 GPIO 端口的 bit 4-7）。但 step_table 中的相位值使用 bit 0-3（如 0x08 = PA3, 0x0C = PA3|PA2），且 ODR 掩码 `0xFFF0` 只清除了 PA0-PA3，保留了 PA4-PA7 的原始值。导致 GPIO 输出从未正确驱动电机线圈，电机无法获得正确的励磁时序。

具体对照：
- 错误：`static const uint8_t step_table[8] = {0x08, 0x0C, 0x04, 0x06, 0x02, 0x03, 0x01, 0x09};`
  - 值在 bit 0-3，对应 PA0-PA3（未连接电机）
  - ODR 掩码 `0xFFF0` 清除了 PA0-PA3，保留了 PA4-PA7
- 正确：`static const uint8_t step_table[8] = {0x10, 0x30, 0x20, 0x60, 0x40, 0xC0, 0x80, 0x90};`
  - 值在 bit 4-7，对应 PA4-PA7（连接电机 A-D 相）
  - ODR 掩码 `0xFF0F` 清除了 PA4-PA7，保留了 PA0-PA3

**修复方案：** 
1. step_table 改为使用 bit 4-7 的值：`{0x10, 0x30, 0x20, 0x60, 0x40, 0xC0, 0x80, 0x90}`
2. ODR 掩码从 `0xFFF0` 改为 `0xFF0F`，正确清除 PA4-PA7 并保留 PA0-PA3

**涉及文件：** Core/Src/stepper.c

---

## 14. Alarm_Process 使用 static 局部变量导致非电源复位后时序混乱

**问题描述：** 系统发生看门狗复位或软件复位后，报警检测时序可能异常。

**错误原因：** Alarm_Process() 中使用 `static uint32_t last_check = 0;` 记录最后一次检测时间戳。非电源复位（看门狗、NVIC_SystemReset）时，SRAM 保留旧值，虽然启动代码会重新初始化 `.data` 段，但在某些复位场景（快速复位、掉电不彻底）下静态局部变量可能保持复位前的值。若 last_check 在复位前为较大值，复位后 sys_tick_ms 从 0 重新计数，`now - last_check` 大数减小数产生正确值（无符号回绕后很大），不会阻挡检测；但如果 last_check 恰好为 0（刚初始化），复位后 now 也为 0，则首次检测不会跳过。然而该变量属于函数内部状态，Init 函数无法重置，是设计缺陷。

**修复方案：** 将 last_check 从函数内部的 static 局部变量移至 Alarm_t 结构体，在 Alarm_Init() 中初始化：
```c
// alarm.h — Alarm_t 结构体增加字段
uint32_t last_check;

// alarm.c — Alarm_Init() 中初始化
halarm.last_check = 0;

// alarm.c — Alarm_Process() 中使用
if (now - halarm.last_check < 100) return;
halarm.last_check = now;
```

**涉及文件：** Core/Src/alarm.c、Core/Inc/alarm.h

---

## 15. OLED_UpdateDisplay 使用 static 局部变量导致非电源复位后刷新异常

**问题描述：** 系统复位后 OLED 显示刷新时序可能异常，表现为显示卡死或刷新率异常。

**错误原因：** OLED_UpdateDisplay() 中使用 `static uint32_t last_render = 0;` 和 `static uint16_t flush_count = 0;` 控制刷新率和定期 I2C 恢复。与 Bug #5 和 Bug #14 同类问题——非电源复位后这些静态局部变量保留旧值，`last_render` 可能指向未来的时间点（复位前的时间戳），导致 `now - last_render` 无符号回绕后虽不会阻挡渲染，但时序不可预测。更重要的是这些是函数内部状态，Init 函数无法重置。

**修复方案：** 将 last_render 和 flush_count 移至 OLED_t 结构体，在 OLED_Setup() 中初始化：
```c
// oled.h — OLED_t 结构体增加字段
uint32_t last_render;
uint16_t flush_count;

// oled.c — OLED_Setup() 中初始化
holog.last_render = 0;
holog.flush_count = 0;

// oled.c — OLED_UpdateDisplay() 中使用
if (now - holog.last_render < interval) return;
holog.last_render = now;
if (++holog.flush_count >= 200) {
    holog.flush_count = 0;
    OLED_ForceRecover();
}
```

**涉及文件：** Core/Src/oled.c、Core/Inc/oled.h

---

## 16. PAGE_SETTINGS 浏览模式缺少 K4 向上导航

**问题描述：** Page2（设置页）浏览模式下 K4 按键无响应，无法向上选择项目。注释中写明"K4=up"但未实现。

**错误原因：** OLED_HandleKey() 中 PAGE_SETTINGS 浏览模式只有 K2 和 K3 的处理分支，缺少 `else if (id == KEY_K4)` 分支。K4 按键被静默忽略。

**修复方案：** 添加 K4 处理分支，实现向上循环导航：
```c
} else if (id == KEY_K4) {
    if (holog.selected_item <= 1) holog.selected_item = 4;  /* 0→4, 1→4(回绕) */
    else holog.selected_item--;                               /* 4→3, 3→2, 2→1 */
}
```

**涉及文件：** Core/Src/oled.c

---

## 17. K3/K4 长按翻页方向互换需求

**问题描述：** K3 长按默认为上一页，K4 长按为下一页，用户要求互换方向（K3=下一页，K4=上一页）。

**错误原因：** 用户习惯——K4 在大多数界面中代表"向右/向前"，应导航到下一页；K3 代表"向左/向后"，应导航到上一页。原实现方向相反。

**修复方案：** 交换 OLED_HandleKey() 中长按处理的 K3/K4 代码块：
```c
if (id == KEY_K3) {
    /* next page: DATA→SETTINGS→ALARM→DATA... */
    holog.current_page = (holog.current_page == PAGE_ALARM) ? PAGE_DATA :
                         (holog.current_page + 1);
} else if (id == KEY_K4) {
    /* prev page: DATA→ALARM→SETTINGS→DATA... */
    holog.current_page = (holog.current_page == PAGE_DATA) ? PAGE_ALARM :
                         (holog.current_page - 1);
}
```

**涉及文件：** Core/Src/oled.c

---

## 总结

| 序号 | 问题 | 类型 | 严重程度 | 是否解决 |
|------|------|------|---------|---------|
| 1 | GPIO 引脚掩码计算错误 | 逻辑错误 | 高 | 已解决 |
| 2 | PAGE_ALARM 自动返回 | 逻辑错误 | 中 | 已解决 |
| 3 | prev_page 被错误覆盖 | 逻辑错误 | 中 | 已解决 |
| 4 | I2C 总线锁定导致显示卡死 | 硬件缺陷/容错 | 高 | 已解决 |
| 5 | 按键静态变量复位后失效 | 状态管理错误 | 高 | 已解决 |
| 6 | ADC DMA 回调未实现 | 功能缺失 | 高 | 已解决 |
| 7 | EXTI 中断处理缺失 | 功能缺失 | 高 | 已解决 |
| 8 | USART1 IDLE 中断未启用 | 功能缺失 | 高 | 已解决 |
| 9 | TX DMA 模式错误 | 配置错误 | 中 | 已解决 |
| 10 | 启动画面花屏 | 初始化顺序 | 低 | 已解决 |
| 11 | 档位范围不一致 | 常量管理 | 低 | 已解决 |
| 12 | PAGE_DATA 编辑模式 K3 handler 被误删 | 代码编辑错误 | 高 | 已解决 |
| 13 | 步进电机相位表 GPIO 位错误 | 逻辑错误 | 高 | 已解决 |
| 14 | Alarm_Process static 变量问题 | 状态管理错误 | 中 | 已解决 |
| 15 | OLED_UpdateDisplay static 变量问题 | 状态管理错误 | 中 | 已解决 |
| 16 | PAGE_SETTINGS 浏览模式无 K4 向上 | 功能缺失 | 中 | 已解决 |
| 17 | K3/K4 长按翻页方向互换 | 交互调整 | 低 | 已解决 |
