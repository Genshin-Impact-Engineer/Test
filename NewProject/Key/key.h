/**
 * @file    key.h
 * @brief   按键模块头文件：4 路独立按键（KEY0-KEY3）
 *          支持短按/长按检测，事件队列，EXTI 中断触发 + 轮询回退
 *          电子秤/POS 系统
 *
 * 硬件连接（与 HVAC 项目完全相同）：
 *   KEY0 = PB12, KEY1 = PB13, KEY2 = PB14, KEY3 = PB15
 *   低电平有效（外部上拉，按下 → GPIO_PIN_RESET）
 */

#ifndef __KEY_H__
#define __KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== 按键时序参数 ========== */
#define KEY_DEBOUNCE_MS    20      /* 消抖时间（ms） */
#define KEY_LONGPRESS_MS   1000    /* 长按判定时间（ms） */
#define KEY_EVENT_QUEUE_SZ 8       /* 事件队列深度 */

/*
 * KeyId —— 按键标识枚举
 * KEY0 = PB12, KEY1 = PB13, KEY2 = PB14, KEY3 = PB15
 */
typedef enum { KEY_K1 = 0, KEY_K2, KEY_K3, KEY_K4 } KeyId;

/*
 * KeyEvent —— 按键事件类型
 */
typedef enum { KEY_EVT_NONE = 0, KEY_EVT_SHORT, KEY_EVT_LONG } KeyEvent;

/*
 * Key_t —— 按键模块状态结构体
 *
 * 事件队列编码方式：
 *   每个事件 1 字节：高 4 位 = key_id, 低 4 位 = event_type
 *   K3 短按 = (2 << 4) | KEY_EVT_SHORT = 0x21
 */
typedef struct {
    uint8_t event_queue[KEY_EVENT_QUEUE_SZ];
    uint8_t q_head;
    uint8_t q_tail;
    uint32_t press_tick[4];
    uint32_t last_debounce_tick[4];
    uint8_t debounce_state[4];
} Key_t;

/* 全局按键实例 */
extern Key_t hkey;

/* 模块接口函数 */
void Key_Init(void);
void Key_ISR(KeyId id);
void Key_Scan(void);
uint8_t Key_GetEvent(KeyId *id, uint8_t *is_long);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
