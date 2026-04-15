/**
 * @file    LED.h
 * @brief   LED驱动头文件
 * @note    LED连接在PC13引脚
 */

#ifndef __LED_H
#define __LED_H

/*===========================================
 * LED控制宏定义
 *===========================================*/

/* 点亮LED：PC13输出低电平 */
#define LED_ON   GPIO_ResetBits(GPIOC, GPIO_Pin_13)

/* 熄灭LED：PC13输出高电平 */
#define LED_OFF  GPIO_SetBits(GPIOC, GPIO_Pin_13)

/*===========================================
 * 函数声明
 *===========================================*/

/**
 * @brief   LED初始化函数
 * @note    配置PC13为推挽输出模式
 */
void LED_Init(void);

#endif
