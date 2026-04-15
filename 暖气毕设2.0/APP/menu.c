/**
 * @file    menu.c
 * @brief   菜单系统实现，支持多页循环显示、K3/K4长按翻页、K1~K4短按操作
 * @note    采用OLED_8X16全屏显示
 *          功能说明：
 *          - 长按K4≥1秒：切换到下一页
 *          - 长按K3≥1秒：切换到上一页
 *          - 页面1（数据页）：K3/K4循环选择，K2进入编辑模式，K1取消编辑
 *          - 页面2（设置页）：显示固定参数（最高/最低温度、浊度阈值）
 *          - 页面3（报警页）：温度或浊度越界时自动弹出，K1返回数据页
 *          -时间:26-3-30-21:50
 */

#include "menu.h"
#include "key.h"
#include "oled.h"
#include "Timer.h"
#include "step_motor.h"  // 步进电机驱动
#include <stdio.h>

/****************************** 全局变量定义（外部可见） ******************************/
uint8_t current_page = MENU_PAGE1;
float temp_max = TEMP_MAX_DEFAULT;
float temp_min = TEMP_MIN_DEFAULT;
float turb_threshold = TURB_THRESHOLD_DEFAULT;
float preset_temp = 27.0f;
uint8_t water_valve_level = 0;
uint8_t selected_item = 0;
uint8_t valve_mode_auto = 1;  // 默认自动模式

extern volatile float g_adc_temp;
extern volatile float g_adc_turbidity;

uint8_t temp_alarm_flag = 0;
uint8_t turb_alarm_flag = 0;

// 外部串口发送函数声明（语音模块用）
extern void Usart2_Send_String(char* str);

/****************************** 长按检测相关 ******************************/
#define LONG_PRESS_TIME    1000    // 长按判定时间（ms）
static uint32_t long_press_start_time = 0;
static uint8_t long_press_key = 0;            // 0=无, 1=K3, 2=K4
static uint8_t long_press_triggered = 0;

/****************************** 页面1编辑模式相关变量 ******************************/
static uint8_t edit_mode = 0;
static float temp_preset_temp;
static uint8_t temp_water_valve_level;
static uint8_t temp_valve_mode;  // 编辑时的临时模式变量
static uint8_t blink_visible = 1;
static uint32_t blink_last_time = 0;
#define BLINK_INTERVAL   300

/****************************** 报警延迟检测相关（5秒延迟）- 温度和浊度独立 ******************************/
#define ALARM_DELAY_MS    5000    // 报警延迟时间（5秒）
#define ALARM_COOLING_MS  60000   // 报警确认后冷却时间（60秒）

// ========== 温度报警相关 ==========
static uint32_t temp_alarm_start_time = 0;     // 温度异常开始时间
static uint8_t temp_alarm_delay_flag = 0;      // 温度延迟状态：0=未计时, 1=计时中, 2=可报警
static uint8_t temp_alarm_acknowledged = 0;    // 温度报警已确认
static uint32_t temp_alarm_ack_time = 0;        // 温度报警确认时间

// ========== 浊度报警相关 ==========
static uint32_t turb_alarm_start_time = 0;     // 浊度异常开始时间
static uint8_t turb_alarm_delay_flag = 0;      // 浊度延迟状态：0=未计时, 1=计时中, 2=可报警
static uint8_t turb_alarm_acknowledged = 0;    // 浊度报警已确认
static uint32_t turb_alarm_ack_time = 0;       // 浊度报警确认时间

// ========== 通用 ==========
static uint8_t last_temp_error = 0;      // 上次温度异常状态
static uint8_t last_turb_error = 0;      // 上次浊度异常状态

/****************************** 报警前页面记录 ******************************/
static uint8_t last_page_before_alarm = MENU_PAGE1;  // 报警前的页面（用于K1返回）

/****************************** 显示相关静态函数 ******************************/

static void UI_ClearMenuArea(void)
{
    OLED_ClearArea(0, 0, 128, 64);
}

static void UI_UpdateMenuArea(void)
{
    OLED_UpdateArea(0, 0, 128, 64);
}

static void Draw_Page2(void)
{
    // Page1（数据页）：Turb/Temp/SetT/Setting
    char str[20];
    
    // ========== 第一行：浊度 Turb ==========
    sprintf(str, "Turb:%.1f", g_adc_turbidity);
    OLED_ShowString(0, 0, str, OLED_8X16);
    OLED_ShowString(72, 0, "NTU", OLED_8X16);
    
    // ========== 第二行：温度 Temp ==========
    sprintf(str, "Temp:%.1f", g_adc_temp);
    OLED_ShowString(0, 16, str, OLED_8X16);
    OLED_ShowString(72, 16, "C", OLED_8X16);
    
    // ========== 第三行：预设温度 SetT ==========
    if(edit_mode && selected_item == 2 && blink_visible) {
        sprintf(str, "SetT:%.1f", temp_preset_temp);
    } else if(edit_mode && selected_item == 2) {
        sprintf(str, "SetT:     ");
    } else {
        sprintf(str, "SetT:%.1f", preset_temp);
    }
    OLED_ShowString(0, 32, str, OLED_8X16);
    OLED_ShowString(72, 32, "C", OLED_8X16);
    
    // ========== 第四行：阀门档位 Setting ==========
    if(valve_mode_auto) {
        // 自动模式：显示温差对应的目标档位
        sprintf(str, "Setting:%d", Valve_Get_Level());
        OLED_ShowString(0, 48, str, OLED_8X16);
    } else {
        // 手动模式：显示/编辑手动设置的档位
        if(edit_mode && selected_item == 1 && blink_visible) {
            sprintf(str, "Setting:%d", temp_water_valve_level);
        } else if(edit_mode && selected_item == 1) {
            sprintf(str, "Setting: ");
        } else {
            sprintf(str, "Setting:%d", water_valve_level);
        }
        OLED_ShowString(0, 48, str, OLED_8X16);
    }
    
    // ========== 选中项矩形框 ==========
    if(!edit_mode && selected_item == 2) {
        OLED_DrawRectangle(0, 32, 90, 16, OLED_UNFILLED);
    } else if(!edit_mode && selected_item == 1 && !valve_mode_auto) {
        // 只有手动模式下Setting才可选中
        OLED_DrawRectangle(0, 48, 80, 16, OLED_UNFILLED);
    }
}

static void Draw_Page1(void)
{
    // 原Page1：固定参数显示（现为MENU_PAGE2）
    char str[20];
    
    // ========== 第一行：最高温度 MaxT ==========
    snprintf(str, sizeof(str), "MaxT:%.1f", temp_max);
    OLED_ShowString(0, 0, str, OLED_8X16);
    OLED_ShowString(72, 0, "C", OLED_8X16);
    
    // ========== 第二行：最低温度 MinT ==========
    snprintf(str, sizeof(str), "MinT:%.1f", temp_min);
    OLED_ShowString(0, 16, str, OLED_8X16);
    OLED_ShowString(72, 16, "C", OLED_8X16);
    
    // ========== 第三行：浊度阈值 Turb ==========
    sprintf(str, "Turb:%.1f", turb_threshold);
    OLED_ShowString(0, 32, str, OLED_8X16);
    OLED_ShowString(72, 32, "NTU", OLED_8X16);
    
    // ========== 第四行：阀门模式 Mode ==========
    if(edit_mode && selected_item == 1 && blink_visible) {
        if(temp_valve_mode) {
            OLED_ShowString(0, 48, "Mode:Auto ", OLED_8X16);
        } else {
            OLED_ShowString(0, 48, "Mode:Manual", OLED_8X16);
        }
    } else if(edit_mode && selected_item == 1) {
        OLED_ShowString(0, 48, "Mode:      ", OLED_8X16);
    } else {
        if(valve_mode_auto) {
            OLED_ShowString(0, 48, "Mode:Auto ", OLED_8X16);
        } else {
            OLED_ShowString(0, 48, "Mode:Manual", OLED_8X16);
        }
    }
    
    // ========== 选中项矩形框 ==========
    if(!edit_mode && selected_item == 1) {
        OLED_DrawRectangle(0, 48, 128, 16, OLED_UNFILLED);
    }
}

static void Draw_Page3(void)
{
    // 温度判断：在min_temp和max_temp之间为正常
    uint8_t temp_normal = (g_adc_temp >= temp_min && g_adc_temp <= temp_max);
    
    // 浊度判断：小于阈值为正常
    uint8_t turb_normal = (g_adc_turbidity < turb_threshold);
    
    // ========== 第一行：回水温度状态 ==========
    if(temp_normal) {
        OLED_ShowString(0, 0, "Temp:OK!", OLED_8X16);
    } else {
        OLED_ShowString(0, 0, "Temp:ERROR!", OLED_8X16);
    }
    
    // ========== 第二行：温度异常时显示"请检查" ==========
    if(!temp_normal) {
        OLED_ShowString(0, 16, "Please check", OLED_8X16);
    }
    
    // ========== 第三行：水质状态 ==========
    if(turb_normal) {
        OLED_ShowString(0, 32, "Water:OK!", OLED_8X16);
    } else {
        OLED_ShowString(0, 32, "Water:ERROR!", OLED_8X16);
    }
    
    // ========== 第四行：浊度异常时显示"建议处理" ==========
    if(!turb_normal) {
        OLED_ShowString(0, 48, "Need handle", OLED_8X16);
    }
}

static void UI_Refresh(void)
{
    UI_ClearMenuArea();
    switch(current_page) {
        case MENU_PAGE1: Draw_Page2(); break;  // Page1显示原Page2（数据页）
        case MENU_PAGE2: Draw_Page1(); break;  // Page2显示原Page1（设置页）
        case MENU_PAGE3: Draw_Page3(); break;  // Page3报警页
    }
    UI_UpdateMenuArea();
}

static void Switch_Page_Next(void)
{
    // 页面顺序：MENU_PAGE1(数据页) → MENU_PAGE2(设置页) → MENU_PAGE3(报警页) → MENU_PAGE1
    if(current_page == MENU_PAGE1) {
        current_page = MENU_PAGE2;  // 数据页 → 设置页
    } else if(current_page == MENU_PAGE2) {
        current_page = MENU_PAGE3;  // 设置页 → 报警页
    } else {
        current_page = MENU_PAGE1;  // 报警页 → 数据页
    }
    edit_mode = 0;
    selected_item = 0;
    UI_Refresh();
}

static void Switch_Page_Prev(void)
{
    // 反向切换：MENU_PAGE3 → MENU_PAGE2 → MENU_PAGE1 → MENU_PAGE3
    if(current_page == MENU_PAGE1) {
        current_page = MENU_PAGE3;  // 数据页 → 报警页
    } else if(current_page == MENU_PAGE2) {
        current_page = MENU_PAGE1;  // 设置页 → 数据页
    } else {
        current_page = MENU_PAGE2;  // 报警页 → 设置页
    }
    edit_mode = 0;
    selected_item = 0;
    UI_Refresh();
}

static void Handle_Short_Press(Key_State key)
{
    // ========== Page3报警页：K1返回报警前的页面 ==========
    if(current_page == MENU_PAGE3) {
        if(key == KEY1_PRESS) {
            // 返回报警前的页面（可能是Page1或Page2）
            current_page = last_page_before_alarm;
            // 设置温度和浊度的确认状态和确认时间（进入各自冷却期）
            if(temp_alarm_delay_flag == 2) {
                temp_alarm_acknowledged = 1;
                temp_alarm_ack_time = uwTick;
            }
            if(turb_alarm_delay_flag == 2) {
                turb_alarm_acknowledged = 1;
                turb_alarm_ack_time = uwTick;
            }
            UI_Refresh();
        }
        return;
    }
    
    // ========== Page1数据页：K3/K4选择，K2编辑 ==========
    if(current_page == MENU_PAGE1) {
        if(edit_mode) {
            // ========== 编辑模式 ==========
            if(selected_item == 2) {
                // SetT编辑：K3/K4调节温度
                if(key == KEY4_PRESS) {
                    temp_preset_temp += PRESET_TEMP_STEP;
                    if(temp_preset_temp > PRESET_TEMP_MAX) temp_preset_temp = PRESET_TEMP_MAX;
                    UI_Refresh();
                }
                else if(key == KEY3_PRESS) {
                    temp_preset_temp -= PRESET_TEMP_STEP;
                    if(temp_preset_temp < PRESET_TEMP_MIN) temp_preset_temp = PRESET_TEMP_MIN;
                    UI_Refresh();
                }
                else if(key == KEY2_PRESS) {
                    preset_temp = temp_preset_temp;
                    edit_mode = 0;
                    UI_Refresh();
                }
                else if(key == KEY1_PRESS) {
                    edit_mode = 0;
                    UI_Refresh();
                }
            }
            else if(selected_item == 1) {
                // Setting编辑（仅手动模式）：K3/K4调节档位
                if(!valve_mode_auto) {  // 只有手动模式才能编辑Setting
                    if(key == KEY4_PRESS) {
                        if(temp_water_valve_level < WATER_VALVE_MAX) temp_water_valve_level++;
                        UI_Refresh();
                    }
                    else if(key == KEY3_PRESS) {
                        if(temp_water_valve_level > WATER_VALVE_MIN) temp_water_valve_level--;
                        UI_Refresh();
                    }
                    else if(key == KEY2_PRESS) {
                        water_valve_level = temp_water_valve_level;
                        Valve_Request_Level(water_valve_level);  // 手动模式下直接设置阀门
                        edit_mode = 0;
                        UI_Refresh();
                    }
                    else if(key == KEY1_PRESS) {
                        edit_mode = 0;
                        UI_Refresh();
                    }
                } else {
                    // 自动模式下不能编辑Setting
                    if(key == KEY1_PRESS || key == KEY2_PRESS) {
                        edit_mode = 0;
                        UI_Refresh();
                    }
                }
            }
        }
        else {
            // ========== 非编辑模式 ==========
            // K2进入编辑模式
            if(key == KEY2_PRESS) {
                if(selected_item == 2 || (selected_item == 1 && !valve_mode_auto)) {
                    temp_preset_temp = preset_temp;
                    temp_water_valve_level = water_valve_level;
                    edit_mode = 1;
                    UI_Refresh();
                }
                return;
            }
            
            // K3/K4循环选择
            if(key == KEY4_PRESS) {
                if(selected_item == 0) selected_item = 1;
                else if(selected_item == 1) selected_item = 2;
                else if(selected_item == 2) selected_item = 0;
                UI_Refresh();
            }
            else if(key == KEY3_PRESS) {
                if(selected_item == 0) selected_item = 2;
                else if(selected_item == 2) selected_item = 1;
                else if(selected_item == 1) selected_item = 0;
                UI_Refresh();
            }
        }
    }
    
    // ========== Page2设置页：Mode模式编辑 ==========
    if(current_page == MENU_PAGE2) {
        if(edit_mode) {
            // Mode编辑：K3/K4切换Auto/Manual
            if(key == KEY4_PRESS || key == KEY3_PRESS) {
                temp_valve_mode = !temp_valve_mode;  // 切换模式
                UI_Refresh();
            }
            // K2确认，K1取消
            else if(key == KEY2_PRESS) {
                valve_mode_auto = temp_valve_mode;
                edit_mode = 0;
                UI_Refresh();
            }
            else if(key == KEY1_PRESS) {
                edit_mode = 0;
                UI_Refresh();
            }
        }
        else {
            // 非编辑模式：K3/K4选择，K2进入编辑
            if(key == KEY4_PRESS || key == KEY3_PRESS) {
                // K3/K4切换选择状态（0=未选中，1=Mode）
                if(selected_item == 0) selected_item = 1;
                else selected_item = 0;
                UI_Refresh();
            }
            else if(key == KEY2_PRESS) {
                if(selected_item == 1) {
                    temp_valve_mode = valve_mode_auto;
                    edit_mode = 1;
                    UI_Refresh();
                }
            }
        }
    }
}

/****************************** 菜单处理函数 ******************************/

/**
 * @brief   菜单处理主函数
 * @note    在主循环中调用，处理闪烁、按键检测、异常自动跳转
 * @note    【报警功能】：温度和浊度各自独立检测报警，互不影响
 *          - 异常持续5秒后才报警
 *          - 报警后按K1返回，60秒内不重复报警
 *          - 异常消除后，下次异常重新计时
 */
void Menu_Process(void)
{
    uint32_t now = uwTick;
    static uint32_t page2_refresh_tick = 0;
    
    // ========== 异常检测 ==========
    uint8_t temp_error = (g_adc_temp < temp_min || g_adc_temp > temp_max);
    uint8_t turb_error = (g_adc_turbidity >= turb_threshold);
    
    // ========== 温度报警逻辑（独立）==========
    // temp_alarm_delay_flag: 0=未计时, 1=计时中, 2=可报警
    if(temp_error) {
        if(!last_temp_error) {
            // 温度从正常变为异常，开始计时
            temp_alarm_start_time = now;
            temp_alarm_delay_flag = 1;
        } else if(temp_alarm_delay_flag == 1) {
            // 温度异常持续中，检查是否超时5秒
            if(now - temp_alarm_start_time >= ALARM_DELAY_MS) {
                temp_alarm_delay_flag = 2;  // 可报警
            }
        }
    } else {
        // 温度恢复正常
        temp_alarm_delay_flag = 0;
        temp_alarm_acknowledged = 0;  // 清除确认状态
    }
    
    // 温度冷却期逻辑
    if(temp_alarm_acknowledged && (temp_alarm_delay_flag == 2)) {
        if(now - temp_alarm_ack_time >= ALARM_COOLING_MS) {
            temp_alarm_acknowledged = 0;  // 冷却期结束
        }
    }
    
    // ========== 浊度报警逻辑（独立）==========
    // turb_alarm_delay_flag: 0=未计时, 1=计时中, 2=可报警
    if(turb_error) {
        if(!last_turb_error) {
            // 浊度从正常变为异常，开始计时
            turb_alarm_start_time = now;
            turb_alarm_delay_flag = 1;
        } else if(turb_alarm_delay_flag == 1) {
            // 浊度异常持续中，检查是否超时5秒
            if(now - turb_alarm_start_time >= ALARM_DELAY_MS) {
                turb_alarm_delay_flag = 2;  // 可报警
            }
        }
    } else {
        // 浊度恢复正常
        turb_alarm_delay_flag = 0;
        turb_alarm_acknowledged = 0;  // 清除确认状态
    }
    
    // 浊度冷却期逻辑
    if(turb_alarm_acknowledged && (turb_alarm_delay_flag == 2)) {
        if(now - turb_alarm_ack_time >= ALARM_COOLING_MS) {
            turb_alarm_acknowledged = 0;  // 冷却期结束
        }
    }
    
    // 记录本次状态
    last_temp_error = temp_error;
    last_turb_error = turb_error;
    
    // ========== 判断是否需要跳转到Page3 ==========
    // 温度可以报警且未确认
    uint8_t temp_can_alarm = (temp_alarm_delay_flag == 2) && !temp_alarm_acknowledged;
    // 浊度可以报警且未确认
    uint8_t turb_can_alarm = (turb_alarm_delay_flag == 2) && !turb_alarm_acknowledged;
    
    // 任一可以报警时跳转Page3
    if((temp_can_alarm || turb_can_alarm) && current_page != MENU_PAGE3) {
        last_page_before_alarm = current_page;  // 记录来源页面
        current_page = MENU_PAGE3;
        
        // ========== 通过USART2发送报警信息到语音模块 ==========
        if(temp_can_alarm && turb_can_alarm) {
            Usart2_Send_String("temp and water");  // 温度和浊度都异常
        } else if(temp_can_alarm) {
            Usart2_Send_String("temp");  // 仅温度异常
        } else if(turb_can_alarm) {
            Usart2_Send_String("water");  // 仅浊度异常
        }
        
        UI_Refresh();
    }
    
    // ========== Page1数据页 定时刷新（每500ms刷新一次显示）==========
    if(current_page == MENU_PAGE1 && !edit_mode) {
        if(now - page2_refresh_tick >= 500) {
            page2_refresh_tick = now;
            UI_Refresh();
        }
    }
    
    // ========== Page3 定时刷新（每500ms刷新一次显示）==========
    if(current_page == MENU_PAGE3) {
        if(now - page2_refresh_tick >= 500) {
            page2_refresh_tick = now;
            UI_Refresh();
        }
    }
    
    // ========== 闪烁处理 ==========
    if(edit_mode) {
        if(now - blink_last_time > BLINK_INTERVAL) {
            blink_visible = !blink_visible;
            blink_last_time = now;
            UI_Refresh();
        }
    } else {
        blink_visible = 1;
    }
    
    // ========== 长按检测（独立于短按处理）==========
    // 使用 key.c 中的 GPIO 读取逻辑
    uint8_t k4_pressed = (GPIO_ReadInputDataBit(KEY_PORT, KEY4_PIN) == 0);
    uint8_t k3_pressed = (GPIO_ReadInputDataBit(KEY_PORT, KEY3_PIN) == 0);
    
    // 状态机
    if(long_press_key == 0) {
        // 空闲状态：检测首次按下
        if(k4_pressed) {
            long_press_key = 2;
            long_press_start_time = now;
            long_press_triggered = 0;
        }
        else if(k3_pressed) {
            long_press_key = 1;
            long_press_start_time = now;
            long_press_triggered = 0;
        }
    }
    else {
        // 检测状态：判断长按还是短按
        uint8_t same_key_pressed;
        if(long_press_key == 2) same_key_pressed = k4_pressed;
        else same_key_pressed = k3_pressed;
        
        if(same_key_pressed) {
            // 按键仍按下
            if(!long_press_triggered && (now - long_press_start_time) >= LONG_PRESS_TIME) {
                // 触发长按
                if(long_press_key == 2) Switch_Page_Next();
                else Switch_Page_Prev();
                long_press_triggered = 1;
                // 清除 pending 防止短按
                if(long_press_key == 2) Key_Clear_Pending(KEY4_PRESS);
                else Key_Clear_Pending(KEY3_PRESS);
            }
        }
        else {
            // 按键释放，未触发长按
            if(!long_press_triggered) {
                if(long_press_key == 2) Handle_Short_Press(KEY4_PRESS);
                else if(long_press_key == 1) Handle_Short_Press(KEY3_PRESS);
            }
            // 重置状态
            long_press_key = 0;
            long_press_start_time = 0;
            long_press_triggered = 0;
        }
    }
    
    // ========== 短按处理（由 key.c 的中断触发）==========
    Key_State key = Key_Get_State();
    if(key != KEY_NONE && long_press_key == 0) {
        // 只有在空闲状态才处理备用短按
        Handle_Short_Press(key);
    }
}

void Menu_Init(void)
{
    current_page = MENU_PAGE1;  // 初始显示数据页
    selected_item = 0;
    edit_mode = 0;
    temp_max = TEMP_MAX_DEFAULT;
    temp_min = TEMP_MIN_DEFAULT;
    turb_threshold = TURB_THRESHOLD_DEFAULT;
    preset_temp = 27.0f;
    water_valve_level = 0;
    temp_alarm_flag = 0;
    turb_alarm_flag = 0;
    UI_ClearMenuArea();
    UI_Refresh();
}
