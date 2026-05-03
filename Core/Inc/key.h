/**
 * @file    key.h
 * @brief   按键模块头文件：4 路独立按键（K1-K4）
 *          支持短按/长按检测，事件队列，EXTI 中断触发 + 轮询回退
 */

#ifndef __KEY_H__
#define __KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== 按键时序参数 ========== */
#define KEY_DEBOUNCE_MS    20      /* 消抖时间（ms）：连续 20ms 稳定电平确认有效按下 */
#define KEY_LONGPRESS_MS   1000    /* 长按判定时间（ms）：按键按下超过 1 秒触发长按事件 */

#define KEY_EVENT_QUEUE_SZ 8       /* 事件队列深度：环形缓冲最多存放 8 个未处理事件 */

/*
 * KeyId —— 按键标识枚举
 * 对应硬件连接：PB12=K1, PB13=K2, PB14=K3, PB15=K4
 * 低电平有效（外部上拉，按下 → GPIO_PIN_RESET）
 */
typedef enum { KEY_K1 = 0, KEY_K2, KEY_K3, KEY_K4 } KeyId;

/*
 * KeyEvent —— 按键事件类型
 * KEY_EVT_NONE  = 无事件（队列空）
 * KEY_EVT_SHORT = 短按（按下 < 1 秒后释放）
 * KEY_EVT_LONG  = 长按（持续按下 ≥ 1 秒）
 */
typedef enum { KEY_EVT_NONE = 0, KEY_EVT_SHORT, KEY_EVT_LONG } KeyEvent;

/*
 * Key_t —— 按键模块状态结构体
 *
 * 事件队列编码方式：
 *   每个事件 1 字节：高 4 位 = key_id, 低 4 位 = event_type
 *   例如 K3 短按 = (2 << 4) | KEY_EVT_SHORT = 0x21
 *
 * event_queue[] = 环形事件缓冲
 * q_head        = 写入索引（生产者位置，ISR/Key_Scan 写入）
 * q_tail        = 读取索引（消费者位置，主循环读取）
 * press_tick[]  = 按键按下时的系统滴答（由 EXTI 中断或轮询记录）
 * last_debounce_tick[] = 消抖确认时间戳（以 ms 为单位的时间基准，由系统滴答计时）
 *                        移至结构体而非静态局部变量，确保 Key_Init() 可重置
 *                        解决非电源复位后静态变量保持旧值导致检测失效的问题
 * debounce_state[] = 去抖状态机阶段（0=空闲, 1=消抖中）
 */
typedef struct {
    uint8_t event_queue[KEY_EVENT_QUEUE_SZ];   /* 事件环形缓冲区 */
    uint8_t q_head;                              /* 队尾索引（写入端） */
    uint8_t q_tail;                              /* 队首索引（读取端） */
    uint32_t press_tick[4];                      /* 按键按下时间戳（ms） */
    uint32_t last_debounce_tick[4];              /* 消抖确认时间戳（ms） */
    uint8_t debounce_state[4];                   /* 消抖状态 */
} Key_t;

/* 全局按键实例 */
extern Key_t hkey;

/* 模块接口函数 */
void Key_Init(void);                                  /* 初始化按键模块 */
void Key_ISR(KeyId id);                                /* EXTI 中断服务（记录按下时间） */
void Key_Scan(void);                                   /* 10ms 周期扫描（消抖+事件生成） */
uint8_t Key_GetEvent(KeyId *id, uint8_t *is_long);     /* 从队列取出一个按键事件 */

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
