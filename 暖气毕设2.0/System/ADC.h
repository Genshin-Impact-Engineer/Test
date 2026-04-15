/**
 * @file    ADC.h
 * @brief   ADC驱动头文件，采集温度和浊度传感器数据
 * @note    温度传感器：A0/PA0（ADC1_CH0）
 *          浊度传感器：A1/PA1（ADC1_CH1）
 */

#ifndef __ADC_H
#define __ADC_H

#include <stdint.h>

// 传感器采集结果（供外部访问）
extern volatile float g_adc_temp;        // 温度值
extern volatile float g_adc_turbidity;  // 浊度值
extern volatile uint8_t g_adc_flag;      // 采集标志

// 函数声明
void ADC_MyInit(void);           // ADC初始化
void ADC_Process(void);          // 传感器采集处理（主循环调用）
float Get_Temperature(void);     // 获取温度值
float Get_Turbidity(void);      // 获取浊度值

#endif
