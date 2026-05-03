# 开发日志

## 2026-05-04 第三轮修正

### 一、按键引脚映射回 key_module_guide.md

将 key_pins 改回 `{PIN_12, PIN_13, PIN_14, PIN_15}`，EXTI 回调 PB12→KEY_K1 ... PB15→KEY_K4。
OLED_HandleKey 中的按键功能分配未变（原本就是正确的）。

### 二、重量抖动报警算法重写

**问题**：旧算法每 100ms 比较相邻采样，受传感器噪声和死区影响，3s 持续抖动 >50g 无法可靠触发。

**新算法**（[alarm.c](../Core/Src/alarm.c)）：
- 每 1s 取一个代表值（调用 10 次 × 100ms 后采样一次）
- 比较当前值与上一秒的差值
- 差值 >50g → `jitter_secs++`，连续 ≥3s → 报警
- 差值 ≤50g → `stable_secs++`，连续 ≥2s → 解除报警

### 三、蜂鸣器间歇时序修改

**问题**：退出报警页后蜂鸣器间歇鸣叫，原 300ms/300ms 节奏太密。

**修改**（[buzzer.c](../Core/Src/buzzer.c)）：
- 鸣叫时长：300ms → **1500ms（1.5s）**
- 停止时长：300ms → **3000ms（3s）**

### 四、单价修改持久化

**问题**：编辑商品单价后保存，切换水果再切回，修改的单价丢失。

**修改**（[oled.c](../Core/Src/oled.c)）：
- 进入编辑模式时记录 `saved_price = product_table[idx].default_price`
- KEY_K2 保存：`product_table[idx].default_price = holog.scale.unit_price`
- KEY_K1 放弃：`holog.scale.unit_price = saved_price`
- 切换商品时同步更新 `saved_price`
- `product_table` 去掉 `const` 修饰（[oled.h](../Core/Inc/oled.h) + [oled.c](../Core/Src/oled.c)）

### 五、字体数组维度更新

用户新增 2 个中文字模（索引 28/29），`g_chinese_fonts` 从 [28][32] 扩至 [30][32]。
同步更新 oled.c、driver_oled.c 中的 extern 声明和边界检查。

### 六、之前已完成的修改（本次未变）

| 项目 | 状态 |
|------|------|
| OLED 中文渲染 + 2 位小数 | ✅ |
| 商品中文名查表切换 | ✅ |
| 编辑闪烁仅数值 | ✅ |
| 报警页中文（超重! / 请重新测量）| ✅ |
| 超重阈值 2kg / 0.5s 确认 | ✅ |
| 蜂鸣器双频（3kHz / 6kHz）| ✅ |
| 死区前数据供报警检测 | ✅ |
| 重量 2dp 参与总价计算 | ✅ |

### 编译结果

- RAM: 3888 B / 20 KB (18.98%)
- FLASH: 34860 B / 64 KB (53.19%)
- 0 error，仅第三方 driver_oled.c 预存 warning
