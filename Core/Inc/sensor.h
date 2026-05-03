#ifndef __SENSOR_H__
#define __SENSOR_H__

#include "main.h"

/* HX711 引脚: PA6=SCK(推挽输出), PA7=DOUT(上拉输入) */
#define HX711_SCK_PIN     GPIO_PIN_6
#define HX711_SCK_PORT    GPIOA
#define HX711_DOUT_PIN    GPIO_PIN_7
#define HX711_DOUT_PORT   GPIOA

/* 自适应一阶滞后滤波: 变化时快速跟踪，稳定时强力平滑 */
#define FILTER_ALPHA_FAST   0.5f
#define FILTER_ALPHA_SLOW   0.1f

/* 输出死区 (kg): 变化小于此值不更新，抑制尾数跳变 */
#define OUTPUT_DEADBAND     0.002f

/* 标定系数: 0.00000236 × 0.213kg / 0.188kg(显示)
 * 交叉验证: 15g→0.014kg(实际0.015), 误差1g */
#define DEFAULT_SCALE_FACTOR   0.00000267f

void  Sensor_Init(void);
void  Sensor_Update(void);
float Sensor_GetWeight(void);         /* 死区限幅后的重量，用于显示 */
float Sensor_GetRawWeight(void);      /* 滤波后但未经死区的重量，用于报警抖动检测 */

#endif
