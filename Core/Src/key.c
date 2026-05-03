/**
 * @file    key.c
 * @brief   4 路独立按键处理模块：K1-K4（PB12-PB15）
 *          EXTI 下降沿中断 + 10ms 周期扫描
 *          两级状态机：消抖 → 短按/长按检测
 *          事件环形缓冲区，支持高速消费
 */

#include "key.h"
#include "stm32f1xx.h"

/* 全局按键实例 */
Key_t hkey = {0};

/*
 * 事件编码格式：(key_id << 4) | event_type
 * 高 4 位 = 按键编号（0-3），低 4 位 = 事件类型（1=短按, 2=长按）
 *
 * 示例：
 *   K3 短按 = (2 << 4) | KEY_EVT_SHORT = 0x21
 *   K4 长按 = (3 << 4) | KEY_EVT_LONG  = 0x32
 *
 * 环形缓冲区实现：
 *   q_head = 写入位置（生产方式：Key_Scan 写入事件后 +1）
 *   q_tail = 读取位置（消费方式：Key_GetEvent 取出事件后 +1）
 *   队列满时丢弃新事件（head 追上 tail），避免阻塞
 */

/*
 * Key_Init —— 初始化按键模块
 * 清零所有状态：
 * - debounce_state[] = 0（空闲状态）
 * - press_tick[] = 0（按下时间清零）
 * - last_debounce_tick[] = 0（消抖完成时间清零）
 * - 事件队列指针重置
 *
 * 注意：last_debounce_tick 不能在 Key_Scan 中定义为 static 变量，
 * 因为静态局部变量在非电源复位（看门狗复位、软件复位）时保持旧值，
 * 导致轮询回退机制永久阻塞。详见 Readme/speak.md #5
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
 * 由 stm32f1xx_it.c 中的 EXTI15_10_IRQHandler → HAL_GPIO_EXTI_IRQHandler
 * → HAL_GPIO_EXTI_Callback 调用（见 main.c USER CODE 4）
 *
 * 功能：
 * 1. 记录当前系统滴答作为按键按下时间戳
 * 2. 设置 debounce_state = 1（启动消抖流程）
 *
 * 为什么中断中只记录时间而不做完整消抖？
 * - 消抖需要等待 20ms，在中断中等待会阻塞整个系统
 * - 中断服务应该尽量短，复杂逻辑放在 Key_Scan() 轮询中处理
 * - 记录时间戳并标记状态，实际消抖在 Key_Scan() 中完成
 */
void Key_ISR(KeyId id) {
    extern volatile uint32_t sys_tick_ms;
    hkey.press_tick[id] = sys_tick_ms;    /* 记录按下的精确时刻 */
    hkey.debounce_state[id] = 1;           /* 标记 debounce 开始 */
}

/*
 * push_event —— 将事件压入环形缓冲
 * @param evt 编码后的事件字节（高 4 位=key_id, 低 4 位=type）
 *
 * 入队策略：
 * - 计算下一个 head 位置（循环 +1）
 * - 如果 next == tail（队列满），丢弃事件，不覆盖旧数据
 * - 否则写入后更新 head
 */
static void push_event(uint8_t evt) {
    uint8_t next = (hkey.q_head + 1) % KEY_EVENT_QUEUE_SZ;
    if (next != hkey.q_tail) {            /* 队列未满 */
        hkey.event_queue[hkey.q_head] = evt;
        hkey.q_head = next;
    }
    /* 队列满时静默丢弃（防止阻塞） */
}

/*
 * Key_Scan —— 按键状态机，主循环每 10ms 调用一次
 *
 * ──── 工作机制 ────
 *
 * 采用两级状态机实现可靠按键检测：
 *
 * 第 0 级：轮询回退（Poll Fallback）
 *   EXTI 中断通常负责检测下降沿并启动 debounce，但如果系统中断负载高
 *   导致 EXTI 触发被延迟甚至丢失（读 pin 为 LOW 但 debounce_state=0），
 *   此机制从轮询路径自动启动 debounce 作为补充。
 *
 * 第 1 级：消抖确认（Debounce）
 *   等待 KEY_DEBOUNCE_MS（20ms）内电平保持稳定 LOW。
 *   20ms 后读取引脚状态：
 *   - 仍为 LOW  → 确认按下，记录消抖完成时间戳
 *   - 已恢复 HIGH → 噪声干扰，回退到空闲状态
 *
 * 第 2 级：事件判定（Release / Long-press）
 *   按键已消抖确认后的两个分支：
 *   a) 引脚恢复 HIGH（释放）：
 *      从消抖完成到释放的时间 < 1000ms → SHORT 事件
 *   b) 引脚持续 LOW 超过 1000ms：
 *      触发 LONG 事件，然后复位状态机（允许再次触发）
 *
 * ──── 时序图 ────
 *   t=0        EXTI 中断触发（或轮询检测到 LOW）
 *   t=20ms     消抖窗口结束，确认按键确实按下
 *   t=20~1020ms 等待释放或长按超时
 *   t=1020ms   仍然按住 → 触发 LONG 事件
 *   t<1020ms   释放 → 触发 SHORT 事件
 *
 * ──── 为什么使用 hkey.last_debounce_tick 而不是 static 变量 ────
 *   last_debounce_tick 原为 Key_Scan() 内部的 static 局部变量，
 *   Key_Init() 无法重置，导致以下场景失效：
 *   1. 按键按下 → 消抖完成 → last_debounce_tick 设为非零值
 *   2. I2C 卡死 → 看门狗复位 → SRAM 保持原值
 *   3. Key_Init() 重置了结构体中的字段，但 static 变量不变
 *   4. last_debounce_tick 指向未来时间 → 状态机跳过第 1 级 → 永久阻塞
 *
 *   移至 hkey.last_debounce_tick[] 后，Key_Init() 统一清零，解决此问题。
 */
void Key_Scan(void) {
    extern volatile uint32_t sys_tick_ms;

    /*
     * 按键引脚映射表：PB12~PB15
     * 使用数组而非 GPIO_PIN_12 + i 的原因是：
     * GPIO_PIN_x 在 HAL 中定义为 (1 << x)，GPIO_PIN_12 + 1 = 0x1001
     * 而非期望的 0x2000（GPIO_PIN_13），导致 K2-K4 读到错误的引脚
     * 这是之前 K2-K4 无反应的根因（详见 Readme/speak.md #1）
     */
    static const uint16_t key_pins[4] = {GPIO_PIN_12, GPIO_PIN_13, GPIO_PIN_14, GPIO_PIN_15};
    for (int i = 0; i < 4; i++) {
        GPIO_TypeDef *port = GPIOB;
        uint16_t pin = key_pins[i];
        GPIO_PinState pin_val = HAL_GPIO_ReadPin(port, pin);

        /* ── 第 0 级：轮询回退 ── */
        /* 如果引脚为 LOW 但 EXTI 没来得及触发 debounce，从这里启动 */
        if (hkey.debounce_state[i] == 0 && hkey.last_debounce_tick[i] == 0) {
            if (pin_val == GPIO_PIN_RESET) {
                hkey.press_tick[i] = sys_tick_ms;          /* 记录按下时刻 */
                hkey.debounce_state[i] = 1;                 /* 启动消抖 */
            }
        }

        /* 状态机当前处于空闲 → 跳过后续处理 */
        if (!hkey.debounce_state[i]) continue;

        /* ── 第 1 级：消抖确认 ── */
        if (hkey.last_debounce_tick[i] == 0) {
            /* 等待 KEY_DEBOUNCE_MS（20ms）消抖时间 */
            if (sys_tick_ms - hkey.press_tick[i] >= KEY_DEBOUNCE_MS) {
                if (pin_val == GPIO_PIN_RESET) {
                    /* 20ms 后引脚仍为 LOW → 确认是有效按键而非噪声 */
                    hkey.last_debounce_tick[i] = sys_tick_ms;   /* 记录消抖完成时间 */
                } else {
                    /* 20ms 内引脚恢复 HIGH → 判断为噪声干扰，状态回退 */
                    hkey.debounce_state[i] = 0;                  /* 空闲状态 */
                }
            }
            continue;
        }

        /* ── 第 2 级：事件判定 ── */
        if (pin_val == GPIO_PIN_SET) {
            /* 按键已释放：判断是否为短按 */
            if (sys_tick_ms - hkey.last_debounce_tick[i] < KEY_LONGPRESS_MS) {
                /* 消抖完成到释放的时间 < 1000ms → 短按 */
                push_event((i << 4) | KEY_EVT_SHORT);
            }
            /* 复位状态机，准备下一次按键检测 */
            hkey.debounce_state[i] = 0;
            hkey.last_debounce_tick[i] = 0;
        } else if (sys_tick_ms - hkey.last_debounce_tick[i] >= KEY_LONGPRESS_MS) {
            /* 按键按住超过 1000ms → 长按事件 */
            push_event((i << 4) | KEY_EVT_LONG);
            /* 状态机复位，允许同一次按住期间再次触发 */
            hkey.debounce_state[i] = 0;
            hkey.last_debounce_tick[i] = 0;
        }
    }
}

/*
 * Key_GetEvent —— 从事件队列取出一个按键事件
 * @param id      [out] 按键编号（KEY_K1~KEY_K4）
 * @param is_long [out] 事件类型（0=短按, 1=长按）
 * @return 1=成功取出事件, 0=队列为空
 *
 * 调用方式：在主循环中轮询，直到返回 0
 *   KeyId kid; uint8_t is_long;
 *   while (Key_GetEvent(&kid, &is_long)) { ... }
 */
uint8_t Key_GetEvent(KeyId *id, uint8_t *is_long) {
    /* 队列空：tail == head */
    if (hkey.q_tail == hkey.q_head) return 0;

    /* 从队首取出事件字节 */
    uint8_t evt = hkey.event_queue[hkey.q_tail];
    hkey.q_tail = (hkey.q_tail + 1) % KEY_EVENT_QUEUE_SZ;

    /* 解码：高 4 位 = key_id, 低 4 位 = event_type */
    *id = (KeyId)(evt >> 4);
    uint8_t type = evt & 0x0F;
    *is_long = (type == KEY_EVT_LONG) ? 1 : 0;
    return 1;
}
