/**
 * @file    menu.h
 * @brief   菜单系统头文件，包含页面框架和相关接口
 * @note    菜单全屏显示使用8x16字符，支持多页显示
 *          页面1（数据页）：Water/Temp/SetT/Valve
 *          页面2（设置页）：MaxT/MinT/Turb/Mode
 *          页面3：报警信息显示
 * @version 2.0
 * @date    2025-03-21
 */

#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"

/****************************** 页面定义 ******************************/
#define MENU_PAGE1  2   // 页面1：实时数据+可调参数（数据显示优先，编辑功能的参数在此页面）
#define MENU_PAGE2  1   // 页面2：固定参数显示（设置页面，用于查看系统阈值）
#define MENU_PAGE3  3   // 页面3：报警信息显示

/****************************** 参数定义 ******************************/
// 固定参数常量
#define TEMP_MAX_DEFAULT      32.0f     // 最高温度默认值（℃）
#define TEMP_MIN_DEFAULT      15.0f     // 最低温度默认值（℃）
#define TURB_THRESHOLD_DEFAULT 8.0f    // 浊度阈值默认值（NTU）

// 可调参数步长
#define PRESET_TEMP_STEP     0.5f      // 预设温度步长（℃）
#define WATER_VALVE_STEP     1         // 水阀挡位步长

// 可调参数范围
#define PRESET_TEMP_MIN      10.0f     // 预设温度最小值（℃）
#define PRESET_TEMP_MAX      40.0f     // 预设温度最大值（℃）
#define WATER_VALVE_MIN      0         // 水阀挡位最小值
#define WATER_VALVE_MAX      5         // 水阀挡位最大值

/****************************** 外部变量声明 ******************************/
extern uint8_t current_page;          // 当前页面

// 页面1固定参数（不可修改）
extern float temp_max;              // 最高温度
extern float temp_min;              // 最低温度
extern float turb_threshold;        // 浊度阈值

// 页面1可调参数
extern float preset_temp;           // 预设温度
extern uint8_t water_valve_level;   // 水阀挡位（0~5）
extern uint8_t selected_item;       // 选中项：0=未选中, 1=Valve档位, 2=SetT温度, 3=Mode模式

// 阀门模式参数
extern uint8_t valve_mode_auto;       // 阀门模式：1=自动(Auto), 0=手动(Manual)

// 传感器实时数据（直接使用ADC模块的全局变量）
extern volatile float g_adc_temp;        // 当前温度值
extern volatile float g_adc_turbidity;  // 当前浊度值

// 页面3报警标志
extern uint8_t temp_alarm_flag;    // 温度报警标志
extern uint8_t turb_alarm_flag;    // 浊度报警标志

/****************************** 函数声明 ******************************/
void Menu_Init(void);    // 菜单系统初始化
void Menu_Process(void); // 菜单处理（按键检测、屏幕刷新、闪烁控制）

#endif /* __MENU_H */
