/**
 * @file    stepper.c
 * @brief   步进电机控制模块：28BYJ-48 半步驱动
 *          PA4-PA7 驱动 ULN2003，TIM2 100Hz 中断驱动步进
 *          支持 0-5 共 6 档阀门位置 + 自动温控调节
 */

#include "stepper.h"

/*
 * 28BYJ-48 半步驱动 8 步相位序列（4 相，A-B-C-D 对应 PA4-PA7）
 *
 * 步序 | D(PA7) C(PA6) B(PA5) A(PA4) | 十六进制
 *  0   |   0      0      0      1     |  0x10
 *  1   |   0      0      1      1     |  0x30
 *  2   |   0      0      1      0     |  0x20
 *  3   |   0      1      1      0     |  0x60
 *  4   |   0      1      0      0     |  0x40
 *  5   |   1      1      0      0     |  0xC0
 *  6   |   1      0      0      0     |  0x80
 *  7   |   1      0      0      1     |  0x90
 *
 * 半步模式相比全步（4 步）转矩更平滑、分辨率更高（5.625°/步 → 2.8125°/半步）
 * ULN2003 达林顿阵列提供电流驱动（500mA/通道），反向二极管保护
 */
static const uint8_t step_table[8] = {0x10, 0x30, 0x20, 0x60, 0x40, 0xC0, 0x80, 0x90};

/* 全局步进电机实例 */
Stepper_t hstepper = {0};

/*
 * Stepper_Init —— 步进电机初始化
 * - 所有状态清零
 * - GPIOA PA4-PA7 输出强制为 0（通过 BRR 位复位寄存器）
 * - 电机保持在初始位置（0 档）
 * 注意：初始化不会执行归零校准，假设机械位置与 0 档一致
 */
void Stepper_Init(void) {
    hstepper.step_index = 0;
    hstepper.current_step = 0;
    hstepper.target_step = 0;
    hstepper.current_gear = 0;
    hstepper.target_gear = 0;
    hstepper.running = 0;
    hstepper.dir = 1;
    GPIOA->BRR = 0x00F0;  /* 低 8 位移位寄存器复位：PA4-PA7 = 0 */
}

/*
 * Stepper_SetGear —— 设置目标档位（0-5）
 * @param gear 目标档位（0=全关, 1-5=开度递增）
 *
 * 电机从当前位置转到目标位置：
 * - 计算目标位置：target_step = gear * STEPPER_GEAR_STEPS
 * - 根据方向差设置旋转方向
 * - 如果已在目标位置则立即停止
 * - running=1 后，TIM2 中断中逐步执行 Stepper_TIM_Step()
 *
 * 每档 350 步，对应约 30° 阀门旋转
 * 0 档 = 0 步   | 1 档 = 350 步  | 2 档 = 700 步
 * 3 档 = 1050 步 | 4 档 = 1400 步 | 5 档 = 1750 步
 */
void Stepper_SetGear(uint8_t gear) {
    if (gear > STEPPER_MAX_GEAR) gear = STEPPER_MAX_GEAR;
    hstepper.target_gear = gear;
    hstepper.current_gear = gear;  /* 立即更新当前档位：上报/显示反应用户意图，不等电机到位 */
    hstepper.target_step = STEPPER_GEAR_TOTAL(gear);
    /* 比较当前位置与目标位置，决定旋转方向 */
    if (hstepper.target_step > hstepper.current_step) {
        hstepper.dir = 1;  /* 顺时针（开阀） */
    } else if (hstepper.target_step < hstepper.current_step) {
        hstepper.dir = 0;  /* 逆时针（关阀） */
    } else {
        hstepper.running = 0;  /* 已到位 */
        return;
    }
    hstepper.running = 1;
}

/* 获取当前实际档位（注意：电机运行中时反映到达的最后一个目标位置） */
uint8_t Stepper_GetGear(void) {
    return hstepper.current_gear;
}

/* 查询电机是否正在运行（1=正在转动，0=停止） */
uint8_t Stepper_IsRunning(void) {
    return hstepper.running;
}

/*
 * Stepper_Stop —— 紧急停止电机
 * 将目标位置设为当前位置，下次 TIM2 中断中 Stepper_TIM_Step()
 * 检测到相等后自动停止运行
 */
void Stepper_Stop(void) {
    hstepper.target_step = hstepper.current_step;
    hstepper.running = 0;
}

/*
 * Stepper_TIM_Step —— 步进电机单步执行
 * 由 TIM2 更新中断调用（100Hz，即每 10ms 一步）
 *
 * 执行逻辑：
 * 1. 如果 running=0，直接返回（保持当前相位以维持保持转矩）
 * 2. 根据方向 dir 前进或后退一步
 *    - dir=1（CW）：step_index +1（正向走 8 步序列）
 *    - dir=0（CCW）：step_index -1（反向走 8 步序列）
 * 3. 更新 GPIOA PA4-PA7 输出对应相位序列值
 *    ODR 低 4 位保留，高 4 位设置为步进表值
 * 4. 检查是否到达目标位置，如果是则停止并更新 current_gear
 *
 * 速度计算：100Hz × 8 步/圈 = 12.5 圈/秒（实际因 28BYJ-48 减速比 1:64 约为 0.2 圈/秒）
 * 350 步/档 ÷ 100 步/秒 = 3.5 秒/档
 */
void Stepper_TIM_Step(void) {
    if (!hstepper.running) return;

    /* 控制 28BYJ-48 的 8 步序列前进或后退 */
    if (hstepper.dir) {
        hstepper.current_step++;
        hstepper.step_index = (hstepper.step_index + 1) % 8;
    } else {
        hstepper.current_step--;
        hstepper.step_index = (hstepper.step_index + 7) % 8;  /* 等效于 -1 mod 8 */
    }

    /*
     * 更新 GPIOA 输出：保留低 4 位（PA0-PA3），高 4 位（PA4-PA7）写入步进相位
     * step_table[] 的值正好对应 PA4-PA7 的 D/C/B/A 顺序
     */
    GPIOA->ODR = (GPIOA->ODR & 0xFF0F) | step_table[hstepper.step_index];

    /* 到达目标位置，停止运行并更新当前档位 */
    if (hstepper.current_step == hstepper.target_step) {
        hstepper.running = 0;
        hstepper.current_gear = hstepper.target_gear;
    }
}

/*
 * Stepper_CalcAutoGear —— 自动模式档位计算
 * @param temp   当前温度（°C）
 * @param preset 设定温度（°C，用户通过 OLED 或蓝牙设置）
 * @return 档位 0-5
 *
 * 计算温差 dt = temp - preset，根据温差区间选择档位：
 *
 *   dt 范围       | 档位 | 行为
 *   dt ≤ -6.5     |  5   | 远低于目标 → 最大加热
 *   -6.5 < dt ≤ -3.5 |  4 | 低于目标 → 强加热
 *   -3.5 < dt ≤ -0.5 |  3 | 略低于目标 → 弱加热
 *   -0.5 < dt < +0.5 | ← | 死区：保持当前档位不变（防止频繁切换）
 *   +0.5 ≤ dt < +3.5 |  2 | 略高于目标 → 弱降温
 *   +3.5 ≤ dt < +6.5 |  1 | 高于目标 → 强降温
 *   dt ≥ +6.5     |  0   | 远高于目标 → 关闭（停止加热）
 *
 * 死区（±0.5°C）防止温度在设定点附近时频繁切换阀门
 */
uint8_t Stepper_CalcAutoGear(float temp, float preset) {
    float dt = temp - preset;

    /* 温度远低于目标 → 最大开度加热 */
    if (dt < -6.5f) return 5;
    if (dt < -3.5f) return 4;
    if (dt < -0.5f) return 3;

    /* 温度远高于目标 → 减小开度或关闭 */
    if (dt > 6.5f) return 0;
    if (dt > 3.5f) return 1;
    if (dt > 0.5f) return 2;

    /* 死区：不变 */
    return hstepper.current_gear;
}
