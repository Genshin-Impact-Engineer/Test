/**
 * @file    buzzer.c
 * @brief   无源蜂鸣器模块（TIM2_CH1 PWM，统一 3kHz）
 *
 * 状态机：
 *   BUZZ_OFF ──(alarm page)──→ BUZZ_CONTINUOUS   一直鸣叫
 *   BUZZ_OFF ──(alarm active)─→ BUZZ_INTERMITTENT  间歇鸣叫
 *   BUZZ_OFF ──(key press)───→ BUZZ_SHORT         短鸣 100ms
 */
#include "buzzer.h"
#include "tim.h"

#define SHORT_MS         100
#define INTERVAL_ON_MS   1500
#define INTERVAL_OFF_MS  3000

/* 3kHz PWM: 8MHz / 8 / 333 ≈ 3003Hz, 50% duty */
#define ARR_3K   332
#define CCR_3K   166

typedef enum { BUZZ_OFF, BUZZ_SHORT, BUZZ_CONTINUOUS,
               BUZZ_INTERMITTENT_ON, BUZZ_INTERMITTENT_OFF } BuzzState;

static BuzzState state    = BUZZ_OFF;
static uint32_t  start_ms = 0;

volatile uint8_t buzzer_beeped = 0;  /* 蜂鸣器开始鸣叫标志，供语音模块消费 */

static inline void set_3kHz(void) {
    __HAL_TIM_SET_AUTORELOAD(&htim2, ARR_3K);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, CCR_3K);
    htim2.Instance->EGR = TIM_EGR_UG;
}

void Buzzer_Init(void)
{
    HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    set_3kHz();
    state    = BUZZ_OFF;
    start_ms = 0;
}

void Buzzer_ShortBeep(void)
{
    if (state != BUZZ_OFF) return;
    extern volatile uint32_t sys_tick_ms;
    set_3kHz();
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    state    = BUZZ_SHORT;
    start_ms = sys_tick_ms;
}

void Buzzer_Process(uint32_t now, uint8_t on_alarm_page, uint8_t alarm_active)
{
    switch (state) {

    case BUZZ_OFF:
        if (on_alarm_page) {
            set_3kHz();
            HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
            state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
        } else if (alarm_active) {
            set_3kHz();
            HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
            state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
            start_ms = now;
        }
        break;

    case BUZZ_SHORT:
        if (now - start_ms >= SHORT_MS) {
            HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
            state = BUZZ_OFF;
            if (on_alarm_page) {
                set_3kHz();
                HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
                state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
            } else if (alarm_active) {
                set_3kHz();
                HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
                state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
                start_ms = now;
            }
        }
        break;

    case BUZZ_CONTINUOUS:
        if (!on_alarm_page) {
            if (alarm_active) {
                set_3kHz();
                state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
                start_ms = now;
            } else {
                HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
                state = BUZZ_OFF;
            }
        }
        break;

    case BUZZ_INTERMITTENT_ON:
        if (!alarm_active && !on_alarm_page) {
            HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
            state = BUZZ_OFF;
        } else if (on_alarm_page) {
            set_3kHz();
            state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
        } else if (now - start_ms >= INTERVAL_ON_MS) {
            HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
            state    = BUZZ_INTERMITTENT_OFF;
            start_ms = now;
        }
        break;

    case BUZZ_INTERMITTENT_OFF:
        if (!alarm_active && !on_alarm_page) {
            state = BUZZ_OFF;
        } else if (on_alarm_page) {
            set_3kHz();
            HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
            state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
        } else if (now - start_ms >= INTERVAL_OFF_MS) {
            HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
            state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
            start_ms = now;
        }
        break;
    }
}
