/**
 * @file    sensor.h
 * @brief   传感器模块头文件
 *          PT100 温度传感器 + TSW-30 浊度传感器
 *          ADC DMA 循环采样 + 加权滑动滤波 + 温度补偿
 */

#ifndef __SENSOR_H__
#define __SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== ADC 配置常量 ========== */
#define SENSOR_ADC_BUF_SIZE    2       /* DMA 缓冲区大小：2 通道 [温度, 浊度] */
#define SENSOR_FILTER_DEPTH    4       /* 滑动加权滤波深度：保留最近 4 次采样值 */
#define SENSOR_VREF            3.3f    /* ADC 参考电压（V）：STM32F103 VDDA=3.3V */
#define SENSOR_ADC_RES         4096.0f /* 12 位 ADC 分辨率：2^12 = 4096 */

/* ========== PT100 温度计算参数 ========== */
#define PT100_SLOPE            51.2f   /* PT100 电路增益系数（°C/V）：放大电路设计决定 */

/* ========== TSW-30 浊度计算参数 ========== */
#define TURB_SLOPE            -865.68f /* TSW-30 浊度电压-浓度转换系数（NTU/V），负斜率表示电压越高浊度越低 */
#define TEMP_COMP_COEFF       -0.0192f /* TSW-30 温度补偿系数（V/°C）：修正 LED 输出随温度的漂移 */

/*
 * Sensor_t —— 传感器状态结构体
 * adc_buf[0]  = PT100 温度通道原始 ADC 值
 * adc_buf[1]  = TSW-30 浊度通道原始 ADC 值
 * dma_flag    = ADC 转换完成标志（由 DMA 中断回调置 1，主循环清零）
 * temperature = 最终计算出的温度值（°C）
 * turbidity   = 最终计算出的浊度值（NTU）
 * K_value     = 浊度校准常数（在标准液下标定得出）
 * temp_offset = 温度校准偏移量（在已知参考温度下标定得出）
 * raw_temp_buf[] / raw_turb_buf[] = 滑动滤波缓冲，保存最近 4 次原始值
 * filter_index = 循环写入索引（0→1→2→3→0），指向下一次要覆盖的位置
 */
typedef struct {
    uint16_t adc_buf[SENSOR_ADC_BUF_SIZE];  /* ADC DMA 循环缓冲区 (双通道) */
    volatile uint8_t dma_flag;               /* ADC 转换完成标志（中断中置位，主循环消费） */
    float temperature;                        /* 滤波后温度值（°C） */
    float turbidity;                          /* 滤波浊度值（NTU） */
    float K_value;                            /* 浊度校准 K 值（基准常数） */
    float temp_offset;                        /* 温度校准偏移（°C） */
    uint16_t raw_temp_buf[SENSOR_FILTER_DEPTH];  /* 温度原始值循环缓冲 */
    uint16_t raw_turb_buf[SENSOR_FILTER_DEPTH];  /* 浊度原始值循环缓冲 */
    uint8_t filter_index;                        /* 滤波缓冲写入索引 */
} Sensor_t;

/* 全局传感器实例，在 sensor.c 中定义 */
extern Sensor_t hsensor;

/* 模块接口函数 */
void Sensor_Init(void);                                    /* 初始化并启动 ADC DMA */
void Sensor_Update(void);                                  /* 处理 ADC 数据（加权滤波+物理换算） */
float Sensor_GetTemperature(void);                         /* 获取当前温度 */
float Sensor_GetTurbidity(void);                           /* 获取当前浊度 */
void Sensor_SetCalibration(float K, float offset);         /* 设置校准参数 */

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_H__ */
