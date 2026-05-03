/**
 * @file    alarm.h
 * @brief   重量异常检测模块
 *          检测超重 (>WEIGHT_MAX) 和重量不稳 (5s 持续抖动)
 *          报警状态与页面显示分离：用户可退出报警页，报警条件保留
 *          报警条件消除后自动清除，dismiss 防止页面反复弹回
 */
#ifndef __ALARM_H__
#define __ALARM_H__

#include "main.h"

typedef enum { ALARM_NONE = 0, ALARM_OVERWEIGHT, ALARM_WEIGHT_ERR } AlarmType;

void      Alarm_Init(void);
void      Alarm_Update(float weight_kg, uint32_t now_ms);
AlarmType Alarm_GetState(void);
uint8_t   Alarm_IsActive(void);
uint8_t   Alarm_ShouldEnterPage(void);
void      Alarm_Dismiss(void);

#endif
