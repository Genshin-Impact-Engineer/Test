/**
 * @file    key.c
 * @brief   按键驱动实现，外部中断+软件消抖实现按键检测
 * @note    中断内有按键记录时间和标志，主循环通过系统定时器判断消抖时间
 *          消抖时间20ms可通过宏修改。
 * @version 2.0
 * @date    2025-03-21
 */

#include "key.h"
#include "Timer.h"

#define KEY_DEBOUNCE_MS    20    // 按键消抖时间（ms）

// 按键信息结构体：记录触发时间和待处理标志
typedef struct {
    uint32_t trigger_time;   // 按键触发时间（ms）
    uint8_t pending;         // 待处理标志：0=已处理，1=待处理
} Key_Info_t;

// 4个按键的信息数组
static Key_Info_t key_info[4] = {0};
// 上次报告的按键状态（用于去重）
static Key_State last_reported_key = KEY_NONE;

/**
 * @brief   初始化GPIO和外部中断
 * @param   无
 * @return  无
 * @note    配置PB12~PB15为上拉输入，配置EXTI12~15下降沿触发中断
 */
void Key_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 使能GPIOB和AFIO时钟
    RCC_APB2PeriphClockCmd(KEY_RCC | RCC_APB2Periph_AFIO, ENABLE);

    // 配置PB12~PB15为上拉输入
    GPIO_InitStructure.GPIO_Pin = KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;    // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY_PORT, &GPIO_InitStructure);

    // 配置EXTI外部中断线
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource12);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource13);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource14);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOB, GPIO_PinSource15);

    // 配置EXTI12~15为下降沿触发中断
    EXTI_InitStructure.EXTI_Line = EXTI_Line12 | EXTI_Line13 | EXTI_Line14 | EXTI_Line15;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;  // 按键按下时为低电平
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    // 配置NVIC中断优先级（抢占优先级2，子优先级1）
    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**
 * @brief   外部中断服务程序（EXTI12~15）
 * @param   无
 * @return  无
 * @note    记录按键触发时间，置位待处理标志
 */
void EXTI15_10_IRQHandler(void)
{
    uint32_t now = uwTick;  // 获取当前时间

    // K1按下（PB12）
    if(EXTI_GetITStatus(EXTI_Line12) != RESET) {
        key_info[0].trigger_time = now;
        key_info[0].pending = 1;
        EXTI_ClearITPendingBit(EXTI_Line12);
    }
    // K2按下（PB13）
    if(EXTI_GetITStatus(EXTI_Line13) != RESET) {
        key_info[1].trigger_time = now;
        key_info[1].pending = 1;
        EXTI_ClearITPendingBit(EXTI_Line13);
    }
    // K3按下（PB14）
    if(EXTI_GetITStatus(EXTI_Line14) != RESET) {
        key_info[2].trigger_time = now;
        key_info[2].pending = 1;
        EXTI_ClearITPendingBit(EXTI_Line14);
    }
    // K4按下（PB15）
    if(EXTI_GetITStatus(EXTI_Line15) != RESET) {
        key_info[3].trigger_time = now;
        key_info[3].pending = 1;
        EXTI_ClearITPendingBit(EXTI_Line15);
    }
}

/**
 * @brief   获取按键状态（供主循环调用）
 * @param   无
 * @return  Key_State 按键状态
 * @note    消抖时间20ms后判断引脚电平，低电平则报告按键按下
 */
Key_State Key_Get_State(void)
{
    uint32_t now = uwTick;
    Key_State result = KEY_NONE;

    // 遍历4个按键
    for(uint8_t i=0; i<4; i++) {
        if(key_info[i].pending) {
            // 消抖时间到
            if((now - key_info[i].trigger_time) >= KEY_DEBOUNCE_MS) {
                uint16_t pin;
                Key_State state;
                
                // 根据索引确定引脚和状态
                switch(i) {
                    case 0: pin = KEY1_PIN; state = KEY1_PRESS; break;
                    case 1: pin = KEY2_PIN; state = KEY2_PRESS; break;
                    case 2: pin = KEY3_PIN; state = KEY3_PRESS; break;
                    case 3: pin = KEY4_PIN; state = KEY4_PRESS; break;
                    default: continue;
                }
                
                // 再次确认引脚电平（防抖动）
                if(GPIO_ReadInputDataBit(KEY_PORT, pin) == 0) {
                    // 避免同一按键连续报告
                    if(last_reported_key != state) {
                        result = state;
                        last_reported_key = state;
                    }
                }
                // 清除待处理标志
                key_info[i].pending = 0;
            }
        }
    }

    // 无按键时清除状态
    if(result == KEY_NONE) {
        last_reported_key = KEY_NONE;
    }

    return result;
}

/**
 * @brief   清除指定按键的待处理标志
 * @param   key 要清除的按键
 * @return  无
 * @note    用于长按检测后，防止再次触发短按
 */
void Key_Clear_Pending(Key_State key)
{
    if(key == KEY3_PRESS) {
        key_info[2].pending = 0;
        if(last_reported_key == KEY3_PRESS) last_reported_key = KEY_NONE;
    }
    else if(key == KEY4_PRESS) {
        key_info[3].pending = 0;
        if(last_reported_key == KEY4_PRESS) last_reported_key = KEY_NONE;
    }
}
