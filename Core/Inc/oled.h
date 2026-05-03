/**
 * @file    oled.h
 * @brief   OLED 显示模块头文件：SSD1306 128x64 I2C
 *          三页面菜单（数据/设置/报警）+ 编辑模式
 */

#ifndef __OLED_H__
#define __OLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "key.h"
#include "stepper.h"

/* ========== OLED 显示参数 ========== */
#define OLED_COLS         16       /* 每行字符数（8x16 字体，128/8 = 16） */
#define OLED_ROWS         4        /* 行数（4 行 × 16 像素 = 64 像素） */
#define OLED_REFRESH_MS   500      /* 正常模式刷新间隔（ms） */
#define OLED_BLINK_MS     300      /* 编辑模式闪烁间隔（ms） */

/* ========== 设置参数范围 ========== */
#define SETT_MAX          40.0f    /* 预设温度最大值（°C） */
#define SETT_MIN          16.0f    /* 预设温度最小值（°C） */

/*
 * PageId —— OLED 页面枚举
 * 三页循环导航：DATA → SETTINGS → ALARM → DATA ...
 * PAGE_DATA     = 数据页：显示传感器值 + 可编辑预设温度/档位
 * PAGE_SETTINGS = 设置页：设置报警阈值 + 工作模式
 * PAGE_ALARM    = 报警页：显示异常状态（由报警触发或手动导航进入）
 */
typedef enum { PAGE_DATA = 0, PAGE_SETTINGS, PAGE_ALARM } PageId;

/*
 * PageData_t —— 数据页的可编辑参数
 * preset_temp  = 预设温度（°C，范围 16.0~40.0，步进 0.5）
 * water_valve  = 阀门档位（0-5，仅手动模式可编辑）
 */
typedef struct {
    float preset_temp;              /* 预设温度（用户设定目标温度） */
    uint8_t water_valve;            /* 阀门档位 0-5 */
} PageData_t;

/*
 * PageSettings_t —— 设置页的可配置参数
 * temp_max           = 温度上限报警阈值（°C，范围 16.0~50.0）
 * temp_min           = 温度下限报警阈值（°C，范围 0.0~32.0）
 * turb_threshold     = 浊度报警阈值（NTU，范围 0.0~20.0）
 * valve_mode_auto    = 工作模式（1=自动调温, 0=手动控制）
 */
typedef struct {
    float temp_max;                  /* 温度上限（报警阈值） */
    float temp_min;                  /* 温度下限（报警阈值） */
    float turb_threshold;            /* 浊度阈值（报警阈值） */
    uint8_t valve_mode_auto;         /* 工作模式（1=自动, 0=手动） */
} PageSettings_t;

/*
 * OLED_t —— OLED 显示状态结构体
 *
 * booted          = 启动标志（预留，当前未使用）
 * current_page    = 当前显示页面（PAGE_DATA/SETTINGS/ALARM）
 * prev_page       = 上一个页面的备份（报警触发后用于恢复）
 * selected_item   = 当前选中项序号（0=无, 1=项1, 2=项2...）
 * edit_mode       = 编辑模式标志（1=编辑中, 0=浏览）
 * blink_state     = 闪烁状态（编辑模式下被编辑项目闪烁显示）
 * last_blink_tick = 上次闪烁切换的系统滴答
 *
 * data            = 数据页参数（详见 PageData_t）
 * settings        = 设置页参数（详见 PageSettings_t）
 *
 * alarm_temp      = 温度报警显示标志（OLED 显示需要）
 * alarm_turb      = 浊度报警显示标志
 */
typedef struct {
    uint8_t booted;                  /* 系统启动标志 */
    PageId current_page;             /* 当前页面 */
    PageId prev_page;                /* 上一页面（报警恢复用） */
    uint8_t selected_item;           /* 当前选中项（0=未选中） */
    uint8_t edit_mode;               /* 编辑模式标志 */
    uint8_t blink_state;             /* 闪烁状态（0/1 交替） */
    uint32_t last_blink_tick;        /* 上次闪烁切换时间（ms） */

    PageData_t data;                 /* 数据页参数 */
    PageSettings_t settings;         /* 设置页参数 */

    uint8_t alarm_temp;              /* 温度报警显示标志 */
    uint8_t alarm_turb;              /* 浊度报警显示标志 */
    uint32_t last_render;            /* 最后渲染时间戳（ms），OLED_UpdateDisplay 刷新率控制 */
    uint16_t flush_count;            /* OLED 刷新计数器，触发定期 I2C 强制恢复 */
} OLED_t;

/* 全局 OLED 实例 */
extern OLED_t holog;

/* 模块接口函数 */
void OLED_Setup(void);                              /* OLED 初始化 */
void Boot_Interface_Show(void);                     /* 显示启动画面 */
void OLED_UpdateDisplay(uint32_t now);              /* 更新显示（渲染+刷新） */
void OLED_HandleKey(KeyId id, uint8_t is_long);     /* 按键处理（菜单导航） */
void OLED_SetAlarmFlags(uint8_t temp_err, uint8_t turb_err);  /* 设置报警标志 */

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H__ */
