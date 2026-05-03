/**
 * @file    led.c
 * @brief   PC13 心跳灯，低电平点亮
 */
#include "led.h"

#define LED_NORMAL_MS 500
#define LED_ALARM_MS  200

static uint32_t last_toggle = 0;

void LED_Init(void)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    last_toggle = 0;
}

void LED_Process(uint32_t now, uint8_t alarm_active)
{
    uint32_t interval = alarm_active ? LED_ALARM_MS : LED_NORMAL_MS;
    if (now - last_toggle >= interval) {
        last_toggle = now;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}
