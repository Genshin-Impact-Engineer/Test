/**
 * @file    oled.h
 * @brief   OLED 显示模块头文件：SSD1306 128x64 I2C
 *          电子秤/POS 系统三页面菜单（称重/预览编辑/报警）+ 编辑模式
 *          字体：8x16 ASCII，4行×16列文本模式
 */

#ifndef __OLED_H__
#define __OLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "key.h"

/* ========== OLED 显示参数 ========== */
#define OLED_COLS         16       /* 每行字符数（128/8 = 16） */
#define OLED_ROWS         4        /* 行数（4 × 16 像素 = 64 像素） */
#define OLED_REFRESH_MS   500      /* 正常模式刷新间隔（ms） */
#define OLED_BLINK_MS     300      /* 编辑模式闪烁间隔（ms） */

/* ========== 产品数量 ========== */
#define PRODUCT_COUNT     7        /* 商品种类数量 */

/* ========== 重量阈值 ========== */
#define WEIGHT_MAX        2.0f     /* 超重阈值（kg） */
#define WEIGHT_MIN        0.005f   /* 最小分辨重量（kg） */

/*
 * ScalePage —— 电子秤页面枚举
 * PAGE_WEIGHING  = 称重页：商品类别、单价、重量、总价
 * PAGE_PREVIEW   = 预览编辑页：修改类别/单价，确认保存或放弃
 * PAGE_ALARM     = 报警页：超重或重量异常
 */
typedef enum { PAGE_WEIGHING = 0, PAGE_PREVIEW, PAGE_ALARM } ScalePage;

/*
 * Product_t —— 商品信息结构体
 */
typedef struct {
    char name[12];          /* 商品名称 */
    float default_price;    /* 默认单价（元/kg） */
} Product_t;

/*
 * ScaleData_t —— 称重数据结构体
 */
typedef struct {
    uint8_t category_idx;       /* 当前商品类别索引（0~PRODUCT_COUNT-1） */
    float unit_price;           /* 单价（元/kg） */
    float weight;               /* 重量（kg） */
    float total_price;          /* 总价（元）= unit_price * weight */
} ScaleData_t;

/*
 * OLED_t —— OLED 显示状态结构体
 *
 * current_page    = 当前显示页面
 * prev_page       = 报警前页面（报警恢复用）
 * selected_item   = 当前选中项（0=未选中, 1=类别, 2=单价）
 * edit_mode       = 编辑模式标志（1=编辑中, 0=浏览）
 * blink_state     = 闪烁状态（0/1 交替）
 * last_blink_tick = 上次闪烁切换的系统滴答
 *
 * scale           = 称重数据
 * alarm_overweight = 超重报警标志
 * alarm_weight_err = 重量异常标志
 */
typedef struct {
    uint8_t booted;
    ScalePage current_page;
    ScalePage prev_page;
    uint8_t selected_item;
    uint8_t edit_mode;
    uint8_t blink_state;
    uint32_t last_blink_tick;

    ScaleData_t scale;

    uint8_t alarm_overweight;
    uint8_t alarm_weight_err;
    uint32_t last_render;
    uint16_t flush_count;
} OLED_t;

/* 全局 OLED 实例 */
extern OLED_t holog;

/* 强制刷新标志：主循环置 1，OLED_UpdateDisplay 消费后清零 */
extern volatile uint8_t oled_force_render;

/* 全局商品表 */
extern Product_t product_table[PRODUCT_COUNT];

/* 模块接口函数 */
void OLED_Setup(void);
void Boot_Interface_Show(void);
void OLED_UpdateDisplay(uint32_t now);
void OLED_HandleKey(KeyId id, uint8_t is_long);
void OLED_SetAlarmFlags(uint8_t overweight, uint8_t weight_err);

/* 蓝牙命令回调 */
void OLED_SetCategoryByName(const char *name);
void OLED_SetUnitPrice(float price);

/* 重称触发（蓝牙或按键） */
void Scale_TriggerReweigh(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H__ */
