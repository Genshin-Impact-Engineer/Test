/**
 * @file    buzzer.h
 * @brief   无源蜂鸣器模块：TIM2_CH1 3kHz PWM 驱动
 *          5 态 FSM：OFF / SHORT(100ms) / CONTINUOUS / INTERMITTENT
 *          非阻塞，所有状态切换在 Buzzer_Process() 中完成
 */
#ifndef __BUZZER_H__
#define __BUZZER_H__

#include "main.h"

/* 蜂鸣器开始鸣叫时置 1（进入 CONTINUOUS 或 INTERMITTENT_ON），语音模块消费后清零 */
extern volatile uint8_t buzzer_beeped;

void Buzzer_Init(void);
void Buzzer_ShortBeep(void);
void Buzzer_Process(uint32_t now, uint8_t on_alarm_page, uint8_t alarm_active);

#endif
