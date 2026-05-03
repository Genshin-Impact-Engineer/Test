/**
 * @file    led.h
 * @brief   PC13 心跳灯模块
 *          正常：1Hz 翻转 (500ms)
 *          报警：2.5Hz 翻转 (200ms)
 */
#ifndef __LED_H__
#define __LED_H__

#include "main.h"

void LED_Init(void);
void LED_Process(uint32_t now, uint8_t alarm_active);

#endif
