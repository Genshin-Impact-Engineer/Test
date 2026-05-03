#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "main.h"

/* HX711 引脚: PA6=SCK(推挽输出), PA7=DOUT(上拉输入) */
#define HX711_SCK_PIN     GPIO_PIN_6
#define HX711_SCK_PORT    GPIOA
#define HX711_DOUT_PIN    GPIO_PIN_7
#define HX711_DOUT_PORT   GPIOA

/* 一阶滞后滤波系数 (设计方案 4.3 节) */
#define FILTER_ALPHA      0.3f

/* 标定参数（需根据实际传感器标定） */
#define DEFAULT_ZERO_OFFSET    0L
#define DEFAULT_SCALE_FACTOR   0.001f

void  Sensor_Init(void);
void  Sensor_Update(void);
float Sensor_GetWeight(void);

#endif
