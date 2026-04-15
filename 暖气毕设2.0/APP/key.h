/**
 * @file    key.h
 * @brief   按键驱动头文件，外部中断+软件消抖实现按键检测
 * @note    硬件连接说明：
 *          PB12 → K1（确认键，功能待定）
 *          PB13 → K2（取消键，功能待定）
 *          PB14 → K3（菜单选择 / 长按切换页面）
 *          PB15 → K4（菜单选择 / 长按切换页面 / 页面3清除报警）
 * @version 2.0
 * @date    2025-03-21
 */

#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

/****************************** 按键硬件引脚定义 ******************************/
#define KEY1_PIN    GPIO_Pin_12   // K1在PB12（确认键）
#define KEY2_PIN    GPIO_Pin_13   // K2在PB13（取消键）
#define KEY3_PIN    GPIO_Pin_14   // K3在PB14（选择移动 / 长按切换页面）
#define KEY4_PIN    GPIO_Pin_15   // K4在PB15（选择移动 / 长按切换页面 / 清除报警）
#define KEY_PORT    GPIOB
#define KEY_RCC     RCC_APB2Periph_GPIOB

/****************************** 按键状态枚举（供外部调用） ******************************/
typedef enum {
    KEY_NONE = 0,    // 无按键按下
    KEY1_PRESS,      // K1按下（PB12），确认功能
    KEY2_PRESS,      // K2按下（PB13），取消功能
    KEY3_PRESS,      // K3按下（PB14），选择移动
    KEY4_PRESS       // K4按下（PB15），选择移动 / 清除报警
} Key_State;

/****************************** 函数声明 ******************************/
void Key_GPIO_Init(void);              // 初始化GPIO+中断
Key_State Key_Get_State(void);         // 获取按键状态（供主循环调用）
void Key_Clear_Pending(Key_State key); // 清除指定按键的待处理标志（用于长按后防止再次触发短按）

#endif /* __KEY_H */
