/**
 * @file    bluetooth.c
 * @brief   蓝牙通信模块：小机云蓝牙 X-B01 文本协议
 *          上行数据上报 / 下行命令解析 + BCC 校验
 *          DMA 循环接收 + IDLE 中断帧检测
 *
 * 协议格式：$X#D#key1:val1;key2:val2;&CheckCode\r\n
 * 回复格式：$XA#D#OK&CheckCode\r\n  /  $XA#D#ERR:message&CheckCode\r\n
 * CheckCode = BCC 异或校验（& 前所有字节异或，输出 2 位大写 hex）
 */

#include "bluetooth.h"
#include "oled.h"
#include "stepper.h"
#include "sensor.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* USART1 句柄（蓝牙模块），在 usart.c 中定义 */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

/* 全局蓝牙通信实例 */
Bluetooth_t hbt = {0};

/* 静态发送缓冲区 */
static char tx_buf[BT_TX_BUF_SIZE];

/* 回复缓冲区（独立于 tx_buf，避免与 SendData 的 DMA 传输冲突） */
static char resp_buf[64];

/* ========================================================================
 * BCC 异或校验
 * ======================================================================== */

/*
 * BCC_Calculate —— 计算 BCC 异或校验码
 * 对 data[0..len-1] 逐字节异或
 */
static uint8_t BCC_Calculate(const uint8_t *data, uint16_t len) {
    uint8_t c = 0;
    while (len--) c ^= *data++;
    return c;
}

/*
 * BCC_Format —— 将校验码格式化为 2 位大写十六进制 ASCII
 */
static void BCC_Format(uint8_t c, char out[2]) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[c >> 4];
    out[1] = hex[c & 0x0F];
}

/* ========================================================================
 * 回复发送
 * ======================================================================== */

/*
 * send_response —— 发送协议回复帧（DMA 模式）
 * @param content  回复内容，如 "OK" / "ERR:XOR" / "ERR:D"
 *
 * 关键修复：使用 HAL_UART_Transmit_DMA 替代 HAL_UART_Transmit（阻塞）。
 * 原因：同一 UART 外设上混用阻塞 TX 和 DMA TX 会导致 STM32F1 HAL 状态机
 * 不一致 —— 阻塞模式完成后 DMA 句柄可能处于非 READY 状态，下次
 * HAL_UART_Transmit_DMA 内部 HAL_DMA_Start_IT 静默失败，gState 永久卡在
 * BUSY_TX，所有后续蓝牙数据上报全部失效。
 *
 * 若当前有 TX 正在进行（gState != READY），跳过本次回复。
 * 小程序可通过下一轮数据上报确认命令已被接收。
 */
static void send_response(const char *content) {
    if (huart1.gState != HAL_UART_STATE_READY) return;

    int head_len = snprintf(resp_buf, sizeof(resp_buf), "$XA#D#%s", content);
    if (head_len <= 0 || head_len >= (int)sizeof(resp_buf) - 5) return;

    uint8_t bcc = BCC_Calculate((uint8_t *)resp_buf, head_len);

    char bcc_str[2];
    BCC_Format(bcc, bcc_str);
    head_len += snprintf(resp_buf + head_len, sizeof(resp_buf) - head_len,
                         "&%c%c\r\n", bcc_str[0], bcc_str[1]);

    /*
     * 同 Bluetooth_SendData：启动 DMA TX 前确保 TC 已置位，
     * 防止回复帧与数据帧格式不同导致硬件移位时序锁死。
     */
    if (!(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC))) {
        extern volatile uint32_t sys_tick_ms;
        uint32_t tc_wait = sys_tick_ms;
        while (!(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC))) {
            if (sys_tick_ms - tc_wait > 2) break;
        }
        if (!(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC))) {
            CLEAR_BIT(huart1.Instance->CR3, USART_CR3_DMAT);
            CLEAR_BIT(huart1.Instance->CR1, USART_CR1_TE);
            __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);
            SET_BIT(huart1.Instance->CR1, USART_CR1_TE);
        }
    }
    __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);

    HAL_UART_Transmit_DMA(&huart1, (uint8_t *)resp_buf, head_len);
}

/* ========================================================================
 * 字段解析辅助
 * ======================================================================== */

/*
 * FIELD_MATCH —— 检查提取的 key 是否匹配已知字段名
 * k     = 指向 key 字符串的指针
 * s     = 字符串字面量
 * key_len = key 的实际长度
 */
#define FIELD_MATCH(k, s) (key_len == sizeof(s)-1 && memcmp(k, s, key_len) == 0)

/*
 * parse_field —— 解析单个 key:value 并写入对应系统参数
 * @param key      key 字符串指针
 * @param key_len  key 长度
 * @param val      value 字符串指针（以 ; / & 结尾）
 *
 * 仅处理可写字段（app → STM32）。只读字段（text_*）不处理。
 */
static void parse_field(const char *key, uint16_t key_len, const char *val) {
    if (FIELD_MATCH(key, "number_preset_temp") ||
            FIELD_MATCH(key, "numb_preset_temp")) {
        float v = strtof(val, NULL);
        if (v >= 0.0f && v <= 50.0f) holog.data.preset_temp = v;
        return;
    }
    if (FIELD_MATCH(key, "number_water_level")) {
        int v = (int)strtol(val, NULL, 10);
        if (v >= 0 && v <= STEPPER_MAX_GEAR) {
            holog.data.water_valve = (uint8_t)v;
            if (!holog.settings.valve_mode_auto) {
                Stepper_SetGear((uint8_t)v);
            }
        }
        return;
    }
    if (FIELD_MATCH(key, "number_temp_max")) {
        float v = strtof(val, NULL);
        if (v > 0) holog.settings.temp_max = v;
        return;
    }
    if (FIELD_MATCH(key, "number_temp_min")) {
        float v = strtof(val, NULL);
        if (v > 0) holog.settings.temp_min = v;
        return;
    }
    if (FIELD_MATCH(key, "number_turb_threshold")) {
        float v = strtof(val, NULL);
        if (v >= 0) holog.settings.turb_threshold = v;
        return;
    }
    if (FIELD_MATCH(key, "selected_valve_mode")) {
        int mode = (int)strtol(val, NULL, 10);
        if (mode == 0 || mode == 1) {
            holog.settings.valve_mode_auto = (mode == 0) ? 1 : 0;
        }
        return;
    }
}

/* ========================================================================
 * 命令解析
 * ======================================================================== */

/*
 * Bluetooth_ParseCommand —— 解析文本协议命令
 * @param cmd  完整的文本协议帧，以 \0 结尾
 *
 * 处理流程：
 *   1. 校验 $X#D# 前缀
 *   2. 定位 & 提取校验码，计算 BCC 并比对
 *   3. 校验失败 → 回复 ERR:XOR
 *   4. 校验通过 → 按 ; 分割 key:value 对，逐字段解析写入 holog
 *   5. 回复 OK 确认
 *   6. 触发立即上传（使 APP 即时看到更新后的参数）
 */
void Bluetooth_ParseCommand(const char *cmd) {
    if (!cmd) return;

    /* 验证前缀 */
    if (strncmp(cmd, "$X#D#", 5) != 0) {
        send_response("ERR:D");
        return;
    }

    /* 定位校验码分隔符 & */
    const char *amp = strchr(cmd, '&');
    if (!amp) {
        send_response("ERR:D");
        return;
    }

    /* BCC 校验 */
    uint8_t calc_bcc = BCC_Calculate((const uint8_t *)cmd, amp - cmd);
    char hex_str[3] = {amp[1], amp[2], '\0'};
    uint8_t expected_bcc = (uint8_t)strtol(hex_str, NULL, 16);

    if (calc_bcc != expected_bcc) {
        send_response("ERR:XOR");
        return;
    }

    /* 解析内容：key1:val1;key2:val2;...（在 $X#D# 之后、& 之前） */
    const char *content = cmd + 5;
    const char *end = amp;

    while (content < end) {
        /* 查找下一个分号或到达末尾 */
        const char *semi = memchr(content, ';', end - content);
        if (!semi) semi = end;

        /* 在当前段中查找冒号 */
        const char *colon = memchr(content, ':', semi - content);
        if (colon) {
            uint16_t key_len = (uint16_t)(colon - content);
            parse_field(content, key_len, colon + 1);
        }

        content = semi + 1;
    }

    /* 回复成功 + 触发立即上传确认 */
    send_response("OK");
    Bluetooth_RequestUpload();
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

/*
 * Bluetooth_Init —— 初始化蓝牙模块
 * 1. 清零所有状态
 * 2. 启动 USART1 DMA 循环接收
 * 3. 使能 IDLE 空闲中断（每条消息完成后由 IDLE 中断通知 CPU）
 */
void Bluetooth_Init(void) {
    memset(&hbt, 0, sizeof(hbt));
    HAL_UART_Receive_DMA(&huart1, hbt.rx_buf, BT_RX_BUF_SIZE);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
}

/* ========================================================================
 * 数据上传
 * ======================================================================== */

/*
 * Bluetooth_SendData —— 向手机 APP 上报传感器 / 状态 / 设置数据
 *
 * 文本协议格式：
 *   $X#D#text_turb:%.1f;text_temp:%.1f;number_preset_temp:%.1f;
 *        number_water_level:%d;number_temp_max:%.1f;number_temp_min:%.1f;
 *        number_turb_threshold:%.1f;selected_valve_mode:%d;
 *        text_temp_state:%s;text_turb_state:%s&XX\r\n
 *
 * 字段与组件表对齐（共 10 个字段）
 */
void Bluetooth_SendData(void) {
    hbt.temperature  = hsensor.temperature;
    hbt.turbidity    = hsensor.turbidity;
    hbt.preset_temp  = holog.data.preset_temp;
    hbt.water_level  = hstepper.current_gear;
    hbt.temp_max     = holog.settings.temp_max;
    hbt.temp_min     = holog.settings.temp_min;
    hbt.turb_threshold = holog.settings.turb_threshold;
    hbt.valve_mode   = holog.settings.valve_mode_auto ? 0 : 1;

    int len = snprintf(tx_buf, sizeof(tx_buf),
        "$X#D#"
        "text_turb:%.1f;"
        "text_temp:%.1f;"
        "number_preset_temp:%.1f;"
        "number_water_level:%d;"
        "number_temp_max:%.1f;"
        "number_temp_min:%.1f;"
        "number_turb_threshold:%.1f;"
        "selected_valve_mode:%d;"
        "text_temp_state:%s;"
        "text_turb_state:%s",
        hbt.turbidity, hbt.temperature, hbt.preset_temp,
        hbt.water_level, hbt.temp_max, hbt.temp_min,
        hbt.turb_threshold, hbt.valve_mode,
        hbt.temp_state, hbt.turb_state);

    if (len <= 0 || len >= (int)sizeof(tx_buf) - 5) return;

    uint8_t bcc = BCC_Calculate((uint8_t *)tx_buf, len);
    char bcc_str[2];
    BCC_Format(bcc, bcc_str);
    len += snprintf(tx_buf + len, sizeof(tx_buf) - len,
                    "&%c%c\r\n", bcc_str[0], bcc_str[1]);

    if (len > 0 && len < BT_TX_BUF_SIZE) {
        extern volatile uint32_t sys_tick_ms;

        /*
         * 关键修复：确保上一帧已从移位寄存器完全移出（TC=1）再启动新 DMA TX。
         *
         * TXE 只看 DR 空不空，不管整帧是否发完。TC 才表示最后一个字节的
         * 停止位已移出 TX 引脚。同长度帧循环发送时，新帧首字节写 DR 会
         * 覆盖移位时序→自愈。但单告警帧切双告警帧（长度/校验/结尾全部不同）
         * 时，旧帧移位相位与新帧不匹配→硬件时序锁死：TXE 正常、DR 正常写入、
         * 但字节被内部吞掉不往外发。
         *
         * 修复：等 TC 置位后再清 TC、启 DMA。若 TC 超时未置位（硬件已锁死），
         * 强制复位 UART TX 状态机（toggle TE）。
         */
        uint32_t tc_wait = sys_tick_ms;
        while (!(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC))) {
            if (sys_tick_ms - tc_wait > 2) break;
        }
        if (!(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TC))) {
            /* TC 超时 2ms → 硬件移位寄存器卡死，强制复位 TX */
            CLEAR_BIT(huart1.Instance->CR3, USART_CR3_DMAT);
            CLEAR_BIT(huart1.Instance->CR1, USART_CR1_TE);
            __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);
            SET_BIT(huart1.Instance->CR1, USART_CR1_TE);
        }
        __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);

        hbt.last_tx_start = sys_tick_ms;
        HAL_UART_Transmit_DMA(&huart1, (uint8_t *)tx_buf, len);
    }
}

/* ========================================================================
 * 命令处理
 * ======================================================================== */

/*
 * Bluetooth_ProcessCommand —— 处理蓝牙接收到的完整命令
 * 由主循环在 hbt.cmd_ready == 1 时调用
 *
 * 从 DMA 循环缓冲区复制数据，定位 $X#D# 帧头后调用 Bluetooth_ParseCommand
 * IDLE 中断机制参见 stm32f1xx_it.c USART1_IRQHandler
 */
void Bluetooth_ProcessCommand(void) {
    if (!hbt.cmd_ready) return;
    hbt.cmd_ready = 0;

    /*
     * 只停 RX DMA，不动 TX DMA——否则会掐断正在发送的数据帧，
     * 导致小程序收到损坏帧后停止更新。
     */
    CLEAR_BIT(huart1.Instance->CR3, USART_CR3_DMAR);
    if (huart1.hdmarx) HAL_DMA_Abort(huart1.hdmarx);
    huart1.RxState = HAL_UART_STATE_READY;

    uint16_t len = BT_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);

    if (len > 0 && len < BT_RX_BUF_SIZE) {
        memcpy(hbt.cmd_buf, hbt.rx_buf, len);
        hbt.cmd_buf[len] = '\0';
    }

    /* 重启 DMA 循环接收，计数器复位，从 rx_buf[0] 开始 */
    HAL_UART_Receive_DMA(&huart1, hbt.rx_buf, BT_RX_BUF_SIZE);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    if (len == 0 || len >= BT_RX_BUF_SIZE) return;

    /*
     * 遍历所有 $X#D# 帧——不止最后一帧。
     * 只解析校验码完整的帧（有 &XX），跳过 DMA stop 间混入的不完整帧头。
     */
    const char *scan = hbt.cmd_buf;
    while (1) {
        const char *next = strstr(scan, "$X#D#");
        if (!next) break;
        scan = next + 1;
        /* 校验码存在性检查：& 后至少要有 2 个 hex 字符 */
        const char *amp = strchr(next, '&');
        if (amp && amp[1] && amp[2]) {
            Bluetooth_ParseCommand(next);
        }
    }
}

/* ========================================================================
 * 数据同步与状态更新（由主循环每 100ms 调用）
 * ======================================================================== */

void Bluetooth_SetSensorData(float temp, float turb) {
    hbt.temperature = temp;
    hbt.turbidity   = turb;
}

void Bluetooth_SetStatus(const char *temp_st, const char *turb_st) {
    strncpy(hbt.temp_state, temp_st, sizeof(hbt.temp_state) - 1);
    strncpy(hbt.turb_state, turb_st, sizeof(hbt.turb_state) - 1);
}

void Bluetooth_RequestUpload(void) {
    hbt.immediate_upload = 1;
}

/*
 * Bluetooth_CheckTXStuck —— 检测蓝牙 TX DMA 是否卡死并尝试恢复
 * @param now  当前系统滴答（ms）
 * @return 1=已执行恢复, 0=正常无需恢复
 *
 * 当 gState 持续 BUSY_TX 超过 2000ms 时，判定 DMA TX 已卡死。
 * 典型触发场景：HAL_UART_Transmit_DMA 内部 HAL_DMA_Start_IT 静默失败，
 * gState 已设为 BUSY_TX 但 DMA 从未启动，TX 完成中断永远不会到来。
 *
 * 恢复步骤：
 *   1. 清除 USART CR3 的 DMAT 位（断开 UART 与 TX DMA 的硬件链接）
 *   2. 中止 TX DMA 通道，重置其状态为 READY
 *   3. 重置 UART TX 状态为 READY
 *   4. 设置 immediate_upload 以便下一轮主循环重发数据
 */
uint8_t Bluetooth_CheckTXStuck(uint32_t now) {
    if (huart1.gState != HAL_UART_STATE_READY &&
        now - hbt.last_tx_start > 2000) {

        CLEAR_BIT(huart1.Instance->CR3, USART_CR3_DMAT);
        HAL_DMA_Abort(&hdma_usart1_tx);
        hdma_usart1_tx.State = HAL_DMA_STATE_READY;
        huart1.gState = HAL_UART_STATE_READY;

        hbt.immediate_upload = 1;
        return 1;
    }
    return 0;
}

/* ========================================================================
 * HAL 回调（保留，本系统使用 IDLE 中断而非 DMA 完成回调）
 * ======================================================================== */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    (void)huart;
}
