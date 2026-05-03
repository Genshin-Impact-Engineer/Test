/**
 * @file    alarm.c
 * @brief   报警监控模块：温度/浊度双路独立状态机
 *          持续 5s 超标触发，连续 3s 正常消警
 *          USART2 驱动语音模块报警
 */

#include "alarm.h"
#include "oled.h"
#include "bluetooth.h"
#include <string.h>

/* USART2 句柄（语音模块）在 usart.c 中定义 */
extern UART_HandleTypeDef huart2;

/* 全局报警实例 */
Alarm_t halarm = {0};

/*
 * Alarm_Init —— 报警模块初始化
 */
void Alarm_Init(void) {
    memset(&halarm, 0, sizeof(halarm));
}

/*
 * send_voice —— 发送语音指令到语音模块
 * @param cmd  语音指令字符串（常量，如 "temp" / "water" / "temp and water"）
 *
 * 复制到 RAM 缓冲区后通过 USART2 DMA 发送。
 * 若 DMA 启动失败则设置重试计数。
 */
static void send_voice(const char *cmd) {
    strncpy(halarm.voice_cmd, cmd, VOICE_CMD_MAX - 1);
    halarm.voice_cmd[VOICE_CMD_MAX - 1] = '\0';

    if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)halarm.voice_cmd,
                               strlen(halarm.voice_cmd)) != HAL_OK) {
        halarm.voice_retry = ALARM_VOICE_RETRY;
        halarm.voice_tx_busy = 0;
    } else {
        halarm.voice_tx_busy = 1;
        halarm.voice_retry = 0;
    }
}

/*
 * Alarm_Process —— 双路独立报警状态机，主循环每 100ms 调用一次
 *
 * 温度、浊度各走独立状态机：
 *
 *   NORMAL ──超标→ COUNTING ──满50→ ALARMED ──正常→ RECOVERING ──满20→ 消警
 *              ↑ 正常则清零                ↑ 超标则清零
 *
 * 两路互不阻塞：温度报警后浊度仍可独立触发（二次跳转+二次语音）。
 * new_trigger 标志由主循环消费，用于页面跳转和蓝牙强制上传。
 */
void Alarm_Process(uint32_t now, float temp, float turb,
                   float temp_max, float temp_min, float turb_threshold) {
    if (now - halarm.last_check < 100) return;
    halarm.last_check = now;

    uint8_t temp_exceed = (temp > temp_max || temp < temp_min);
    uint8_t turb_exceed = (turb >= turb_threshold);

    /* ========== 温度状态机 ========== */
    if (temp_exceed) {
        halarm.temp_normal_cnt = 0;
        if (!halarm.temp_alarmed && halarm.temp_exceed_cnt < 0xFFFF)
            halarm.temp_exceed_cnt++;
    } else {
        halarm.temp_exceed_cnt = 0;
        if (halarm.temp_alarmed && halarm.temp_normal_cnt < 0xFFFF)
            halarm.temp_normal_cnt++;
    }

    /* ========== 浊度状态机 ========== */
    if (turb_exceed) {
        halarm.turb_normal_cnt = 0;
        if (!halarm.turb_alarmed && halarm.turb_exceed_cnt < 0xFFFF)
            halarm.turb_exceed_cnt++;
    } else {
        halarm.turb_exceed_cnt = 0;
        if (halarm.turb_alarmed && halarm.turb_normal_cnt < 0xFFFF)
            halarm.turb_normal_cnt++;
    }

    /* ========== 触发检测 ========== */
    uint8_t trig_temp = 0, trig_turb = 0;

    if (halarm.temp_exceed_cnt >= ALARM_TRIGGER_CNT && !halarm.temp_alarmed) {
        halarm.temp_alarmed = 1;
        halarm.temp_exceed_cnt = 0;
        trig_temp = 1;
    }

    if (halarm.turb_exceed_cnt >= ALARM_TRIGGER_CNT && !halarm.turb_alarmed) {
        halarm.turb_alarmed = 1;
        halarm.turb_exceed_cnt = 0;
        trig_turb = 1;
    }

    /* ========== 恢复检测 ========== */
    uint8_t recovered = 0;

    if (halarm.temp_alarmed && halarm.temp_normal_cnt >= ALARM_RECOVER_CNT) {
        halarm.temp_alarmed = 0;
        halarm.temp_normal_cnt = 0;
        recovered = 1;
    }

    if (halarm.turb_alarmed && halarm.turb_normal_cnt >= ALARM_RECOVER_CNT) {
        halarm.turb_alarmed = 0;
        halarm.turb_normal_cnt = 0;
        recovered = 1;
    }

    /* ========== 通知主循环 ========== */
    if (trig_temp || trig_turb) {
        halarm.new_trigger = 1;
        if (trig_temp && trig_turb)
            halarm.new_trigger_type = 3;
        else if (trig_temp)
            halarm.new_trigger_type = 1;
        else
            halarm.new_trigger_type = 2;

        /* 选择语音：反映当前全部报警状态 */
        const char *cmd;
        if (halarm.temp_alarmed && halarm.turb_alarmed)
            cmd = "temp and water";
        else if (halarm.temp_alarmed)
            cmd = "temp";
        else
            cmd = "water";

        send_voice(cmd);
    } else if (recovered && !halarm.temp_alarmed && !halarm.turb_alarmed) {
        /* 两路全部恢复正常 → 消警 */
        halarm.new_trigger = 1;
        halarm.new_trigger_type = 0;
    }

    /* ========== 语音重试 ========== */
    if (halarm.voice_retry > 0 && !halarm.voice_tx_busy) {
        if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)halarm.voice_cmd,
                                   strlen(halarm.voice_cmd)) == HAL_OK) {
            halarm.voice_retry = 0;
            halarm.voice_tx_busy = 1;
        } else {
            halarm.voice_retry--;
        }
    }
}

/*
 * HAL_UART_TxCpltCallback —— USART2 DMA 发送完成回调
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        extern Bluetooth_t hbt;
        hbt.last_tx_start = 0;
    } else if (huart->Instance == USART2) {
        halarm.voice_tx_busy = 0;
    }
}

/*
 * Alarm_GetStatusStrings —— 获取报警状态文字描述
 * 用于蓝牙模块上传给手机 APP 显示
 */
void Alarm_GetStatusStrings(const char **temp_str, const char **turb_str) {
    /*
     * 状态字符串长度直接影响蓝牙上报帧总长。
     * 双告警帧 = 两段告警字符串同时出现，帧长可达 261 字节，
     * 超出蓝牙模块 X-B01 内部 256 字节限制 → 模块静默丢弃。
     * 将告警字符串压缩至 12 字节以内，最大帧长控制在 236 字节。
     */
    *temp_str = halarm.temp_alarmed ? "Temp Alarm!" : "Temp OK!";
    *turb_str = halarm.turb_alarmed ? "Water Alarm!" : "Water OK!";
}
