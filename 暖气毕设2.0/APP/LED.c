/**
 * @file    LED.c
 * @brief   LED驱动实现文件
 * @note    LED连接在PC13引脚，低电平点亮
 */

#include "stm32f10x.h"

/**
 * @brief   LED初始化函数
 * @retval  无
 * @note    配置PC13为推挽输出模式
 *          - LED_ON:  点亮LED（输出低电平）
 *          - LED_OFF: 熄灭LED（输出高电平）
 */
void LED_Init(void)
{
    /* 使能GPIOC时钟（LED所在端口） */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    
    /* 配置GPIO结构体 */
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;   /* 推挽输出模式 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;          /* LED在PC13 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;   /* 输出速度50MHz */
    GPIO_Init(GPIOC, &GPIO_InitStructure);
    
    /* 默认关闭LED（输出高电平） */
    GPIO_SetBits(GPIOC, GPIO_Pin_13);
}
