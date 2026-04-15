/**
 * @file    Delay.c
 * @brief   延时函数实现文件
 * @note    提供微秒、毫秒、秒级延时功能
 *          使用SysTick定时器实现精确延时
 */

#include "stm32f10x.h"
#include "Delay.h"

/**
 * @brief   微秒级延时
 * @param   xus: 延时时长（微秒）
 * @retval  无
 * @note    利用SysTick定时器实现，延时精度高
 *          系统时钟72MHz时，最大延时约233015微秒
 */
void Delay_us(uint32_t xus)
{
    /* 设置定时器重装载值：72 * 延时微秒数 */
    /* 因为72MHz / 1 = 72，所以每微秒需要72个计数 */
    SysTick->LOAD = 72 * xus;
    
    /* 清空当前计数值 */
    SysTick->VAL = 0x00;
    
    /* 设置时钟源为HCLK（72MHz），并启动定时器 */
    SysTick->CTRL = 0x00000005;
    
    /* 等待计数到0（COUNTFLAG标志位置1） */
    while(!(SysTick->CTRL & 0x00010000));
    
    /* 关闭定时器 */
    SysTick->CTRL = 0x00000004;
}

/**
 * @brief   毫秒级延时
 * @param   xms: 延时时长（毫秒）
 * @retval  无
 * @note    内部调用Delay_us(1000)实现
 */
void Delay_ms(uint32_t xms)
{
    /* 循环调用微秒延时，每次延时1毫秒 */
    while(xms--)
    {
        Delay_us(1000);
    }
}

/**
 * @brief   秒级延时
 * @param   xs: 延时时长（秒）
 * @retval  无
 * @note    内部调用Delay_ms(1000)实现
 */
void Delay_s(uint32_t xs)
{
    /* 循环调用毫秒延时，每次延时1秒 */
    while(xs--)
    {
        Delay_ms(1000);
    }
}

/*====================================================================*/
/*                      以下是旧版延时函数（保留兼容）                  */
/*====================================================================*/

/**
 * @brief   微秒级延时（粗略版）
 * @param   time: 延时时长
 * @retval  无
 * @note    使用循环实现的粗略延时，精度较低
 */
void delay_us(u16 time)
{    
    u16 i = 0;  
    while(time--)
    {
        i = 10;  /* 循环10次约1微秒（可根据需要调整） */
        while(i--);    
    }
}

/**
 * @brief   毫秒级延时（粗略版）
 * @param   time: 延时时长
 * @retval  无
 * @note    使用循环实现的粗略延时
 */
void delay_ms(u16 time)
{    
    u16 i = 0;  
    while(time--)
    {
        i = 12000;  /* 循环12000次约1毫秒（可根据需要调整） */
        while(i--);    
    }
}
