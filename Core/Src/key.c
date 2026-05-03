/**
 * @file    key.c
 * @brief   4 路独立按键处理模块：KEY0-KEY3（PB12-PB15）
 *          EXTI 下降沿中断 + 10ms 周期扫描
 *          两级状态机：消抖 → 短按/长按检测
 *          事件环形缓冲区，支持高速消费
 *          电子秤/POS 系统
 *
 * 按键功能映射（三页面）：
 * ┌─────────┬────────────────────────┬──────────────────────┬────────────┐
 * │  按键   │  PAGE_WEIGHING         │  PAGE_PREVIEW        │ PAGE_ALARM │
 * ├─────────┼────────────────────────┼──────────────────────┼────────────┤
 * │ KEY0    │ 光标上移: 0→1→2→0     │ 选中→退出 / 未→重称  │ 退出报警   │
 * │ KEY1    │ 光标下移: 0→2→1→0     │ 编辑类→下一 / 价-0.5 │ 退出报警   │
 * │ KEY2    │ 未选中→结算 / 选→编辑  │ 保存，返回称量       │ 退出报警   │
 * │ KEY3    │ 未选中→重称 / 选→退出  │ 放弃，返回称量       │ 退出报警   │
 * └─────────┴────────────────────────┴──────────────────────┴────────────┘
 *
 * 此文件与 HVAC 项目的 key.c 逻辑完全相同，仅按键映射注释不同。
 * 实际按键行为由 OLED_HandleKey() 根据当前页面解释。
 */

#include "key.h"
#include "stm32f1xx.h"

/* 全局按键实例 */
Key_t hkey = {0};

/*
 * Key_Init —— 初始化按键模块
 * 清零所有状态：debounce_state、press_tick、last_debounce_tick、事件队列指针
 *
 * 注意：last_debounce_tick 必须放入结构体而非 static 局部变量，
 * 因为非电源复位（看门狗复位、软件复位）时 static 变量保持旧值，
 * 导致轮询回退机制永久阻塞。
 */
void Key_Init(void) {
    for (int i = 0; i < 4; i++) {
        hkey.debounce_state[i] = 0;
        hkey.press_tick[i] = 0;
        hkey.last_debounce_tick[i] = 0;
    }
    hkey.q_head = 0;
    hkey.q_tail = 0;
}

/*
 * Key_ISR —— 按键 EXTI 中断服务函数
 * @param id 触发中断的按键编号（KEY_K1~KEY_K4）
 *
 * 在中断中只记录时间戳 + 标记 debounce 状态，实际消抖在 Key_Scan() 中完成。
 * 由 HAL_GPIO_EXTI_Callback() 调用（main.c USER CODE 4）。
 */
void Key_ISR(KeyId id) {
    extern volatile uint32_t sys_tick_ms;
    hkey.press_tick[id] = sys_tick_ms;
    hkey.debounce_state[id] = 1;
}

/*
 * push_event —— 将事件压入环形缓冲
 * 队列满时静默丢弃（防止阻塞）
 */
static void push_event(uint8_t evt) {
    uint8_t next = (hkey.q_head + 1) % KEY_EVENT_QUEUE_SZ;
    if (next != hkey.q_tail) {
        hkey.event_queue[hkey.q_head] = evt;
        hkey.q_head = next;
    }
}

/*
 * Key_Scan —— 按键状态机，主循环每 10ms 调用一次
 *
 * 两级状态机：
 *   第 0 级：轮询回退 —— EXTI 未触发但引脚为 LOW 时，从这里启动消抖
 *   第 1 级：消抖确认 —— 等待 20ms 电平稳定 LOW
 *   第 2 级：事件判定 —— 释放=短按 / 超过 1s=长按
 *
 * 按键引脚映射表（使用数组而非 PIN_12+i，因为 GPIO_PIN_x 是位掩码）：
 *   PB12 → GPIO_PIN_12, PB13 → GPIO_PIN_13,
 *   PB14 → GPIO_PIN_14, PB15 → GPIO_PIN_15
 */
void Key_Scan(void) {
    extern volatile uint32_t sys_tick_ms;

    static const uint16_t key_pins[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};
    for (int i = 0; i < 4; i++) {
        GPIO_TypeDef *port = GPIOB;
        uint16_t pin = key_pins[i];
        GPIO_PinState pin_val = HAL_GPIO_ReadPin(port, pin);

        /* ── 第 0 级：轮询回退 ── */
        if (hkey.debounce_state[i] == 0 && hkey.last_debounce_tick[i] == 0) {
            if (pin_val == GPIO_PIN_RESET) {
                hkey.press_tick[i] = sys_tick_ms;
                hkey.debounce_state[i] = 1;
            }
        }

        if (!hkey.debounce_state[i]) continue;

        /* ── 第 1 级：消抖确认（20ms）── */
        if (hkey.last_debounce_tick[i] == 0) {
            if (sys_tick_ms - hkey.press_tick[i] >= KEY_DEBOUNCE_MS) {
                if (pin_val == GPIO_PIN_RESET) {
                    hkey.last_debounce_tick[i] = sys_tick_ms;
                } else {
                    hkey.debounce_state[i] = 0;
                }
            }
            continue;
        }

        /* ── 第 2 级：事件判定 ── */
        if (pin_val == GPIO_PIN_SET) {
            /* 按键释放：< 1s → 短按 */
            if (sys_tick_ms - hkey.last_debounce_tick[i] < KEY_LONGPRESS_MS) {
                push_event((i << 4) | KEY_EVT_SHORT);
            }
            hkey.debounce_state[i] = 0;
            hkey.last_debounce_tick[i] = 0;
        } else if (sys_tick_ms - hkey.last_debounce_tick[i] >= KEY_LONGPRESS_MS) {
            /* 持续按下 ≥ 1s → 长按 */
            push_event((i << 4) | KEY_EVT_LONG);
            hkey.debounce_state[i] = 0;
            hkey.last_debounce_tick[i] = 0;
        }
    }
}

/*
 * Key_GetEvent —— 从事件队列取出一个按键事件
 * @param id      [out] 按键编号
 * @param is_long [out] 0=短按, 1=长按
 * @return 1=成功取出, 0=队列为空
 */
uint8_t Key_GetEvent(KeyId *id, uint8_t *is_long) {
    if (hkey.q_tail == hkey.q_head) return 0;

    uint8_t evt = hkey.event_queue[hkey.q_tail];
    hkey.q_tail = (hkey.q_tail + 1) % KEY_EVENT_QUEUE_SZ;

    *id = (KeyId)(evt >> 4);
    uint8_t type = evt & 0x0F;
    *is_long = (type == KEY_EVT_LONG) ? 1 : 0;
    return 1;
}
