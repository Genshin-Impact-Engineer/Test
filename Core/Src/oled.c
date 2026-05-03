/**
 * @file    oled.c
 * @brief   OLED 显示模块：SSD1306 128x64，I2C 接口
 *          8x16 ASCII 字库，4 行 × 16 列文本模式
 *          三页面菜单 + 编辑模式（闪烁指示）
 *          内置 I2C 故障恢复机制（STM32F1 I2C BUSY 缺陷补偿）
 */

#include "oled.h"
#include "driver_oled.h"
#include "sensor.h"
#include "alarm.h"
#include "i2c.h"
#include <string.h>
#include <stdio.h>

/* 全局 OLED 实例 */
OLED_t holog = {0};

/* 8x16 ASCII 字库表（128 字符），在驱动文件中定义 */
extern const uint8_t ascii_font[128][16];

/* 帧缓冲区指针（由 OLED_GetFrameBuffer 获取） */
static uint8_t *fb = NULL;

/*
 * Framebuffer: 128x64 = 1024 bytes, organized as 8 pages × 128 columns.
 * Each byte = 8 vertical pixels. fb[page * 128 + col] addresses one byte.
 *
 * 8x16 font occupies 2 pages per character row, so 4 text rows fit on screen:
 *   Row 0: pages 0-1    Row 1: pages 2-3
 *   Row 2: pages 4-5    Row 3: pages 6-7
 */

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

/* Draw selection rectangle around one text row (128x16) */
static void fb_draw_sel_rect(uint8_t page) {
    fb_draw_rect(0, page * 8, 128, 16);
}

/*
 * ──── 页面 1：数据显示页（PAGE_DATA）────
 * 显示传感器实时数据 + 可编辑参数
 *
 * 行号 | 页码 | 内容                  | 可编辑 | 说明
 *  0   | 0-1  | Turb: XX.X NTU       |  否   | 当前浊度值（只读）
 *  1   | 2-3  | Temp: XX.X C         |  否   | 当前温度值（只读）
 *  2   | 4-5  | SetT: XX.X C         |  是   | 预设温度 16-40°C（步进 0.5°C）
 *      |      |                      |       | K1/K2 进入/退出编辑，K3↓/K4↑
 *  3   | 6-7  | Setting: Auto/Manual |  是   | 自动/手动模式 + 手动档位
 *
 * 编辑模式下被选中项目会闪烁（blink_state 交替 0/1）
 * 当前选中项绘制矩形选择框（fb_draw_sel_rect）
 */
static void render_page_data(void) {
    char buf[18];
    /* 编辑模式且有选中项 → 使用闪烁控制；否则始终显示 */
    uint8_t blink = holog.edit_mode ? holog.blink_state : 1;

    /* 第 0 行：浊度值（NTU），保留 1 位小数 */
    snprintf(buf, sizeof(buf), "Turb:%5.1f NTU ", hsensor.turbidity);
    fb_draw_string(0, 0, buf);

    /* 第 1 行：温度（°C），保留 1 位小数 */
    snprintf(buf, sizeof(buf), "Temp:%4.1f C    ", hsensor.temperature);
    fb_draw_string(0, 2, buf);

    /* 第 2 行：预设温度 —— 非选中时或闪烁亮时显示数值，否则显示空白实现闪烁效果 */
    if (blink || holog.selected_item != 1) {
        snprintf(buf, sizeof(buf), "SetT:%5.1f C   ", holog.data.preset_temp);
        fb_draw_string(0, 4, buf);
    } else {
        fb_draw_string(0, 4, "SetT:           ");
    }

    /* 第 3 行：工作模式与档位 */
    if (holog.settings.valve_mode_auto) {
        snprintf(buf, sizeof(buf), "Setting:Auto %-2d ", holog.data.water_valve);
        fb_draw_string(0, 6, buf);
    } else if (blink || holog.selected_item != 2) {
        /* 手动模式：显示当前档位数字 */
        snprintf(buf, sizeof(buf), "Setting:%-2d      ", holog.data.water_valve);
        fb_draw_string(0, 6, buf);
    } else {
        fb_draw_string(0, 6, "Setting:        ");
    }

    /* 选中项选择框 */
    if (holog.selected_item == 1) fb_draw_sel_rect(4);     /* SetT 行 */
    else if (holog.selected_item == 2) fb_draw_sel_rect(6); /* Setting 行 */
}

/*
 * ──── 页面 2：设置页（PAGE_SETTINGS）────
 * 报警阈值参数 + 工作模式选择
 *
 * 行号 | 页码 | 内容                  | 可编辑 | 范围
 *  0   | 0-1  | MaxT: XX.X C         |  是   | 16.0-50.0°C（温度上限报警阈值）
 *  1   | 2-3  | MinT: XX.X C         |  是   | 0.0-32.0°C（温度下限报警阈值）
 *  2   | 4-5  | Turb: XX.X NTU       |  是   | 0.0-20.0 NTU（浊度报警阈值）
 *  3   | 6-7  | Mode: Auto/Manual    |  是   | 切换阀门控制模式
 *
 * 所有四项均在编辑模式下可调节，K3↓/K4↑ 调整值或切换模式
 * 选中项闪烁效果同数据页
 */
static void render_page_settings(void) {
    char buf[18];
    uint8_t blink = holog.edit_mode ? holog.blink_state : 1;

    /* 第 0 行：温度上限报警阈值 */
    if (blink || holog.selected_item != 1)
        snprintf(buf, sizeof(buf), "MaxT:%5.1f C   ", holog.settings.temp_max);
    else
        snprintf(buf, sizeof(buf), "MaxT:           ");
    fb_draw_string(0, 0, buf);

    /* 第 1 行：温度下限报警阈值 */
    if (blink || holog.selected_item != 2)
        snprintf(buf, sizeof(buf), "MinT:%5.1f C   ", holog.settings.temp_min);
    else
        snprintf(buf, sizeof(buf), "MinT:           ");
    fb_draw_string(0, 2, buf);

    /* 第 2 行：浊度报警阈值 */
    if (blink || holog.selected_item != 3)
        snprintf(buf, sizeof(buf), "Turb:%5.1f NTU ", holog.settings.turb_threshold);
    else
        snprintf(buf, sizeof(buf), "Turb:           ");
    fb_draw_string(0, 4, buf);

    /* 第 3 行：工作模式 Auto/Manual */
    if (blink || holog.selected_item != 4)
        snprintf(buf, sizeof(buf), "Mode:%-7s    ",
                 holog.settings.valve_mode_auto ? "Auto" : "Manual");
    else
        snprintf(buf, sizeof(buf), "Mode:           ");
    fb_draw_string(0, 6, buf);

    /* 选中项选择框绘制 */
    switch (holog.selected_item) {
        case 1: fb_draw_sel_rect(0); break;  /* MaxT 行 */
        case 2: fb_draw_sel_rect(2); break;  /* MinT 行 */
        case 3: fb_draw_sel_rect(4); break;  /* Turb 行 */
        case 4: fb_draw_sel_rect(6); break;  /* Mode 行 */
    }
}

/*
 * ──── 页面 3：报警/状态页（PAGE_ALARM）────
 * 显示温度和浊度的报警状态信息。
 *
 * 行号 | 内容                     | 触发时
 *  0   | Temp: OK!               | Temp: ERROR!
 *  1   | (空白)                  | Please check
 *  2   | Water: OK!              | Water: ERROR!
 *  3   | (空白)                  | Need handle
 *
 * 此页面有两种进入方式：
 *   1. 报警自动跳转：Alarm_Process() 检测到阈值触发后强制切换到该页
 *      按任意键返回 prev_page，报警状态持续到数据恢复 2s
 *   2. 手动导航：长按 K3/K4 翻页进入，查看当前报警状态
 */
static void render_page_alarm(void) {
    fb_draw_string(0, 0, holog.alarm_temp ? "Temp:ERROR!     " : "Temp:OK!        ");
    fb_draw_string(0, 2, holog.alarm_temp ? "Please check    " : "                ");
    fb_draw_string(0, 4, holog.alarm_turb ? "Water:ERROR!    " : "Water:OK!       ");
    fb_draw_string(0, 6, holog.alarm_turb ? "Need handle     " : "                ");
}

void OLED_Setup(void) {
    OLED_Init();  /* driver hardware init */

    uint32_t xres, yres, bpp;
    fb = OLED_GetFrameBuffer(&xres, &yres, &bpp);

    holog.current_page = PAGE_DATA;
    holog.prev_page = PAGE_DATA;
    holog.edit_mode = 0;
    holog.selected_item = 0;
    holog.blink_state = 1;

    holog.data.preset_temp = 25.0f;
    holog.data.water_valve = 1;

    holog.settings.temp_max = 32.0f;
    holog.settings.temp_min = 16.0f;
    holog.settings.turb_threshold = 8.0f;
    holog.settings.valve_mode_auto = 1;

    holog.last_render = 0;
    holog.flush_count = 0;

    OLED_ClearFrameBuffer();
    OLED_Flush();
}

void Boot_Interface_Show(void) {
    OLED_ClearFrameBuffer();

    /* Outer border */
    fb_draw_rect(0, 0, 128, 64);

    /* Title on text row 1 (pages 1-2) */
    fb_draw_string(2, 1, "Heating Valve");

    /* "Loading..." on text row 3 (pages 4-5) */
    fb_draw_string(4, 4, "Loading...");

    /* Progress bar border at (10,48)-(117,58) */
    fb_draw_rect(10, 48, 108, 11);

    OLED_Flush();
    HAL_Delay(100);

    /* Animate progress bar: 107 steps, 10ms each */
    for (int i = 0; i < 107; i++) {
        for (uint8_t row = 49; row <= 57; row++) {
            fb_draw_pixel(11 + i, row);
        }
        OLED_FlushRegion(11 + i, 49, 1, 9);
        HAL_Delay(10);
    }

    /* Clear "Loading..." area, show "Ready!" */
    for (uint8_t col = 0; col < 128; col++) {
        fb[4 * 128 + col] = 0;
        fb[5 * 128 + col] = 0;
    }
    fb_draw_string(5, 4, "Ready!");
    OLED_FlushRegion(0, 32, 128, 16);
    HAL_Delay(1000);

    /* Clear for main menu */
    OLED_ClearFrameBuffer();
    OLED_Flush();
}

void OLED_SetAlarmFlags(uint8_t temp_err, uint8_t turb_err) {
    holog.alarm_temp = temp_err;
    holog.alarm_turb = turb_err;
}

/*
 * _oled_probe —— 主动探测 I2C 总线上的 OLED 是否正常响应
 * @return HAL_OK=正常，其他=故障
 *
 * 发送 ENTIRE_DISPLAY_OFF 命令（0xA4）并检查返回状态。
 * 使用主动探测而非依赖 HAL 状态的原因是：
 * STM32F1 的 I2C 外设在遇到总线错误（BUSY、AF、BERR）后，
 * 其 HAL 状态可能仍显示 READY，但实际上后续传输都会被静默丢弃。
 *
 * 详见 STM32F1 I2C 硬件缺陷勘误表。
 * 超时 5ms（HAL_I2C_Master_Transmit 最后一个参数）
 */
static int _oled_probe(void) {
    /* Co=0, D/C#=0, ENTIRE_DISP_OFF — 无副作用的探测命令 */
    uint8_t tmp[2] = {0x00, 0xA4};
    return HAL_I2C_Master_Transmit(&hi2c1, 0x78, tmp, 2, 5);
}

/*
 * OLED_ForceRecover —— I2C1 外设强制恢复
 *
 * STM32F1 I2C 的 BUSY 标志位锁定问题：
 * - 当 I2C 传输被更高优先级中断打断时，I2C 状态机可能进入错误状态
 * - BUSY 标志位被硬件锁定，软件无法直接清除
 * - 即使重新初始化 I2C 外设寄存器，BUSY 位也可能仍然置位
 *
 * 唯一可靠的恢复方法：
 * 1. 强制复位 I2C1 外设（__HAL_RCC_I2C1_FORCE_RESET）
 * 2. 等待 1ms 确保复位生效
 * 3. 释放复位（__HAL_RCC_I2C1_RELEASE_RESET）
 * 4. 重新调用 CubeMX 生成的初始化函数 MX_I2C1_Init()
 * 5. 重新初始化 OLED 控制器（发送初始化序列）
 *
 * 此操作会短暂打断 I2C 总线上的通信（约 3-5ms），
 * 但由于 OLED 是总线上唯一的设备，不影响其他外设。
 */
static void OLED_ForceRecover(void) {
    __HAL_RCC_I2C1_FORCE_RESET();     /* 强制 I2C1 复位 */
    HAL_Delay(1);                      /* 等待复位完成 */
    __HAL_RCC_I2C1_RELEASE_RESET();   /* 释放复位 */
    MX_I2C1_Init();                    /* 重新初始化 I2C1 外设 */
    OLED_Init();                       /* 重新初始化 OLED 控制器 */
}

/*
 * OLED_UpdateDisplay —— OLED 显示更新（由主循环周期性调用）
 * @param now 当前系统滴答（ms）
 *
 * 工作流程：
 * 1. 刷新率控制：根据是否处于编辑模式决定刷新间隔（500ms / 100ms）
 * 2. I2C 总线探测：每次刷新前检查 OLED 是否正常响应
 * 3. 故障恢复：如果探测失败，强制恢复 I2C 总线
 * 4. 定期保养：每 200 次刷新（约 100 秒）执行一次强制恢复作为预防
 * 5. 闪烁控制：编辑模式下每 OLED_BLINK_MS（300ms）切换闪烁状态
 * 6. 帧缓冲渲染：清除 → 根据当前页面调用对应的渲染函数
 * 7. OLED 刷新：将帧缓冲数据通过 I2C 发送到 SSD1306
 */
void OLED_UpdateDisplay(uint32_t now) {
    /* 刷新率控制：编辑模式 100ms（闪烁需要），正常模式 500ms */
    if (now - holog.last_render < (holog.edit_mode ? OLED_BLINK_MS : OLED_REFRESH_MS))
        return;
    holog.last_render = now;

    /* ── I2C 总线探测与故障恢复 ── */
    if (_oled_probe() != HAL_OK) {
        OLED_ForceRecover();
        /* 恢复后，帧缓冲中的数据仍然有效（RAM 未丢失），
         * 下次 OLED_Flush() 会将当前内容正确发送 */
    }

    /* ── 定期强制恢复（容错增强） ── */
    if (++holog.flush_count >= 200) {   /* 约 200×500ms = 100 秒 */
        holog.flush_count = 0;
        OLED_ForceRecover();            /* 预防性恢复 */
    }

    /* ── 编辑模式闪烁控制 ── */
    if (holog.edit_mode && now - holog.last_blink_tick >= OLED_BLINK_MS) {
        holog.blink_state = !holog.blink_state;    /* 0/1 交替 */
        holog.last_blink_tick = now;
    }

    /* ── 帧缓冲渲染 ── */
    OLED_ClearFrameBuffer();       /* 清空帧缓冲 */

    /* 根据当前页面调用对应的页面渲染函数 */
    switch (holog.current_page) {
        case PAGE_DATA:    render_page_data();    break;
        case PAGE_SETTINGS: render_page_settings(); break;
        case PAGE_ALARM:   render_page_alarm();   break;
    }

    /* ── 将帧缓冲数据发送到 OLED ── */
    /* 检查 Flush 返回值：若 I2C 在传输过程中锁定（STM32F1 硬件缺陷），
     * 立即恢复而非等待 500ms 超时，避免主循环长时间阻塞 */
    if (OLED_Flush() != HAL_OK) {
        OLED_ForceRecover();
        /* 恢复后重试一次，帧缓冲数据仍在 SRAM 中有效 */
        OLED_Flush();
    }
}

/*
 * ──── OLED 按键映射表 ────
 *
 * 按键功能总览：
 * ┌─────────┬───────────────────────┬────────────────────────┐
 * │ 按键    │ 短按（浏览模式/编辑） │ 长按                    │
 * ├─────────┼───────────────────────┼────────────────────────┤
 * │ K1      │ 取消/退出编辑        │ （保留，当前未使用）     │
 * │ K2      │ 确认/进入编辑        │ 强制蓝牙上传            │
 * │ K3      │ 下移/减小数值        │ 下一页：DATA→SETTINGS→  │
 * │ K4      │ 上移/增大数值        │ 上一页：DATA→ALARM←     │
 * └─────────┴───────────────────────┴────────────────────────┘
 *
 * 页面导航（长按）：
 *   K3 长按：DATA → SETTINGS → ALARM → DATA → ...
 *   K4 长按：DATA → ALARM → SETTINGS → DATA → ...
 *
 * 报警页面行为（视进入方式不同）：
 *   报警自动跳入 → 任意短按确认报警，返回 prev_page
 *   手动导航进入 → 像普通页面一样停留，用长按离开
 *
 * 注意：main.c 中 K2 长按强制蓝牙上传的逻辑独立于此处，
 * 在事件分发循环中判断（main.c ~line 135）
 */
void OLED_HandleKey(KeyId id, uint8_t is_long) {
    /* Long press: page navigation (cycles through DATA/SETTINGS/ALARM) */
    if (is_long) {
        if (id == KEY_K3) {
            /* next page: DATA→SETTINGS→ALARM→DATA... */
            holog.current_page = (holog.current_page == PAGE_ALARM) ? PAGE_DATA :
                                 (holog.current_page + 1);
            holog.edit_mode = 0;
            holog.selected_item = 0;
        } else if (id == KEY_K4) {
            /* prev page: DATA→ALARM→SETTINGS→DATA... */
            holog.current_page = (holog.current_page == PAGE_DATA) ? PAGE_ALARM :
                                 (holog.current_page - 1);
            holog.edit_mode = 0;
            holog.selected_item = 0;
        }
        return;
    }

    switch (holog.current_page) {

    /* ── 数据页 ── */
    case PAGE_DATA:
    {
        if (!holog.edit_mode) {
            /* ── 浏览模式：移动选中项，K2 进入编辑 ── */
            if (id == KEY_K2) {
                if (holog.selected_item == 1 || holog.selected_item == 2)
                    holog.edit_mode = 1;
            } else if (id == KEY_K3) {
                /* K3 = down: 0→1→2→0 cycling */
                if (holog.settings.valve_mode_auto) {
                    holog.selected_item = (holog.selected_item == 1) ? 0 : 1;
                } else {
                    holog.selected_item = (holog.selected_item == 2) ? 0 :
                                          (holog.selected_item == 1) ? 2 : 1;
                }
            } else if (id == KEY_K4) {
                /* K4 = up: 0→2→1→0 cycling */
                if (holog.settings.valve_mode_auto) {
                    holog.selected_item = (holog.selected_item == 1) ? 0 : 1;
                } else {
                    holog.selected_item = (holog.selected_item == 0) ? 2 :
                                          (holog.selected_item == 2) ? 1 : 0;
                }
            }
        } else {
            /* ── 编辑模式：K1/K2 退出，K3/K4 调整数值 ── */
            if (id == KEY_K1 || id == KEY_K2) {
                holog.edit_mode = 0;
            } else if (id == KEY_K3) {
                if (holog.selected_item == 1) {
                    holog.data.preset_temp -= 0.5f;
                    if (holog.data.preset_temp < SETT_MIN)
                        holog.data.preset_temp = SETT_MIN;
                } else if (holog.selected_item == 2 && !holog.settings.valve_mode_auto) {
                    if (holog.data.water_valve > 0) {
                        holog.data.water_valve--;
                        Stepper_SetGear(holog.data.water_valve);
                    }
                }
            } else if (id == KEY_K4) {
                if (holog.selected_item == 1) {
                    holog.data.preset_temp += 0.5f;
                    if (holog.data.preset_temp > SETT_MAX)
                        holog.data.preset_temp = SETT_MAX;
                } else if (holog.selected_item == 2 && !holog.settings.valve_mode_auto) {
                    if (holog.data.water_valve < STEPPER_MAX_GEAR) {
                        holog.data.water_valve++;
                        Stepper_SetGear(holog.data.water_valve);
                    }
                }
            }
        }
        break;
    }

    /* ── 设置页 ── */
    case PAGE_SETTINGS:
    {
        if (!holog.edit_mode) {
            /* ── 浏览模式 ── */
            if (id == KEY_K2) {
                if (holog.selected_item >= 1 && holog.selected_item <= 4)
                    holog.edit_mode = 1;
            } else if (id == KEY_K3) {
                /* K3 = down: cycling 0→1→2→3→4→1 */
                if (holog.selected_item == 0)
                    holog.selected_item = 1;
                else if (holog.selected_item < 4)
                    holog.selected_item++;
                else
                    holog.selected_item = 1;
            } else if (id == KEY_K4) {
                /* K4 = up: cycling 0→4→3→2→1→4 */
                if (holog.selected_item <= 1)
                    holog.selected_item = 4;
                else
                    holog.selected_item--;
            }
        } else {
            /* ── 编辑模式 ── */
            if (id == KEY_K1 || id == KEY_K2) {
                holog.edit_mode = 0;
            } else if (id == KEY_K3) {
                switch (holog.selected_item) {
                case 1: /* MaxT */
                    holog.settings.temp_max -= 0.5f;
                    if (holog.settings.temp_max < 16.0f)
                        holog.settings.temp_max = 16.0f;
                    break;
                case 2: /* MinT */
                    holog.settings.temp_min -= 0.5f;
                    if (holog.settings.temp_min < 0.0f)
                        holog.settings.temp_min = 0.0f;
                    break;
                case 3: /* Turb threshold */
                    holog.settings.turb_threshold -= 0.5f;
                    if (holog.settings.turb_threshold < 0.0f)
                        holog.settings.turb_threshold = 0.0f;
                    break;
                case 4: /* Mode toggle */
                    holog.settings.valve_mode_auto = !holog.settings.valve_mode_auto;
                    if (holog.settings.valve_mode_auto) {
                        uint8_t gear = Stepper_CalcAutoGear(hsensor.temperature,
                                                           holog.data.preset_temp);
                        holog.data.water_valve = gear;
                        Stepper_SetGear(gear);
                    }
                    break;
                }
            } else if (id == KEY_K4) {
                switch (holog.selected_item) {
                case 1: /* MaxT */
                    holog.settings.temp_max += 0.5f;
                    if (holog.settings.temp_max > 50.0f)
                        holog.settings.temp_max = 50.0f;
                    break;
                case 2: /* MinT */
                    holog.settings.temp_min += 0.5f;
                    if (holog.settings.temp_min > 32.0f)
                        holog.settings.temp_min = 32.0f;
                    break;
                case 3: /* Turb threshold */
                    holog.settings.turb_threshold += 0.5f;
                    if (holog.settings.turb_threshold > 20.0f)
                        holog.settings.turb_threshold = 20.0f;
                    break;
                case 4: /* Mode toggle */
                    holog.settings.valve_mode_auto = !holog.settings.valve_mode_auto;
                    if (holog.settings.valve_mode_auto) {
                        uint8_t gear = Stepper_CalcAutoGear(hsensor.temperature,
                                                           holog.data.preset_temp);
                        holog.data.water_valve = gear;
                        Stepper_SetGear(gear);
                    }
                    break;
                }
            }
        }
        break;
    }

    /* ── 报警页：按任意键返回上一页，报警状态持续到数据恢复 2s ── */
    case PAGE_ALARM:
        holog.current_page = holog.prev_page;
        holog.selected_item = 0;
        holog.edit_mode = 0;
        break;

    }
}
