/**
 * @file    buzzer.c
 * @brief   有源蜂鸣器模块（PA0 GPIO 高低电平驱动）
 *
 * 状态机：
 *   BUZZ_OFF ──(alarm page)──→ BUZZ_CONTINUOUS   一直鸣叫
 *   BUZZ_OFF ──(alarm active)─→ BUZZ_INTERMITTENT  间歇鸣叫
 *   BUZZ_OFF ──(key press)───→ BUZZ_SHORT         短鸣 100ms
 */
#include "buzzer.h"
#include "gpio.h"

#define SHORT_MS         100
#define INTERVAL_ON_MS   1500
#define INTERVAL_OFF_MS  3000

typedef enum { BUZZ_OFF, BUZZ_SHORT, BUZZ_CONTINUOUS,
               BUZZ_INTERMITTENT_ON, BUZZ_INTERMITTENT_OFF } BuzzState;

static BuzzState state    = BUZZ_OFF;
static uint32_t  start_ms = 0;

volatile uint8_t buzzer_beeped = 0;

static inline void buzzer_on(void)  { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET); }
static inline void buzzer_off(void) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET); }

void Buzzer_Init(void)
{
    buzzer_off();
    state    = BUZZ_OFF;
    start_ms = 0;
}

void Buzzer_ShortBeep(void)
{
    if (state != BUZZ_OFF) return;
    extern volatile uint32_t sys_tick_ms;
    buzzer_on();
    state    = BUZZ_SHORT;
    start_ms = sys_tick_ms;
}

void Buzzer_Process(uint32_t now, uint8_t on_alarm_page, uint8_t alarm_active)
{
    switch (state) {

    case BUZZ_OFF:
        if (on_alarm_page) {
            buzzer_on();
            state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
        } else if (alarm_active) {
            buzzer_on();
            state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
            start_ms = now;
        }
        break;

    case BUZZ_SHORT:
        if (now - start_ms >= SHORT_MS) {
            buzzer_off();
            state = BUZZ_OFF;
            if (on_alarm_page) {
                buzzer_on();
                state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
            } else if (alarm_active) {
                buzzer_on();
                state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
                start_ms = now;
            }
        }
        break;

    case BUZZ_CONTINUOUS:
        if (!on_alarm_page) {
            if (alarm_active) {
                state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
                start_ms = now;
            } else {
                buzzer_off();
                state = BUZZ_OFF;
            }
        }
        break;

    case BUZZ_INTERMITTENT_ON:
        if (!alarm_active && !on_alarm_page) {
            buzzer_off();
            state = BUZZ_OFF;
        } else if (on_alarm_page) {
            state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
        } else if (now - start_ms >= INTERVAL_ON_MS) {
            buzzer_off();
            state    = BUZZ_INTERMITTENT_OFF;
            start_ms = now;
        }
        break;

    case BUZZ_INTERMITTENT_OFF:
        if (!alarm_active && !on_alarm_page) {
            state = BUZZ_OFF;
        } else if (on_alarm_page) {
            buzzer_on();
            state = BUZZ_CONTINUOUS; buzzer_beeped = 1;
        } else if (now - start_ms >= INTERVAL_OFF_MS) {
            buzzer_on();
            state    = BUZZ_INTERMITTENT_ON; buzzer_beeped = 1;
            start_ms = now;
        }
        break;
    }
}
