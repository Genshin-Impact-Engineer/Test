/**
 * @file    voice.h
 * @brief   ASR-PRO 语音播报模块（UART2, 115200）
 *
 * 指令集（对齐 ASR-PRO.md）：
 *   "completed"         —— 更新完成（编辑保存）
 *   "cancelled"         —— 取消更新（编辑取消）
 *   "Overweight"        —— 超重，请重新称量
 *   "Weight abnormal"   —— 重量异常，请重新称量
 *   "Tare"             —— 去皮
 *   "Tare Off"         —— 取消去皮
 *   "Measure Complete" —— 称量完成（结算）
 *   "Start Adjust"     —— 开始调整（进入编辑）
 */
#ifndef __VOICE_H__
#define __VOICE_H__

#include "main.h"

void Voice_Init(void);

/* 由主循环每轮调用 */
void Voice_Process(uint32_t now, uint8_t on_alarm_page,
                   uint8_t overweight, uint8_t weight_err);

/* 按键事件触发 */
void Voice_Completed(void);
void Voice_Cancelled(void);
void Voice_Tare(void);
void Voice_TareOff(void);
void Voice_MeasureComplete(void);
void Voice_StartAdjust(void);

#endif
