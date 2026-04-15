/**
 * @file    Delay.h
 * @brief   延时函数头文件
 * @note    声明延时相关函数
 */

#ifndef __DELAY_H
#define __DELAY_H

/*===========================================
 * 精确延时函数（使用SysTick）
 *===========================================*/

/**
 * @brief   微秒级延时
 * @param   us: 延时时长（微秒）
 */
void Delay_us(uint32_t us);

/**
 * @brief   毫秒级延时
 * @param   ms: 延时时长（毫秒）
 */
void Delay_ms(uint32_t ms);

/**
 * @brief   秒级延时
 * @param   s: 延时时长（秒）
 */
void Delay_s(uint32_t s);

/*===========================================
 * 粗略延时函数（使用循环）
 *===========================================*/

/**
 * @brief   微秒级延时（粗略版）
 * @param   time: 延时时长
 */
void delay_us(u16 time);

/**
 * @brief   毫秒级延时（粗略版）
 * @param   time: 延时时长
 */
void delay_ms(u16 time);

#endif
