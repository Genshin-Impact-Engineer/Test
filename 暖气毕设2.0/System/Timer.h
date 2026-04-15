/**
 * @file    Timer.h
 * @brief   定时器驱动头文件
 * @note    Timer3：系统时基（uwTick）
 *          Timer4：传感器采集定时器
 */

#ifndef __TIMER_H
#define __TIMER_H

void TIM3_Init(u16 arr, u16 psc);  // Timer3初始化（系统时基）
void TIM4_Init(u16 arr, u16 psc);  // Timer4初始化（传感器采集）

extern u32 uwTick;  // 系统时基计数器
extern volatile u8 valve_control_flag;  // 阀门控制标志（0=不控制, 1=立即控制）
extern volatile float g_last_preset_temp;  // 上次的预设温度（用于检测变化）

extern volatile u8 valve_timer_flag;  // 阀门定时检测标志

extern volatile float g_last_adc_temp;  // 上次的实际温度

#endif
