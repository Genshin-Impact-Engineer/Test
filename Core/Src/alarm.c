/**
 * @file    alarm.c
 * @brief   重量异常检测
 *
 * 检测规则：
 *   超重：weight > 2.0kg 持续 0.5s → ALARM_OVERWEIGHT
 *   抖动：每 1s 取一次代表值，相邻两次差值 >50g，连续 3s → ALARM_WEIGHT_ERR
 *   恢复：空载自动清除；超重回落自动清除；连续 2s 差值 ≤50g 后清除抖动
 */
#include "alarm.h"
#include "oled.h"

static AlarmType alarm_state = ALARM_NONE;
static uint8_t   dismissed   = 0;

static uint32_t overweight_ms  = 0;

/* 抖动检测：每 100ms 调用一次，10 次 = 1s */
static uint8_t  call_1s_cnt    = 0;   /* 1s 内调用计数 (0~9) */
static float    sample_prev    = 0.0f; /* 上一次 1s 代表值 */
static uint8_t  jitter_secs    = 0;   /* 连续抖动秒数 */
static uint8_t  stable_secs    = 0;   /* 连续稳定秒数（用于恢复） */

#define JITTER_THRESHOLD  0.050f  /* 50g 差值阈值 */
#define JITTER_TRIGGER    3       /* 连续 3s 触发报警 */
#define STABLE_RECOVER    2       /* 连续 2s 稳定后解除 */
#define OVERWEIGHT_HOLD   500     /* 0.5s 超重确认 */

void Alarm_Init(void)
{
    alarm_state    = ALARM_NONE;
    dismissed      = 0;
    overweight_ms  = 0;
    call_1s_cnt    = 0;
    sample_prev    = 0.0f;
    jitter_secs    = 0;
    stable_secs    = 0;
}

AlarmType Alarm_GetState(void)       { return alarm_state; }
uint8_t   Alarm_IsActive(void)       { return alarm_state != ALARM_NONE; }
uint8_t   Alarm_ShouldEnterPage(void){ return Alarm_IsActive() && !dismissed; }
void      Alarm_Dismiss(void)        { dismissed = 1; }

void Alarm_Update(float w, uint32_t now)
{
    /* ── 空载：清除一切 ── */
    if (w < WEIGHT_MIN) {
        alarm_state   = ALARM_NONE;
        dismissed     = 0;
        overweight_ms = 0;
        call_1s_cnt   = 0;
        sample_prev   = 0.0f;
        jitter_secs   = 0;
        stable_secs   = 0;
        return;
    }

    /* ── 超重检测（需持续 0.5s 确认）── */
    if (w > WEIGHT_MAX) {
        if (alarm_state != ALARM_OVERWEIGHT) {
            if (overweight_ms == 0) {
                overweight_ms = now;
            } else if (now - overweight_ms >= OVERWEIGHT_HOLD) {
                alarm_state = ALARM_OVERWEIGHT;
                dismissed   = 0;
            }
        }
        return;
    }

    /* ── 超重回落到正常范围 → 清除 ── */
    overweight_ms = 0;
    if (alarm_state == ALARM_OVERWEIGHT) {
        alarm_state = ALARM_NONE;
        dismissed   = 0;
    }

    /* ================================================================
     * 抖动检测：每 1s 取一个代表值，比较相邻两次
     * 连续 3s 差值 >50g → 报警
     * 连续 2s 差值 ≤50g → 恢复
     * ================================================================ */
    if (++call_1s_cnt < 10) return;   /* 不满 1s 不采样 */
    call_1s_cnt = 0;

    float diff = w - sample_prev;
    if (diff < 0) diff = -diff;

    if (diff > JITTER_THRESHOLD) {
        jitter_secs++;
        stable_secs = 0;
        if (jitter_secs >= JITTER_TRIGGER) {
            if (alarm_state != ALARM_WEIGHT_ERR) {
                alarm_state = ALARM_WEIGHT_ERR;
                dismissed   = 0;
            }
        }
    } else {
        stable_secs++;
        jitter_secs = 0;
        if (alarm_state == ALARM_WEIGHT_ERR && stable_secs >= STABLE_RECOVER) {
            alarm_state = ALARM_NONE;
            dismissed   = 0;
        }
    }
    sample_prev = w;
}
