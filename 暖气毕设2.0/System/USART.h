/**
 * @file    usart.h
 * @brief   串口驱动头文件
 * @note    声明串口初始化函数和发送/接收缓冲区
 *          - USART1: PA9/PA10 - 蓝牙通信
 *          - USART2: PA2/PA3 - 语音模块通信
 *          - USART3: PB10/PB11 - 备用
 */

#ifndef _USART_H
#define _USART_H

#include "stm32f10x.h"

/*===========================================
 * 缓冲区长度定义
 *===========================================*/

/* USART1接收缓冲区长度（蓝牙） */
#define USART1_REC_LEN   255

/* USART2接收缓冲区长度（语音模块） */
#define USART2_REC_LEN   200

/* USART3接收缓冲区长度（备用） */
#define USART3_REC_LEN   200

/*===========================================
 * 外部变量声明
 *===========================================*/

/* USART1接收缓冲区（存储蓝牙接收的数据） */
extern u8 USART1_RX_BUF[USART1_REC_LEN];
/* USART1接收状态标志 */
extern u16 USART1_RX_STA;

/* USART2接收缓冲区（存储语音模块反馈） */
extern u8 USART2_RX_BUF[USART2_REC_LEN];
/* USART2接收状态标志 */
extern u16 USART2_RX_STA;
/* USART2发送缓冲区 */
extern u8 USART2_TX_BUF[255];
/* USART2接收计数 */
extern u8 RX_BUF_CNT;

/* USART3接收缓冲区 */
extern u8 USART3_RX_BUF[USART3_REC_LEN];
/* USART3接收状态标志 */
extern u16 USART3_RX_STA;

/*===========================================
 * 函数声明
 *===========================================*/

/**
 * @brief   USART1初始化（蓝牙通信）
 * @param   bound: 波特率，如9600、115200
 */
void Usart1_Init(u32 bound);

/**
 * @brief   USART2初始化（语音模块通信）
 * @param   bound: 波特率
 */
void Usart2_Init(u32 bound);

/**
 * @brief   USART3初始化（备用）
 * @param   bound: 波特率
 */
void Usart3_Init(u32 bound);

/**
 * @brief   USART2发送字符串（用于语音模块）
 * @param   str: 要发送的字符串
 * @note    自动添加换行符\r\n
 */
void Usart2_Send_String(char* str);

/**
 * @brief   清除所有串口缓冲区
 */
void Clear_USART_buf(void);

/**
 * @brief   RS485发送帧（备用）
 * @param   Length: 发送数据长度
 */
void RS485_TX_Frame(u16 Length);

#endif
