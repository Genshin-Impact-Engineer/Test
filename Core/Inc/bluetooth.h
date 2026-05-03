/**
 * @file    bluetooth.h
 * @brief   蓝牙通信模块头文件：小机云蓝牙 X-B01 文本协议
 *          USART1 + DMA 循环接收 + IDLE 中断
 *
 * 协议格式：$X#D#key1:val1;key2:val2;&CheckCode\r\n
 * 回复格式：$XA#D#OK&CheckCode\r\n  /  $XA#D#ERR:message&CheckCode\r\n
 * CheckCode = BCC 异或校验（& 前所有字节异或，输出 2 位大写 hex）
 */

#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== 通信缓冲区大小 ========== */
#define BT_RX_BUF_SIZE    256   /* DMA 循环接收缓冲区 */
#define BT_TX_BUF_SIZE    320   /* 发送缓冲区（需容纳 10 字段 + 最长状态字符串） */
#define BT_PERIOD_MS      500   /* 数据上报周期（ms） */

/*
 * Bluetooth_t —— 蓝牙通信状态结构体
 *
 * rx_buf[]        = DMA 循环接收缓冲区（USART1 DMA CIRCULAR 模式）
 * rx_index        = 接收索引（保留扩展）
 * cmd_ready       = 收到完整命令标志（IDLE 中断置位，主循环消费）
 * cmd_buf[]       = 当前待处理命令副本（从 rx_buf 复制）
 *
 * 上传数据字段：
 *   turbidity~turb_threshold → 传感器数据与设置参数
 *   valve_mode    = 模式标志（0=Auto, 1=Manual）
 *   temp_state[] / turb_state[] = 报警状态文字
 *
 * immediate_upload = 强制上传标志
 */
typedef struct {
    uint8_t rx_buf[BT_RX_BUF_SIZE];
    uint16_t rx_index;
    volatile uint8_t cmd_ready;
    char cmd_buf[BT_RX_BUF_SIZE];

    float turbidity;
    float temperature;
    float preset_temp;
    uint8_t water_level;
    float temp_max;
    float temp_min;
    float turb_threshold;
    uint8_t valve_mode;
    char temp_state[32];
    char turb_state[32];

    volatile uint8_t immediate_upload;
    uint32_t last_tx_start;              /* DMA TX 启动时间戳（ms），用于 TX 卡死检测 */
} Bluetooth_t;

/* 全局蓝牙实例 */
extern Bluetooth_t hbt;

/* 模块接口函数 */
void Bluetooth_Init(void);
void Bluetooth_SendData(void);
void Bluetooth_ProcessCommand(void);
void Bluetooth_ParseCommand(const char *cmd);
void Bluetooth_SetSensorData(float temp, float turb);
void Bluetooth_SetStatus(const char *temp_st, const char *turb_st);
void Bluetooth_RequestUpload(void);
uint8_t Bluetooth_CheckTXStuck(uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_H__ */
