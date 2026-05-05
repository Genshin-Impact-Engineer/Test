/**
 * @file    oled.c
 * @brief   OLED 三页面系统：称重/预览编辑/报警
 *          中文显示 + 帧缓冲渲染 + I2C 故障恢复
 *          按键映射严格对齐 oled_module_guide.md
 */
#include "oled.h"
#include "driver_oled.h"
#include "bluetooth.h"
#include "i2c.h"
#include "voice.h"
#include "sensor.h"
#include <string.h>
#include <stdio.h>

OLED_t holog = {0};
volatile uint8_t oled_force_render = 0;

extern const uint8_t ascii_font[128][16];
extern const uint8_t g_chinese_fonts[40][32];

Product_t product_table[PRODUCT_COUNT] = {
    {"Apple",    8.0f},   /* 苹果  */
    {"Banana",   7.0f},   /* 香蕉  */
    {"Pineap",   8.0f},   /* 菠萝  */
    {"Grape",    6.0f},   /* 葡萄  */
    {"WMelon",   4.0f},   /* 西瓜  */
    {"Pear",    12.0f},   /* 梨子  */
    {"Melon",   12.0f},   /* 哈密瓜 */
};

/* 商品中文名在 g_chinese_fonts 中的索引（最多 3 字） */
static const uint8_t product_cn[PRODUCT_COUNT][3] = {
    { 8,  9,  0},  /* 苹果   */
    {10, 11,  0},  /* 香蕉   */
    {12, 13,  0},  /* 菠萝   */
    {14, 15,  0},  /* 葡萄   */
    {16, 17,  0},  /* 西瓜   */
    {18, 19,  0},  /* 梨子   */
    {20, 21, 17},  /* 哈密瓜 */
};
static const uint8_t product_cn_len[PRODUCT_COUNT] = {2, 2, 2, 2, 2, 2, 3};

static uint8_t *fb = NULL;
static float    saved_price = 0.0f;   /* 进入编辑前的原单价，用于取消时恢复 */

/* ========================================================================
 * 帧缓冲绘图
 * ======================================================================== */

static void fb_draw_char(uint8_t col, uint8_t page, char c) {
    uint8_t idx = (uint8_t)c;
    uint16_t base = page * 128 + col * 8;
    for (int i = 0; i < 8; i++) {
        fb[base + i] = ascii_font[idx][i];
        fb[base + 128 + i] = ascii_font[idx][i + 8];
    }
}

static void fb_draw_string(uint8_t col, uint8_t page, const char *str) {
    while (*str && col < OLED_COLS) {
        fb_draw_char(col, page, *str);
        col++; str++;
    }
}

/* 绘制单个 16x16 中文汉字（占 2 个 ASCII 列） */
static void fb_draw_chinese(uint8_t col, uint8_t page, uint8_t idx) {
    if (col > 14 || idx >= 40) return;
    uint16_t base = page * 128 + col * 8;
    for (int i = 0; i < 16; i++) {
        fb[base + i] = g_chinese_fonts[idx][i];
        fb[base + 128 + i] = g_chinese_fonts[idx][i + 16];
    }
}

static void fb_draw_pixel(uint8_t x, uint8_t y) {
    if (x >= 128 || y >= 64) return;
    fb[y / 8 * 128 + x] |= (uint8_t)(1 << (y % 8));
}

static void fb_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    for (uint8_t i = 0; i < w; i++) {
        fb_draw_pixel(x + i, y);
        fb_draw_pixel(x + i, y + h - 1);
    }
    for (uint8_t i = 1; i < h - 1; i++) {
        fb_draw_pixel(x, y + i);
        fb_draw_pixel(x + w - 1, y + i);
    }
}

static void fb_draw_sel_rect(uint8_t page) {
    fb_draw_rect(0, page * 8, 128, 16);
}

/* ========================================================================
 * 数值格式化助手（newlib-nano 禁 %f，手动拆整数+小数）
 * ======================================================================== */
static void fmt_val_2(char *buf, int sz, float val) {
    int s = (int)(val * 100.0f + 0.5f);
    snprintf(buf, sz, "%d.%02d", s / 100, s % 100);
}

/* ========================================================================
 * 页面 1：称重页（PAGE_WEIGHING）
 *
 * 布局（中文，16 列）：
 *   │商品:苹果        │ ← 选中 item=1，选中框在 pages 0-1
 *   │单价: 8.0元/kg   │ ← 选中 item=2，选中框在 pages 2-3
 *   │重量:  1.25kg    │ ← 只读
 *   │总价: 10.00元    │ ← 只读
 * ======================================================================== */
static void render_page_weighing(void) {
    char nbuf[8];
    uint8_t blink_off = holog.edit_mode && !holog.blink_state;
    uint8_t idx = holog.scale.category_idx;

    /* ── 标签双字，始终可见 ── */
    fb_draw_chinese(0, 0, 0);   /* 商 */
    fb_draw_chinese(2, 0, 1);   /* 品 */
    fb_draw_char  (4, 0, ':');

    fb_draw_chinese(0, 2, 2);   /* 单 */
    fb_draw_chinese(2, 2, 6);   /* 价 */
    fb_draw_char  (4, 2, ':');

    /* ── 第 0 行：商品名（选中时闪烁）── */
    if (!(holog.selected_item == 1 && blink_off)) {
        fb_draw_chinese(5, 0, product_cn[idx][0]);
        fb_draw_chinese(7, 0, product_cn[idx][1]);
        if (product_cn_len[idx] > 2)
            fb_draw_chinese(9, 0, product_cn[idx][2]);
    }

    /* ── 第 1 行：单价（2位小数，选中时闪烁）── */
    if (!(holog.selected_item == 2 && blink_off)) {
        fmt_val_2(nbuf, sizeof(nbuf), holog.scale.unit_price);
        fb_draw_string(5, 2, nbuf);
        fb_draw_chinese(10,2, 7);   /* 元 */
        fb_draw_char  (12,2, '/');
        fb_draw_char  (13,2, 'k');
        fb_draw_char  (14,2, 'g');
    }

    /* ── 第 2 行：重量（只读）+ 去皮（闪烁）── */
    fb_draw_chinese(0, 4, 3);   /* 重 */
    fb_draw_chinese(2, 4, 4);   /* 量 */
    fb_draw_char  (4, 4, ':');
    fmt_val_2(nbuf, sizeof(nbuf), holog.scale.weight);
    fb_draw_string(5, 4, nbuf);
    fb_draw_char  (9, 4, 'k');
    fb_draw_char  (10,4, 'g');
    if (holog.tare_active && holog.tare_blink) {
        fb_draw_char  (11,4, ' ');
        fmt_val_2(nbuf, sizeof(nbuf), holog.tare_weight);
        fb_draw_string(12,4, nbuf);
    }

    /* ── 第 3 行：总价（只读）── */
    fb_draw_chinese(0, 6, 5);   /* 总 */
    fb_draw_chinese(2, 6, 6);   /* 价 */
    fb_draw_char  (4, 6, ':');
    fmt_val_2(nbuf, sizeof(nbuf), holog.scale.total_price);
    fb_draw_string(5, 6, nbuf);
    fb_draw_chinese(10,6, 7);    /* 元 */

    if (holog.selected_item == 1) fb_draw_sel_rect(0);
    else if (holog.selected_item == 2) fb_draw_sel_rect(2);
}

/* ========================================================================
 * 页面 2：预览编辑页（PAGE_PREVIEW）
 * ======================================================================== */
static void render_page_preview(void) {
    char nbuf[8];
    uint8_t blink_off = holog.edit_mode && !holog.blink_state;
    uint8_t idx = holog.scale.category_idx;

    /* 第 0 行：状态提示 */
    if (!holog.edit_mode) {
        fb_draw_string(0, 0, "[Preview]       ");
    } else if (holog.selected_item == 1) {
        fb_draw_string(0, 0, "[Edit]商品      ");
    } else {
        fb_draw_string(0, 0, "[Edit]单价      ");
    }

    /* ── 标签双字，始终可见 ── */
    fb_draw_chinese(0, 2, 0);   /* 商 */
    fb_draw_chinese(2, 2, 1);   /* 品 */
    fb_draw_char  (4, 2, ':');

    fb_draw_chinese(0, 4, 2);   /* 单 */
    fb_draw_chinese(2, 4, 6);   /* 价 */
    fb_draw_char  (4, 4, ':');

    /* 第 1 行：商品名（选中时闪烁） */
    if (!(holog.selected_item == 1 && blink_off)) {
        fb_draw_chinese(5, 2, product_cn[idx][0]);
        fb_draw_chinese(7, 2, product_cn[idx][1]);
        if (product_cn_len[idx] > 2)
            fb_draw_chinese(9, 2, product_cn[idx][2]);
    }

    /* 第 2 行：单价（2位小数，选中时闪烁） */
    if (!(holog.selected_item == 2 && blink_off)) {
        fmt_val_2(nbuf, sizeof(nbuf), holog.scale.unit_price);
        fb_draw_string(5, 4, nbuf);
        fb_draw_chinese(10,4, 7);   /* 元 */
        fb_draw_char  (12,4, '/');
        fb_draw_char  (13,4, 'k');
        fb_draw_char  (14,4, 'g');
    }

    /* 第 3 行：总价预览 */
    {
        fb_draw_chinese(0, 6, 5);   /* 总 */
        fb_draw_chinese(2, 6, 6);   /* 价 */
        fb_draw_char  (4, 6, ':');
        float pt = holog.scale.unit_price * holog.scale.weight;
        fmt_val_2(nbuf, sizeof(nbuf), pt);
        fb_draw_string(5, 6, nbuf);
        fb_draw_chinese(10,6, 7);   /* 元 */
    }

    if (holog.selected_item == 1) fb_draw_sel_rect(2);
    else if (holog.selected_item == 2) fb_draw_sel_rect(4);
}

/* ========================================================================
 * 页面 3：报警页（PAGE_ALARM）—— 中文
 * ======================================================================== */
static void render_page_alarm(void) {
    /* 超重优先于重量抖动，居中显示 */
    if (holog.alarm_overweight) {
        fb_draw_chinese(3, 2, 22);  /* 超 */
        fb_draw_chinese(5, 2, 3);   /* 重 */
        fb_draw_char  (7, 2, '!');
        fb_draw_chinese(2, 4, 25);  /* 请 */
        fb_draw_chinese(4, 4, 31);  /* 减 */
        fb_draw_chinese(6, 4, 3);   /* 重 */
    } else if (holog.alarm_weight_err) {
        fb_draw_chinese(3, 2, 3);   /* 重 */
        fb_draw_chinese(5, 2, 4);   /* 量 */
        fb_draw_chinese(7, 2, 23);  /* 异 */
        fb_draw_chinese(9, 2, 24);  /* 常 */
        fb_draw_char  (11,2, '!');
        fb_draw_chinese(1, 4, 25);  /* 请 */
        fb_draw_chinese(3, 4, 3);   /* 重 */
        fb_draw_chinese(5, 4, 26);  /* 新 */
        fb_draw_chinese(7, 4, 30);  /* 称 */
        fb_draw_chinese(9, 4, 4);   /* 量 */
    }
}

/* ========================================================================
 * I2C 探测与恢复
 * ======================================================================== */
static int _oled_probe(void) {
    uint8_t tmp[2] = {0x00, 0xA4};
    return HAL_I2C_Master_Transmit(&hi2c1, 0x78, tmp, 2, 5);
}

static void OLED_ForceRecover(void) {
    __HAL_RCC_I2C1_FORCE_RESET();
    HAL_Delay(1);
    __HAL_RCC_I2C1_RELEASE_RESET();
    MX_I2C1_Init();
    OLED_Init();
}

/* ========================================================================
 * 公共接口
 * ======================================================================== */

void OLED_Setup(void) {
    OLED_Init();
    uint32_t xres, yres, bpp;
    fb = OLED_GetFrameBuffer(&xres, &yres, &bpp);

    holog.current_page  = PAGE_WEIGHING;
    holog.prev_page      = PAGE_WEIGHING;
    holog.edit_mode      = 0;
    holog.selected_item  = 0;
    holog.blink_state    = 1;
    holog.scale.category_idx = 0;
    holog.scale.unit_price   = product_table[0].default_price;
    holog.scale.weight       = 0.0f;
    holog.scale.total_price  = 0.0f;
    holog.alarm_overweight   = 0;
    holog.alarm_weight_err   = 0;
    holog.tare_weight        = 0.0f;
    holog.tare_active        = 0;
    holog.tare_blink         = 1;
    holog.last_tare_blink    = 0;
    holog.last_render        = 0;
    holog.flush_count        = 0;

    OLED_ClearFrameBuffer();
    OLED_Flush();
}

void Boot_Interface_Show(void) {
    /* ── 直接显示进度条加载动画 ── */
    OLED_ClearFrameBuffer();
    fb_draw_rect(0, 0, 128, 64);
    fb_draw_chinese(0, 2, 32);  /* 智 */
    fb_draw_chinese(2, 2, 33);  /* 能 */
    fb_draw_chinese(4, 2, 34);  /* 生 */
    fb_draw_chinese(6, 2, 35);  /* 鲜 */
    fb_draw_chinese(8, 2, 36);  /* 结 */
    fb_draw_chinese(10,2, 37);  /* 算 */
    fb_draw_chinese(12,2, 38);  /* 系 */
    fb_draw_chinese(14,2, 39);  /* 统 */
    fb_draw_rect(10, 48, 108, 11);
    OLED_Flush();

    for (int i = 0; i < 107; i++) {
        for (uint8_t row = 49; row <= 57; row++)
            fb_draw_pixel(11 + i, row);
        OLED_FlushRegion(11 + i, 49, 1, 9);
        HAL_Delay(10);
    }

    /* ── 传感器初始化 ── */
    extern void Sensor_Init(void);
    Sensor_Init();

    /* ── 就绪 ── */
    for (uint8_t col = 0; col < 128; col++) {
        fb[4 * 128 + col] = 0;
        fb[5 * 128 + col] = 0;
    }
    fb_draw_string(5, 4, "Ready!");
    OLED_FlushRegion(0, 32, 128, 16);
    HAL_Delay(1000);
    OLED_ClearFrameBuffer();
    OLED_Flush();
}

void OLED_SetAlarmFlags(uint8_t overweight, uint8_t weight_err) {
    holog.alarm_overweight = overweight;
    holog.alarm_weight_err = weight_err;
}

void OLED_SetCategoryByName(const char *name) {
    for (int i = 0; i < PRODUCT_COUNT; i++) {
        if (strstr(product_table[i].name, name)) {
            holog.scale.category_idx = i;
            holog.scale.unit_price   = product_table[i].default_price;
            holog.scale.total_price  = holog.scale.unit_price * holog.scale.weight;
            return;
        }
    }
}

void OLED_SetUnitPrice(float price) {
    if (price > 0) {
        holog.scale.unit_price  = price;
        holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
    }
}

void Scale_TriggerReweigh(void) {
    holog.scale.weight      = 0.0f;
    holog.scale.total_price = 0.0f;
    oled_force_render       = 1;
    Sensor_ResetFilters();   /* 复位滤波但不改零位，有重物也能正确重新测量 */
}

void OLED_UpdateDisplay(uint32_t now) {
    uint32_t rate = (holog.edit_mode || holog.tare_active) ? OLED_BLINK_MS : OLED_REFRESH_MS;
    if (!oled_force_render && now - holog.last_render < rate)
        return;
    oled_force_render = 0;
    holog.last_render = now;

    if (_oled_probe() != HAL_OK) OLED_ForceRecover();
    if (++holog.flush_count >= 200) { holog.flush_count = 0; OLED_ForceRecover(); }

    if (holog.edit_mode && now - holog.last_blink_tick >= OLED_BLINK_MS) {
        holog.blink_state = !holog.blink_state;
        holog.last_blink_tick = now;
    }
    if (holog.tare_active && now - holog.last_tare_blink >= OLED_BLINK_MS) {
        holog.tare_blink = !holog.tare_blink;
        holog.last_tare_blink = now;
    }

    OLED_ClearFrameBuffer();
    switch (holog.current_page) {
        case PAGE_WEIGHING: render_page_weighing(); break;
        case PAGE_PREVIEW:  render_page_preview();  break;
        case PAGE_ALARM:    render_page_alarm();    break;
    }

    if (OLED_Flush() != HAL_OK) { OLED_ForceRecover(); OLED_Flush(); }
}

/* ========================================================================
 * OLED_HandleKey —— 按键分发（严格对齐 oled_module_guide.md + key_module_guide.md）
 *
 * 映射总览（引脚参照 key_module_guide.md）：
 *   KEY_K4(PB15/丝印K4) = 光标上移 / 编辑:上一商品|单价+0.5
 *   KEY_K3(PB14/丝印K3) = 光标下移 / 编辑:下一商品|单价-0.5
 *   KEY_K2(PB13/丝印K2) = 结算|进入编辑 / 保存返回
 *   KEY_K1(PB12/丝印K1) = 重称|退出选中 / 放弃返回
 * ======================================================================== */
void OLED_HandleKey(KeyId id, uint8_t is_long) {
    (void)is_long;

    switch (holog.current_page) {

    /* ================================================================
     * PAGE_WEIGHING
     * ================================================================ */
    case PAGE_WEIGHING:
    {
        if (!holog.edit_mode) {
            /* ── 浏览模式 ── */
            switch (id) {
            case KEY_K4:
                /* 上: 0→2→1→0 (空→单价→商品名) */
                if (holog.selected_item == 0)      holog.selected_item = 2;
                else if (holog.selected_item == 2) holog.selected_item = 1;
                else                               holog.selected_item = 0;
                break;
            case KEY_K3:
                /* 下: 0→1→2→0 (空→商品名→单价) */
                if (holog.selected_item == 0)      holog.selected_item = 1;
                else if (holog.selected_item == 1) holog.selected_item = 2;
                else                               holog.selected_item = 0;
                break;
            case KEY_K2:
                /* 未选中→仅播报；选中→播报＋进入编辑 */
                if (holog.selected_item == 0) {
                    Voice_MeasureComplete();
                } else {
                    Voice_StartAdjust();
                    saved_price = product_table[holog.scale.category_idx].default_price;
                    holog.edit_mode = 1;
                }
                break;
            case KEY_K1:
                /* 未选中→去皮/取消去皮＋语音；选中→退出选中 */
                if (holog.selected_item == 0) {
                    if (holog.tare_active) {
                        Voice_TareOff();
                        holog.tare_active = 0;
                        holog.tare_weight = 0.0f;
                    } else {
                        Voice_Tare();
                        holog.tare_active = 1;
                        holog.tare_weight = holog.scale.weight;
                    }
                    oled_force_render = 1;
                } else {
                    holog.selected_item = 0;
                }
                break;
            }
        } else {
            /* ── 编辑模式 ── */
            switch (id) {
            case KEY_K2:
                /* 保存：回写单价到商品表 */
                product_table[holog.scale.category_idx].default_price = holog.scale.unit_price;
                holog.edit_mode = 0;
                Voice_Completed();
                break;
            case KEY_K1:
                /* 放弃：恢复原单价 */
                holog.scale.unit_price = saved_price;
                holog.edit_mode = 0;
                holog.selected_item = 0;
                Voice_Cancelled();
                break;
            case KEY_K4:
                /* 上一商品 / 单价+0.5 */
                if (holog.selected_item == 1) {
                    holog.scale.category_idx =
                        (holog.scale.category_idx + PRODUCT_COUNT - 1) % PRODUCT_COUNT;
                    holog.scale.unit_price = product_table[holog.scale.category_idx].default_price;
                    saved_price = holog.scale.unit_price;
                } else if (holog.selected_item == 2) {
                    holog.scale.unit_price += 0.5f;
                    if (holog.scale.unit_price > 200.0f) holog.scale.unit_price = 200.0f;
                }
                break;
            case KEY_K3:
                /* 下一商品 / 单价-0.5 */
                if (holog.selected_item == 1) {
                    holog.scale.category_idx = (holog.scale.category_idx + 1) % PRODUCT_COUNT;
                    holog.scale.unit_price = product_table[holog.scale.category_idx].default_price;
                    saved_price = holog.scale.unit_price;
                } else if (holog.selected_item == 2) {
                    holog.scale.unit_price -= 0.5f;
                    if (holog.scale.unit_price < 0.5f) holog.scale.unit_price = 0.5f;
                }
                break;
            }
        }
        break;
    }

    /* ================================================================
     * PAGE_PREVIEW
     * ================================================================ */
    case PAGE_PREVIEW:
    {
        if (!holog.edit_mode) {
            /* ── 浏览模式 ── */
            switch (id) {
            case KEY_K4:
                if (holog.selected_item == 0)      holog.selected_item = 2;
                else if (holog.selected_item == 2) holog.selected_item = 1;
                else                               holog.selected_item = 0;
                break;
            case KEY_K3:
                if (holog.selected_item == 0)      holog.selected_item = 1;
                else if (holog.selected_item == 1) holog.selected_item = 2;
                else                               holog.selected_item = 0;
                break;
            case KEY_K2:
                /* 保存返回 */
                holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
                holog.current_page = PAGE_WEIGHING;
                holog.selected_item = 0;
                holog.edit_mode = 0;
                break;
            case KEY_K1:
                /* 放弃返回 */
                holog.current_page = PAGE_WEIGHING;
                holog.selected_item = 0;
                holog.edit_mode = 0;
                break;
            }
        } else {
            /* ── 编辑模式 ── */
            switch (id) {
            case KEY_K2:
                /* 保存返回：回写单价到商品表 */
                product_table[holog.scale.category_idx].default_price = holog.scale.unit_price;
                holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
                holog.current_page = PAGE_WEIGHING;
                holog.selected_item = 0;
                holog.edit_mode = 0;
                break;
            case KEY_K1:
                /* 放弃返回：恢复原单价 */
                holog.scale.unit_price = saved_price;
                holog.current_page = PAGE_WEIGHING;
                holog.selected_item = 0;
                holog.edit_mode = 0;
                break;
            case KEY_K4:
                /* 上一商品 / 单价+0.5 */
                if (holog.selected_item == 1) {
                    holog.scale.category_idx =
                        (holog.scale.category_idx + PRODUCT_COUNT - 1) % PRODUCT_COUNT;
                    holog.scale.unit_price = product_table[holog.scale.category_idx].default_price;
                    saved_price = holog.scale.unit_price;
                } else if (holog.selected_item == 2) {
                    holog.scale.unit_price += 0.5f;
                    if (holog.scale.unit_price > 200.0f) holog.scale.unit_price = 200.0f;
                }
                break;
            case KEY_K3:
                /* 下一商品 / 单价-0.5 */
                if (holog.selected_item == 1) {
                    holog.scale.category_idx = (holog.scale.category_idx + 1) % PRODUCT_COUNT;
                    holog.scale.unit_price = product_table[holog.scale.category_idx].default_price;
                    saved_price = holog.scale.unit_price;
                } else if (holog.selected_item == 2) {
                    holog.scale.unit_price -= 0.5f;
                    if (holog.scale.unit_price < 0.5f) holog.scale.unit_price = 0.5f;
                }
                break;
            }
        }
        break;
    }

    default: break;
    }
}
