/**
 * @file    bluetooth.h
 * @brief   蓝牙通信模块头文件：小机云蓝牙 X-B01 文本协议
 *          电子秤/POS 系统 —— USART1 + DMA 循环接收 + IDLE 中断
 *
 * 协议格式：$X#D#key1:val1;key2:val2;&CheckCode\r\n
 * 回复格式：$XA#D#OK&CheckCode\r\n  /  $XA#D#ERR:message&CheckCode\r\n
 * CheckCode = BCC 异或校验（& 前所有字节异或，输出 2 位大写 hex）
 *
 * 上行单帧 6 个动态字段（全 ASCII，不发送中文，小程序标签由设计时预置）
 *
 * 硬件：USART1, PA9(TX)/PA10(RX), 115200-8-N-1
 * 蓝牙模块：小机云 X-B01，透传模式，GPIO=LOW
 */
#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== 通信缓冲区大小 ========== */
#define BT_RX_BUF_SIZE    256   /* DMA 循环接收缓冲区 */
#define BT_TX_BUF_SIZE    256   /* 发送缓冲区 */
#define BT_PERIOD_MS      200   /* 数据上报周期（ms） */

/*
 * Bluetooth_t —— 蓝牙通信状态结构体
 *
 * 上行数据字段（6 个，单帧发送）：
 *   selected_goods  = 商品索引（0~6）
 *   number_price    = 单价（元/kg）
 *   weight          = 重量（kg）
 *   net_weight      = 去皮重量（kg）
 *   total_price     = 总价（元）
 *   text_state      = 系统状态英文文本
 */
typedef struct {
    uint8_t rx_buf[BT_RX_BUF_SIZE];
    uint16_t rx_index;
    volatile uint8_t cmd_ready;
    char cmd_buf[BT_RX_BUF_SIZE];

    /* 上行数据字段 */
    uint8_t selected_goods;
    float   number_price;
    float   weight;
    float   net_weight;
    float   total_price;
    char    text_state[48];

    volatile uint8_t immediate_upload;
    uint8_t  status_pending;
    uint32_t last_tx_start;              /* DMA TX 启动时间戳（ms），用于 TX 卡死检测 */
} Bluetooth_t;

/* 全局蓝牙实例 */
extern Bluetooth_t hbt;

/* 模块接口函数 */
void Bluetooth_Init(void);
void Bluetooth_SendData(void);           /* 单帧 6 字段，周期性发送 */
void Bluetooth_ProcessCommand(void);
void Bluetooth_ParseCommand(const char *cmd);
void Bluetooth_SetLiveData(uint8_t goods_idx, float price,
                           float weight, float net_weight, float total);
void Bluetooth_SetStatus(const char *status);
void Bluetooth_RequestUpload(void);
uint8_t Bluetooth_CheckTXStuck(uint32_t now);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_H__ */
