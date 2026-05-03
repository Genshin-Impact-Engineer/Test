# 生鲜结算系统开发进度

## 开发日期
2026-05-03

## 开发目的
基于设计方案（设计方案.md、oled_module_guide.md、key_module_guide.md），完成STM32F103C8T6生鲜电子秤结算系统的核心功能集成：OLED三页面显示、HX711数据称量、4路独立按键操作、无源蜂鸣器提示/报警、LED心跳灯。

## 实现内容

### 1. 系统时基与中断服务（stm32f1xx_it.c）
- 新增全局变量 `sys_tick_ms`（1ms 时基计数器），在 SysTick_Handler 中递增
- 新增 `task_flag_10ms` 和 `task_flag_100ms` 任务标志位，用于主循环周期性任务调度
- 新增 `HAL_GPIO_EXTI_Callback` 回调函数，将 PB12-PB15 的 EXTI 中断分发到 Key_ISR()

### 2. OLED 三页面显示系统（oled.h / oled.c）
- **Page_one 称重页**：显示商品类别、单价、重量、总价四行信息；支持光标在「未选中→类别→单价」之间循环；未选中时 KEY3 结算进入预览页、KEY4 重称；选中时 KEY3 进入编辑模式
- **Page_two 预览编辑页**：显示 [Preview]/[Edit] 状态、商品类别、单价、预览总价；编辑模式下闪烁指示当前选中项（300ms 周期）；支持切换商品/调整单价（±0.5元）；KEY3 保存返回 / KEY4 取消返回
- **Page_three 故障报警页**：超重显示 "Overweight! Remove items"；重量异常显示 "Weight Error! Please re-weigh"；任意按键退出报警返回前一页面
- 商品表更新为7种水果：苹果(8.0)、香蕉(7.0)、菠萝(8.0)、葡萄(6.0)、西瓜(4.0)、梨子(12.0)、哈密瓜(12.0)
- 帧缓冲渲染 + I2C 故障恢复（主动探测 + 定期强制复位）
- 刷新率：正常模式 500ms，编辑模式 300ms（与闪烁同步）

### 3. 按键检测与处理（key.h / key.c）
- 4路独立按键 KEY1(PB12)、KEY2(PB13)、KEY3(PB14)、KEY4(PB15)，低电平有效
- EXTI 下降沿中断 + 20ms 软件消抖 + 轮询回退三重保障
- 两级状态机：消抖确认 → 短按/长按判定（1s 阈值）
- 环形事件队列（8 槽位），支持高速消费不丢键

### 4. HX711 称重传感器数据采集（sensor.h / sensor.c）
- 自定义串行接口驱动（PA6=SCK, PA7=DOUT），24bit 数据读取
- 开机自动调零：20 次采样 → 排序 → 去首尾各 3 → 均值作为零点偏移
- 滑动均值降噪（4 样本缓冲）+ 自适应一阶滞后滤波（变化大 α=0.5 快速跟踪，稳定 α=0.1 强力平滑）
- 输出死区 2g，抑制尾数跳变

### 5. 主循环集成（main.c）
- **10ms 周期任务**：Key_Scan() 按键扫描
- **100ms 周期任务**：Sensor_Update() 传感器采样 → 更新 OLED 重量/总价 → Weight_CheckAlarm() 报警检测
- **按键事件消费**：Key_GetEvent() 出队 → Buzzer_ShortBeep() 提示音 → OLED_HandleKey() 页面分发
- **OLED 显示更新**：OLED_UpdateDisplay() 内部节流渲染
- **蜂鸣器状态机**：非阻塞 FSM，支持 50ms 短促提示音（按键反馈）和 300ms 间歇报警音（鸣300ms/停300ms 循环）；报警优先级高于按键提示音
- **LED 心跳灯**：PC13 引脚，正常 500ms 翻转（1Hz），报警时 200ms 翻转（2.5Hz）

### 6. 异常检测与报警
- **超重检测**：重量 > 30.0kg → 触发 PAGE_ALARM，蜂鸣器间歇鸣叫
- **重量不稳检测**：物品放置后 5 秒内重量持续抖动（相邻采样波动 ≥ 5g）→ 触发 PAGE_ALARM
- 任意按键退出报警页，停止蜂鸣器，返回先前页面

## 修改文件清单

| 文件 | 修改类型 | 说明 |
|------|---------|------|
| Core/Src/stm32f1xx_it.c | 修改 | 新增 sys_tick_ms、task_flag、EXTI callback |
| Core/Inc/oled.h | 修改 | PRODUCT_COUNT 改为 7 |
| Core/Src/oled.c | 修改 | 更新商品表、修复缓冲区大小、添加 default case |
| Core/Src/main.c | 重写 | 集成 OLED + Key + Sensor + Buzzer + LED 主循环 |
| CMakeLists.txt | 修改 | 添加 oled.c、key.c 到编译 |

## 编译结果
- 编译器：arm-none-eabi-gcc 13.3.1
- RAM: 3760 B / 20 KB (18.36%)
- FLASH: 32472 B / 64 KB (49.55%)
- 0 error, 0 warning（clean build）
