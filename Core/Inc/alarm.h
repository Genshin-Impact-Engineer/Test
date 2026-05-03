/**
 * @file    alarm.h
 * @brief   报警监控模块头文件
 *          温度/浊度双路独立状态机：5s 触发，2s 恢复
 *          USART2 驱动语音模块报警
 */

#ifndef __ALARM_H__
#define __ALARM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== 报警时间参数 ========== */
#define ALARM_TRIGGER_CNT   50      /* 5s / 100ms = 50，持续超标触发报警 */
#define ALARM_RECOVER_CNT   30      /* 3s / 100ms = 30，连续正常消警 */
#define ALARM_VOICE_RETRY   3       /* 语音重试次数 */
#define VOICE_CMD_MAX       24      /* 语音指令缓冲区大小 */

/*
 * Alarm_t —— 报警状态结构体（双路独立状态机）
 *
 * 温度/浊度各自维护超标计数和正常计数，互不阻塞。
 * 一路触发后另一路仍可独立触发，产生二次跳转和二次语音。
 *
 * temp_alarmed / turb_alarmed = 当前是否处于报警态
 * temp_exceed_cnt / turb_exceed_cnt = 超标持续计数 → TRIGGER_CNT
 * temp_normal_cnt / turb_normal_cnt = 正常持续计数 → RECOVER_CNT
 * new_trigger / new_trigger_type = 本轮触发标志（主循环消费，1=temp 2=turb 3=both 0=消警）
 */
typedef struct {
    uint8_t  temp_alarmed;
    uint8_t  turb_alarmed;
    uint16_t temp_exceed_cnt;
    uint16_t turb_exceed_cnt;
    uint16_t temp_normal_cnt;
    uint16_t turb_normal_cnt;
    uint8_t  new_trigger;
    uint8_t  new_trigger_type;       /* 1=temp 2=turb 3=both 0=recover */
    uint8_t  voice_retry;
    char     voice_cmd[VOICE_CMD_MAX];
    uint8_t  voice_tx_busy;
    uint8_t  prev_page;
    uint32_t last_check;
} Alarm_t;

/* 全局报警实例 */
extern Alarm_t halarm;

/* 模块接口函数 */
void Alarm_Init(void);
void Alarm_Process(uint32_t now, float temp, float turb,
                   float temp_max, float temp_min, float turb_threshold);
void Alarm_GetStatusStrings(const char **temp_str, const char **turb_str);

#ifdef __cplusplus
}
#endif

#endif /* __ALARM_H__ */
