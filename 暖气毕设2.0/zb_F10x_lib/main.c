/**
 * @file    main.c
 * @brief   暖气控制系统主程序，实现温度/浊度采集、OLED菜单显示功能
 * @note    传感器数据由Timer4定时采集（100ms周期）
 *          温度传感器：A0/PA0（ADC1_CH0）
 *          浊度传感器：A1/PA1（ADC1_CH1）
 */

#include "stm32f10x.h"
#include "Timer.h"
#include "Delay.h"
#include "led.h"
#include "oled.h"
#include "key.h"
#include "menu.h"
#include "ADC.h"
#include "step_motor.h"  // 步进电机驱动
#include "usart.h"       // 串口驱动（蓝牙+语音模块）

// 外部变量声明
extern volatile u8 valve_control_flag;  // 阀门立即控制标志
extern volatile u8 valve_timer_flag;    // 阀门定时检测标志
extern volatile float g_last_preset_temp;  // 上次的预设温度

extern float preset_temp;  // 目标温度（来自menu.h）
extern uint8_t valve_mode_auto;  // 阀门模式：1=自动, 0=手动（来自menu.h）

/****************************** LED状态变量 ******************************/
static u32 led_tick = 0;      // 上次LED翻转时间（ms）
static u8 led_flag = 0;       // LED状态标志：0=亮，1=灭

/**
 * @brief   LED状态处理函数，每秒翻转一次，指示系统运行状态
 */
static void LED_process(void)
{
    if(uwTick - led_tick < 1000) return;
    led_tick = uwTick;
    led_flag = ~led_flag;
    if(led_flag) LED_OFF;
    else LED_ON;
}

/*************************************************
 * 开机界面函数（执行一次，无循环）
 *************************************************/
#define CHINESE_STR "Heating Valve Controller"
#define CHINESE_START_X 0
#define CHINESE_START_Y 12

void Boot_Interface_Show(void)
{
    uint8_t i;
    uint8_t load_bar_width = 0;

    // 1. OLED清屏初始化
    OLED_Clear();
    delay_ms(100);
    OLED_Update();

    // 2. 绘制界面边框
    OLED_DrawLine(0, 0, 127, 0);    // 上边框
    OLED_DrawLine(127, 0, 127, 63); // 右边框
    OLED_DrawLine(127, 63, 0, 63);  // 下边框
    OLED_DrawLine(0, 63, 0, 0);     // 左边框

    // 3. 内边框（加载区）
    OLED_DrawLine(10, 48, 117, 48);
    OLED_DrawLine(117, 48, 117, 58);
    OLED_DrawLine(117, 58, 10, 58);
    OLED_DrawLine(10, 58, 10, 48);
    OLED_Update();

    // 4. 显示汉字
    OLED_ShowString(CHINESE_START_X, CHINESE_START_Y, CHINESE_STR, OLED_8X16);

    // 5. 绘制加载动画
    OLED_ShowString(12, 34, "Loading...", OLED_6X8);
    OLED_Update();

    for(i = 0; i <= 107; i++)
    {
        load_bar_width = i;
        OLED_DrawRectangle(11, 49, load_bar_width, 8, OLED_FILLED);
        OLED_UpdateArea(10, 48, 108, 10);
        delay_ms(10);
    }

    // 6. 加载完成
    OLED_ClearArea(0, 34, 128, 30);
    OLED_DrawLine(0, 0, 127, 0);
    OLED_DrawLine(127, 0, 127, 63);
    OLED_DrawLine(0, 0, 0, 63);
    OLED_DrawLine(0, 63, 127, 63);
    OLED_ShowString(40, 33, "Ready!", OLED_8X16);
    OLED_Update();
    delay_ms(1000);

    OLED_Clear();
    OLED_Update();
}

/****************************** 主函数 ******************************/


/**
 * @brief   系统主函数
 */
int main(void)
{
    // 1. 配置NVIC中断分组
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    // 2. 初始化定时器3（系统时基uwTick，1ms周期）
    TIM3_Init(72, 1000);

    // 3. 初始化定时器4（传感器数据处理，100ms触发一次）
    TIM4_Init(7200, 100);  // 72MHz / 7200 / 100 = 100Hz = 10ms周期
                           // 实际每100ms读取一次DMA数据

    // 4. 初始化OLED显示屏
    OLED_Init();
    // 开机界面
    Boot_Interface_Show();

    // 5. 初始化LED
    LED_Init();

    // 6. 初始化按键
    Key_GPIO_Init();
    
    // 7. 初始化ADC
    ADC_MyInit();
    
    // 8. 初始化步进电机（阀门控制）
    Step_Motor_Init();
    
    // 9. 初始化菜单系统
    Menu_Init();
    
    // 10. 初始化串口（蓝牙+语音模块）
    Usart1_Init(9600);   // 蓝牙通信，波特率9600
    Usart2_Init(9600);    // 语音模块通信，波特率9600
    
    // 11. 主循环
    while(1)
    {
        LED_process();        // LED状态处理
        ADC_Process();        // 传感器采集处理
        Menu_Process();       // 菜单处理
        
        // ========== 非阻塞阀门控制 ==========
        // 根据阀门模式决定控制方式
        if(valve_mode_auto) {
            // 自动模式：根据温差自动调节
            // 检测SET_T温度变化 → 立即触发阀门调节
            if(preset_temp != g_last_preset_temp) {
                g_last_preset_temp = preset_temp;
                Valve_Auto_Control(g_adc_temp, preset_temp);  // 请求新目标
            }
            
            // 定时检测（每15秒）→ 触发阀门调节
            if(valve_timer_flag) {
                valve_timer_flag = 0;
                Valve_Auto_Control(g_adc_temp, preset_temp);  // 请求新目标
            }
        }
        // 手动模式：Valve_Request_Level由menu.c中的编辑逻辑调用
        
        // 非阻塞执行：每次只移动1步（约8ms），避免按键卡顿
        Valve_Process(1);
    }
}
