/**
 * @file    step_motor.h
 * @brief   步进电机驱动头文件，控制28BYJ48步进电机
 * @note    硬件连接：
 *          PA4 → A相
 *          PA5 → B相
 *          PA6 → C相
 *          PA7 → D相
 * 
 * ========== 阀门角度控制说明 ==========
 * 【核心思想】：记住上电时的零点位置，所有角度都相对于零点计算
 * 
 * - 上电位置定义为【零点/零度】
 * - 6个档位，每档对应一个【相对零点】的角度
 * - 电机转动角度始终相对于零点计算
 * - 电机到达目标档位后停止，保持该角度不变
 * 
 * 档位与角度/步数对照（每档30度，相对于零点）：
 * | 档位 | 相对0点的角度 | 步数(累计) | 说明              |
 * |------|--------------|-----------|------------------|
 * | 0级  | 0度          | 0步       | 零点位置          |
 * | 1级  | 相对0点30度  | 350步     | 转动30度停止      |
 * | 2级  | 相对0点60度  | 700步     | 转动60度停止      |
 * | 3级  | 相对0点90度  | 1050步    | 转动90度停止      |
 * | 4级  | 相对0点120度 | 1400步    | 转动120度停止     |
 * | 5级  | 相对0点150度 | 1750步    | 转动150度停止     |
 * 
 * 温差与档位关系：
 * | 温差范围        | 档位 | 相对0点角度 | 说明              |
 * |-----------------|------|-------------|------------------|
 * | <= -6.5°C      | 5级  | 相对0点150度 | 阀门大开          |
 * | -6.5 ~ -3.5°C | 4级  | 相对0点120度 | 阀门大开          |
 * | -3.5 ~ -0.5°C | 3级  | 相对0点90度  | 阀门中开          |
 * | -0.5 ~ +0.5°C | 保持 | --          | 【死区】电机停止   |
 * | +0.5 ~ +3.5°C | 2级  | 相对0点60度  | 阀门中小开        |
 * | +3.5 ~ +6.5°C | 1级  | 相对0点30度  | 阀门小开          |
 * | > +6.5°C       | 0级  | 0度(零点)   | 阀门关闭          |
 * 
 * 【示例说明】：
 * - 假设电机初始在5档（已相对零点转了150度）
 * - 温差变为+3度，目标是2档（相对0点60度）
 * - 需要回转：150度 - 60度 = 90度，电机回转90度后停止
 * - 此时电机停在2档位置（相对零点60度）
 */

#ifndef __STEP_MOTOR_H
#define __STEP_MOTOR_H

#include "stm32f10x.h"

// 旋转方向
#define MOTOR_DIR_CW     0   // 顺时针
#define MOTOR_DIR_CCW    1   // 逆时针

// 阀门档位定义（0-5级共6档，每档30度）
#define VALVE_LEVEL_MIN  0    // 阀门最小档位（0度）
#define VALVE_LEVEL_MAX  5    // 阀门最大档位（150度）

// 阀门步数定义
// 每档30度 = 350步（2100步/180度 * 30度 ≈ 350步）
#define VALVE_STEP_PER_LEVEL 350  // 每档对应的步数
#define VALVE_TOTAL_STEPS 1750    // 最大档位(5级)对应的步数

// 步进电机引脚定义（替换PB2为PA7，避开Boot1）
#define MOTOR_A_PIN    GPIO_Pin_4  // A相
#define MOTOR_B_PIN    GPIO_Pin_5  // B相
#define MOTOR_C_PIN    GPIO_Pin_6  // C相
#define MOTOR_D_PIN    GPIO_Pin_7  // D相
#define MOTOR_PORT     GPIOA       // 端口A
#define MOTOR_RCC      RCC_APB2Periph_GPIOA

// 函数声明
void Step_Motor_Init(void);                          // 电机初始化
void Step_Motor_Set_Dir(u8 dir);                    // 设置旋转方向
void Step_Motor_Set_Speed(float rpm, float step_angle); // 设置转速(RPM)
void Step_Motor_Run_Step(u16 step_num);             // 运行指定步数
void Step_Motor_Cont_Run(u8 en);                    // 连续运行/停止
u16  Step_Motor_Get_Total_Step(void);                // 获取累计步数

// 阀门控制函数（新增）
u8 Valve_Set_Level(u8 level);                       // 设置阀门等级
void Valve_Stop(void);                               // 停止阀门
u8  Valve_Get_Level(void);                           // 获取当前阀门等级

// 阀门自动控制函数
void Valve_Auto_Control(float actual_temp, float target_temp);  // 根据温差自动调节阀门

// 非阻塞阀门控制函数
void Valve_Request_Level(u8 level);   // 请求设置阀门等级（非阻塞）
u8 Valve_Process(u8 max_steps);       // 阀门处理函数（非阻塞，每次移动少量步数）
u8 Valve_Is_Moving(void);              // 检查阀门是否在移动中

#endif
