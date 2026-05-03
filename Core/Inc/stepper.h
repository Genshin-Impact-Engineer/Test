/**
 * @file    stepper.h
 * @brief   步进电机模块头文件：28BYJ-48 + ULN2003 驱动
 *          6 档阀门控制（0-5），支持自动温控调节
 */

#ifndef __STEPPER_H__
#define __STEPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ========== 步进电机机械参数 ========== */
#define STEPPER_GEAR_STEPS   350     /* 每档对应步数（28BYJ-48 半步，约 30° 阀门转角） */
#define STEPPER_MAX_GEAR     5       /* 最大档位（0=全关, 1-5=开度递增） */
#define STEPPER_GEAR_TOTAL(g)  ((uint16_t)((g) * STEPPER_GEAR_STEPS))  /* 档位→总步数换算 */

#define STEPPER_DEAD_ZONE    0.5f    /* 自动模式死区（°C）：±0.5°C 内保持档位不变 */

/*
 * Stepper_t —— 步进电机状态结构体
 * 所有 `volatile` 字段在主循环和 TIM2 中断间共享（无锁设计，简单原子操作）
 *
 * step_index     = 当前相位索引（0-7），指向 step_table[] 的当前位置
 * current_step   = 当前绝对步进位置（从 0 开始），范围为 0~1750（对应 0-5 档）
 * target_step    = 目标绝对步进位置
 * running        = 1=步进中, 0=停止（保持相位以持转矩）
 * dir            = 1=顺时针(CW, 开阀), 0=逆时针(CCW, 关阀)
 * current_gear   = 当前实际档位（0-5），motor 到达目标后更新
 * target_gear    = 目标档位（0-5）
 */
typedef struct {
    volatile uint8_t step_index;       /* 步进相位表索引 0-7 */
    volatile int32_t current_step;     /* 当前位置（绝对步数） */
    volatile int32_t target_step;      /* 目标位置（绝对步数） */
    volatile uint8_t running;          /* 运动状态标志 */
    volatile uint8_t dir;              /* 旋转方向 */
    uint8_t current_gear;              /* 当前档位 */
    uint8_t target_gear;               /* 目标档位 */
} Stepper_t;

/* 全局步进电机实例 */
extern Stepper_t hstepper;

/* 模块接口函数 */
void Stepper_Init(void);                           /* 初始化电机状态 */
void Stepper_SetGear(uint8_t gear);                /* 设置目标档位并启动运动 */
uint8_t Stepper_GetGear(void);                      /* 查询当前档位 */
uint8_t Stepper_IsRunning(void);                    /* 查询是否正在运行 */
void Stepper_Stop(void);                            /* 紧急停止 */
void Stepper_TIM_Step(void);                        /* TIM2 中断单步执行（100Hz） */
uint8_t Stepper_CalcAutoGear(float temp, float preset);  /* 自动模式档位计算 */

#ifdef __cplusplus
}
#endif

#endif /* __STEPPER_H__ */
