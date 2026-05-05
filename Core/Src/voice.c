/**
 * @file    voice.c
 * @brief   ASR-PRO 语音播报实现（UART2, 115200）
 *
 * 指令集（对齐 ASR-PRO.md）：
 *   completed / cancelled / Tare / Tare Off / Measure Complete：单次
 *   Overweight / Weight abnormal：报警页每 3s，退出后蜂鸣器同步
 */
#include "voice.h"
#include "buzzer.h"
#include "usart.h"
#include <string.h>

#define REPEAT_MS  3000

static uint32_t last_send_ms = 0;
static uint8_t  completed_flag      = 0;
static uint8_t  cancelled_flag      = 0;
static uint8_t  tare_flag           = 0;
static uint8_t  tareoff_flag        = 0;
static uint8_t  measure_done_flag   = 0;
static uint8_t  start_adjust_flag   = 0;

static void send_str(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), 100);
}

void Voice_Init(void)
{
    last_send_ms      = 0;
    completed_flag    = 0;
    cancelled_flag    = 0;
    tare_flag         = 0;
    tareoff_flag      = 0;
    measure_done_flag = 0;
    start_adjust_flag = 0;
}

void Voice_Completed(void)       { completed_flag    = 1; }
void Voice_Cancelled(void)       { cancelled_flag    = 1; }
void Voice_Tare(void)            { tare_flag         = 1; }
void Voice_TareOff(void)         { tareoff_flag      = 1; }
void Voice_MeasureComplete(void) { measure_done_flag = 1; }
void Voice_StartAdjust(void)    { start_adjust_flag = 1; }

void Voice_Process(uint32_t now, uint8_t on_alarm_pg,
                   uint8_t overweight, uint8_t weight_err)
{
    /* ── 一次性指令（优先级最高，按标志置位顺序处理）── */
    if (completed_flag) {
        completed_flag = 0;
        send_str("completed");
        return;
    }
    if (cancelled_flag) {
        cancelled_flag = 0;
        send_str("cancelled");
        return;
    }
    if (tare_flag) {
        tare_flag = 0;
        send_str("Tare");
        return;
    }
    if (tareoff_flag) {
        tareoff_flag = 0;
        send_str("Tare Off");
        return;
    }
    if (measure_done_flag) {
        measure_done_flag = 0;
        send_str("Measure");
        return;
    }
    if (start_adjust_flag) {
        start_adjust_flag = 0;
        send_str("Start Adjust");
        return;
    }

    /* ── 报警语音 ── */
    if (!overweight && !weight_err) return;

    const char *msg = overweight ? "Overweight" : "Weight abnormal";

    if (on_alarm_pg) {
        if (now - last_send_ms >= REPEAT_MS) {
            last_send_ms = now;
            send_str(msg);
        }
    } else if (buzzer_beeped) {
        buzzer_beeped = 0;
        send_str(msg);
    }
}
