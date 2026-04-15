/**
 * @file    Timer.c
 * @brief   定时器驱动实现
 * @note    Timer3：系统时基（uwTick）
 *          Timer4：传感器采集定时器
 */

#include "stm32f10x.h"

/**
 * @brief   定时器3初始化（系统时基）
 * @param   arr 自动重装载值
 * @param   psc 预分频系数
 * @return  无
 * @note    Timer3每1ms触发一次中断，用于uwTick计数
 */
void TIM3_Init(u16 arr, u16 psc)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Period = arr - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = psc - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    TIM_Cmd(TIM3, ENABLE);
}

// 系统时基计数器
u32 uwTick = 0;

// 阀门控制标志（0=不控制, 1=立即控制）
volatile u8 valve_control_flag = 1;  // 初始为1，系统启动时立即执行一次

// 用于检测温度变化的变量（需要与其他文件共享）
volatile float g_last_preset_temp = 27.0f;  // 上次的预设温度
volatile float g_last_adc_temp = 25.0f;     // 上次的实际温度

// 阀门定时检测标志
volatile u8 valve_timer_flag = 0;
static u16 valve_timer_counter = 0;
#define VALVE_TIMER_INTERVAL 150  // 每150次TIM4中断触发一次（约15秒）

/**
 * @brief   Timer3中断服务程序（系统时基）
 */
void TIM3_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET) {
        uwTick++;  // 系统时基+1
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}

// 传感器采集相关变量（在ADC.h中声明）
extern volatile float g_adc_temp;
extern volatile float g_adc_turbidity;
extern volatile uint8_t g_adc_flag;

/**
 * @brief   定时器4初始化（传感器采集）
 * @param   arr 自动重装载值
 * @param   psc 预分频系数
 * @return  无
 * @note    Timer4用于定时触发ADC采集，避免阻塞主循环
 */
void TIM4_Init(u16 arr, u16 psc)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Period = arr - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = psc - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);
    
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);
    
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    TIM_Cmd(TIM4, ENABLE);
}

/**
 * @brief   Timer4中断服务程序（传感器采集）
 */
void TIM4_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET) {
        g_adc_flag = 1;  // 置位采集标志
        
        // 阀门定时检测：每15秒（约150次 × 100ms）触发一次
        valve_timer_counter++;
        if(valve_timer_counter >= VALVE_TIMER_INTERVAL) {
            valve_timer_counter = 0;
            valve_timer_flag = 1;  // 置位定时检测标志
        }
        
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
    }
}
