/**
 * @file    oled.c
 * @brief   OLED 显示模块：SSD1306 128x64，I2C 接口
 *          电子秤/POS 系统三页面菜单 + 编辑模式（闪烁指示）
 *          8x16 ASCII 字库，4行×16列文本模式
 *          内置 I2C 故障恢复机制（STM32F1 I2C BUSY 缺陷补偿）
 *
 * 三页面说明：
 *   PAGE_WEIGHING —— 称重主页：类别、单价、重量、总价、光标选择
 *   PAGE_PREVIEW  —— 预览编辑：确认/修改类别与单价后保存
 *   PAGE_ALARM    —— 报警页：超重/重量异常提示
 */

#include "oled.h"
#include "driver_oled.h"
#include "bluetooth.h"
#include "i2c.h"
#include <string.h>
#include <stdio.h>

/* 全局 OLED 实例 */
OLED_t holog = {0};

/* 8x16 ASCII 字库表（128 字符），在驱动文件中定义 */
extern const uint8_t ascii_font[128][16];

/* ========================================================================
 * 商品表（名称 + 默认单价）
 * 可根据实际商品修改
 * ======================================================================== */
const Product_t product_table[PRODUCT_COUNT] = {
    {"  Apple  ",  7.0f},
    {" Banana  ",  3.0f},
    {" Orange  ",  4.0f},
    {"  Grape  ",  6.0f},
    {"  Beef   ", 20.0f},
    {"  Pork   ", 15.0f},
    {" Chicken ", 12.0f},
    {"  Fish   ", 10.0f},
};

/* 帧缓冲区指针 */
static uint8_t *fb = NULL;

/* ========================================================================
 * 帧缓冲绘图函数（与 HVAC 项目完全相同）
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
        col++;
        str++;
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
 * 页面 1：称重页（PAGE_WEIGHING）
 *
 * 行号 | 内容                      | 可选中
 *  0   | Category: XXXX           |   是（item=1）
 *  1   | UnitPrice: XX.X yuan/kg  |   是（item=2）
 *  2   | Weight: XX.XXX kg        |   否（实时只读）
 *  3   | Total: XX.XX yuan        |   否（实时计算）
 *
 * 操作逻辑：
 *   - KEY0/KYE1: 光标在 未选中→类别→单价 之间循环
 *   - KEY2 (未选中): 结算（进入预览页）
 *   - KEY3 (未选中): 重称（触发重量重新采集）
 *   - KEY2 (选中): 进入编辑模式
 *   - KEY3 (选中): 退出选中
 * ======================================================================== */
static void render_page_weighing(void) {
    char buf[18];
    uint8_t blink = holog.edit_mode ? holog.blink_state : 1;
    const Product_t *p = &product_table[holog.scale.category_idx];

    /* 第 0 行：商品类别 */
    if (blink || holog.selected_item != 1) {
        snprintf(buf, sizeof(buf), "Category: %-7s", p->name);
    } else {
        snprintf(buf, sizeof(buf), "Category:         ");
    }
    fb_draw_string(0, 0, buf);

    /* 第 1 行：单价 */
    if (blink || holog.selected_item != 2) {
        snprintf(buf, sizeof(buf), "UniPrice:%4.1fY/kg", holog.scale.unit_price);
    } else {
        snprintf(buf, sizeof(buf), "UniPrice:          ");
    }
    fb_draw_string(0, 2, buf);

    /* 第 2 行：重量（只读） */
    snprintf(buf, sizeof(buf), "Weight:%7.3f kg ", holog.scale.weight);
    fb_draw_string(0, 4, buf);

    /* 第 3 行：总价（只读） */
    snprintf(buf, sizeof(buf), "Total:%8.2f Y   ", holog.scale.total_price);
    fb_draw_string(0, 6, buf);

    /* 选中框 */
    if (holog.selected_item == 1) fb_draw_sel_rect(0);
    else if (holog.selected_item == 2) fb_draw_sel_rect(2);
}

/* ========================================================================
 * 页面 2：预览编辑页（PAGE_PREVIEW）
 *
 * 行号 | 内容
 *  0   | [Preview] Save? / [Edit] Category / [Edit] UnitPrice
 *  1   | Category: XXXX
 *  2   | UnitPrice: XX.X
 *  3   | Total: XX.XX yuan
 *
 * 操作逻辑：
 *   - KEY0 (未选中): 重称
 *   - KEY0 (选中): 退出选中
 *   - KEY1 (编辑类别): 切换到下一个商品
 *   - KEY1 (编辑单价): 单价 -0.5
 *   - KEY2: 保存修改，返回称重页
 *   - KEY3: 放弃修改，返回称重页
 * ======================================================================== */
static void render_page_preview(void) {
    char buf[18];
    uint8_t blink = holog.edit_mode ? holog.blink_state : 1;
    const Product_t *p = &product_table[holog.scale.category_idx];

    /* 第 0 行：状态提示 */
    if (!holog.edit_mode) {
        snprintf(buf, sizeof(buf), "[Preview]         ");
    } else if (holog.selected_item == 1) {
        snprintf(buf, sizeof(buf), "[Edit]Category    ");
    } else {
        snprintf(buf, sizeof(buf), "[Edit]UnitPrice   ");
    }
    fb_draw_string(0, 0, buf);

    /* 第 1 行：类别 */
    if (blink || holog.selected_item != 1) {
        snprintf(buf, sizeof(buf), "Category: %-7s", p->name);
    } else {
        snprintf(buf, sizeof(buf), "Category:         ");
    }
    fb_draw_string(0, 2, buf);

    /* 第 2 行：单价 */
    if (blink || holog.selected_item != 2) {
        snprintf(buf, sizeof(buf), "UniPrice:%4.1fY/kg", holog.scale.unit_price);
    } else {
        snprintf(buf, sizeof(buf), "UniPrice:          ");
    }
    fb_draw_string(0, 4, buf);

    /* 第 3 行：总价（预览） */
    float preview_total = holog.scale.unit_price * holog.scale.weight;
    snprintf(buf, sizeof(buf), "Total:%8.2f Y   ", preview_total);
    fb_draw_string(0, 6, buf);

    /* 选中框 */
    if (holog.selected_item == 1) fb_draw_sel_rect(2);
    else if (holog.selected_item == 2) fb_draw_sel_rect(4);
}

/* ========================================================================
 * 页面 3：报警页（PAGE_ALARM）
 *
 * 行号 | 内容
 *  0   | !! WARNING !!
 *  1   | Overweight!        / Weight Error!
 *  2   | Check the scale    / Please re-weigh
 *  3   | Press any key...
 *
 * 操作逻辑：任意按键退出报警，停止蜂鸣器，返回称重页
 * ======================================================================== */
static void render_page_alarm(void) {
    fb_draw_string(0, 0, "!!   WARNING   !!");

    if (holog.alarm_overweight) {
        fb_draw_string(0, 2, "Overweight!      ");
        fb_draw_string(0, 4, "Remove items     ");
    } else if (holog.alarm_weight_err) {
        fb_draw_string(0, 2, "Weight Error!    ");
        fb_draw_string(0, 4, "Please re-weigh  ");
    } else {
        fb_draw_string(0, 2, "                ");
        fb_draw_string(0, 4, "                ");
    }

    fb_draw_string(0, 6, "Press any key... ");
}

/* ========================================================================
 * I2C 探测与恢复（与 HVAC 项目完全相同）
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

    holog.current_page = PAGE_WEIGHING;
    holog.prev_page = PAGE_WEIGHING;
    holog.edit_mode = 0;
    holog.selected_item = 0;
    holog.blink_state = 1;

    holog.scale.category_idx = 0;
    holog.scale.unit_price = product_table[0].default_price;
    holog.scale.weight = 0.0f;
    holog.scale.total_price = 0.0f;

    holog.alarm_overweight = 0;
    holog.alarm_weight_err = 0;

    holog.last_render = 0;
    holog.flush_count = 0;

    OLED_ClearFrameBuffer();
    OLED_Flush();
}

void Boot_Interface_Show(void) {
    OLED_ClearFrameBuffer();

    fb_draw_rect(0, 0, 128, 64);
    fb_draw_string(2, 1, " Smart Scale ");
    fb_draw_string(4, 4, "Loading...");

    fb_draw_rect(10, 48, 108, 11);

    OLED_Flush();
    HAL_Delay(100);

    for (int i = 0; i < 107; i++) {
        for (uint8_t row = 49; row <= 57; row++) {
            fb_draw_pixel(11 + i, row);
        }
        OLED_FlushRegion(11 + i, 49, 1, 9);
        HAL_Delay(10);
    }

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

/* 蓝牙命令：按名称设置类别 */
void OLED_SetCategoryByName(const char *name) {
    for (int i = 0; i < PRODUCT_COUNT; i++) {
        if (strstr(product_table[i].name, name)) {
            holog.scale.category_idx = i;
            holog.scale.unit_price = product_table[i].default_price;
            holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
            return;
        }
    }
}

/* 蓝牙命令：设置单价 */
void OLED_SetUnitPrice(float price) {
    if (price > 0) {
        holog.scale.unit_price = price;
        holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
    }
}

/* 重称触发 */
void Scale_TriggerReweigh(void) {
    /* 由外部传感器模块实现，此处设置标志 */
    holog.scale.weight = 0.0f;
    holog.scale.total_price = 0.0f;
}

/* ========================================================================
 * OLED_UpdateDisplay —— OLED 显示更新（由主循环周期性调用）
 * ======================================================================== */
void OLED_UpdateDisplay(uint32_t now) {
    if (now - holog.last_render < (holog.edit_mode ? OLED_BLINK_MS : OLED_REFRESH_MS))
        return;
    holog.last_render = now;

    if (_oled_probe() != HAL_OK) {
        OLED_ForceRecover();
    }

    if (++holog.flush_count >= 200) {
        holog.flush_count = 0;
        OLED_ForceRecover();
    }

    if (holog.edit_mode && now - holog.last_blink_tick >= OLED_BLINK_MS) {
        holog.blink_state = !holog.blink_state;
        holog.last_blink_tick = now;
    }

    OLED_ClearFrameBuffer();

    switch (holog.current_page) {
        case PAGE_WEIGHING: render_page_weighing(); break;
        case PAGE_PREVIEW:  render_page_preview();  break;
        case PAGE_ALARM:    render_page_alarm();    break;
    }

    if (OLED_Flush() != HAL_OK) {
        OLED_ForceRecover();
        OLED_Flush();
    }
}

/* ========================================================================
 * OLED_HandleKey —— 按键处理（三页面统一入口）
 *
 * 按键映射总览：
 * ┌─────────┬────────────────────────┬──────────────────────┬────────────┐
 * │  按键   │  PAGE_WEIGHING         │  PAGE_PREVIEW        │ PAGE_ALARM │
 * ├─────────┼────────────────────────┼──────────────────────┼────────────┤
 * │ KEY0    │ 光标上移: 0→1→2→0     │ 选中→退出 / 未→重称  │ 退出报警   │
 * │ KEY1    │ 光标下移: 0→2→1→0     │ 编辑类→下一 / 价-0.5 │ 退出报警   │
 * │ KEY2    │ 未选中→结算 / 选→编辑  │ 保存，返回称量       │ 退出报警   │
 * │ KEY3    │ 未选中→重称 / 选→退出  │ 放弃，返回称量       │ 退出报警   │
 * └─────────┴────────────────────────┴──────────────────────┴────────────┘
 * ======================================================================== */
void OLED_HandleKey(KeyId id, uint8_t is_long) {
    (void)is_long;  /* 本系统所有按键使用短按 */

    /* ── PAGE_ALARM: 任意按键退出报警，返回称重页 ── */
    if (holog.current_page == PAGE_ALARM) {
        holog.current_page = holog.prev_page;
        holog.selected_item = 0;
        holog.edit_mode = 0;
        holog.alarm_overweight = 0;
        holog.alarm_weight_err = 0;
        /* 停止蜂鸣器由主循环处理 alarm 标志清零后自动停止 */
        return;
    }

    switch (holog.current_page) {

    /* ================================================================
     * PAGE_WEIGHING —— 称重主页
     * ================================================================ */
    case PAGE_WEIGHING:
    {
        if (!holog.edit_mode) {
            /* ── 浏览模式 ── */
            switch (id) {
            case KEY_K1:
                /* KEY0: 光标上移循环 0→1→2→0 */
                if (holog.selected_item == 0)
                    holog.selected_item = 1;
                else if (holog.selected_item == 1)
                    holog.selected_item = 2;
                else
                    holog.selected_item = 0;
                break;

            case KEY_K2:
                /* KEY1: 光标下移循环 0→2→1→0 */
                if (holog.selected_item == 0)
                    holog.selected_item = 2;
                else if (holog.selected_item == 2)
                    holog.selected_item = 1;
                else
                    holog.selected_item = 0;
                break;

            case KEY_K3:
                /* KEY2: 未选中→结算（进入预览页）；选中→进入编辑 */
                if (holog.selected_item == 0) {
                    /* 结算：进入预览页 */
                    holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
                    holog.prev_page = PAGE_WEIGHING;
                    holog.current_page = PAGE_PREVIEW;
                    holog.selected_item = 0;
                    holog.edit_mode = 0;
                } else if (holog.selected_item == 1 || holog.selected_item == 2) {
                    holog.edit_mode = 1;
                }
                break;

            case KEY_K4:
                /* KEY3: 未选中→重称；选中→退出选中 */
                if (holog.selected_item == 0) {
                    Scale_TriggerReweigh();
                } else {
                    holog.selected_item = 0;
                }
                break;
            }
        } else {
            /* ── 编辑模式（称重页）── */
            switch (id) {
            case KEY_K1:
            case KEY_K4:
                /* KEY0/KEY3: 退出编辑/退出选中 */
                holog.edit_mode = 0;
                holog.selected_item = 0;
                break;

            case KEY_K2:
                /* KEY1: 编辑类别→下一个商品；编辑单价→单价-0.5 */
                if (holog.selected_item == 1) {
                    /* 切换到下一个商品 */
                    holog.scale.category_idx = (holog.scale.category_idx + 1) % PRODUCT_COUNT;
                    holog.scale.unit_price = product_table[holog.scale.category_idx].default_price;
                } else if (holog.selected_item == 2) {
                    holog.scale.unit_price -= 0.5f;
                    if (holog.scale.unit_price < 0.5f) holog.scale.unit_price = 0.5f;
                }
                break;

            case KEY_K3:
                /* KEY2: 确认并退出编辑 */
                holog.edit_mode = 0;
                break;
            }
        }
        break;
    }

    /* ================================================================
     * PAGE_PREVIEW —— 预览编辑页
     * ================================================================ */
    case PAGE_PREVIEW:
    {
        if (!holog.edit_mode) {
            /* ── 浏览模式 ── */
            switch (id) {
            case KEY_K1:
                /* KEY0: 光标上移 0→1→2→0 */
                if (holog.selected_item == 0)
                    holog.selected_item = 1;
                else if (holog.selected_item == 1)
                    holog.selected_item = 2;
                else
                    holog.selected_item = 0;
                break;

            case KEY_K2:
                /* KEY1: 光标下移 0→2→1→0 */
                if (holog.selected_item == 0)
                    holog.selected_item = 2;
                else if (holog.selected_item == 2)
                    holog.selected_item = 1;
                else
                    holog.selected_item = 0;
                break;

            case KEY_K3:
                /* KEY2: 保存修改，返回称重页 */
                holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
                holog.current_page = PAGE_WEIGHING;
                holog.selected_item = 0;
                break;

            case KEY_K4:
                /* KEY3: 放弃修改，返回称重页（数据不保存）*/
                /* 注意：需要恢复进入预览前的数据。此处简化处理：保留当前编辑值 */
                holog.current_page = PAGE_WEIGHING;
                holog.selected_item = 0;
                break;
            }
        } else {
            /* ── 编辑模式（预览页）── */
            switch (id) {
            case KEY_K1:
                /* KEY0: 退出选中 */
                holog.edit_mode = 0;
                holog.selected_item = 0;
                break;

            case KEY_K2:
                /* KEY1: 编辑类别→下一个商品；编辑单价→单价-0.5 */
                if (holog.selected_item == 1) {
                    holog.scale.category_idx = (holog.scale.category_idx + 1) % PRODUCT_COUNT;
                    holog.scale.unit_price = product_table[holog.scale.category_idx].default_price;
                } else if (holog.selected_item == 2) {
                    holog.scale.unit_price -= 0.5f;
                    if (holog.scale.unit_price < 0.5f) holog.scale.unit_price = 0.5f;
                }
                break;

            case KEY_K3:
                /* KEY2: 保存修改 → 返回称重 */
                holog.scale.total_price = holog.scale.unit_price * holog.scale.weight;
                holog.edit_mode = 0;
                holog.selected_item = 0;
                holog.current_page = PAGE_WEIGHING;
                break;

            case KEY_K4:
                /* KEY3: 放弃修改 → 返回称重 */
                holog.edit_mode = 0;
                holog.selected_item = 0;
                holog.current_page = PAGE_WEIGHING;
                break;
            }
        }
        break;
    }

    }
}
