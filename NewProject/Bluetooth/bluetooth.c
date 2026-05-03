/**
 * @file    bluetooth.c
 * @brief   蓝牙通信模块：小机云蓝牙 X-B01 文本协议
 *          电子秤/POS 系统 —— 上行数据上报 / 下行命令解析 + BCC 校验
 *          DMA 循环接收 + IDLE 中断帧检测
 *
 * 协议格式：$X#D#key1:val1;key2:val2;&CheckCode\r\n
 * 回复格式：$XA#D#OK&CheckCode\r\n  /  $XA#D#ERR:message&CheckCode\r\n
 * CheckCode = BCC 异或校验（& 前所有字节异或，输出 2 位大写 hex）
 *
 * 上行数据字段（共 5 个）：
 *   text_category       → 商品类别名称
 *   number_unit_price   → 单价
 *   number_weight       → 重量
 *   number_total_price  → 总价
 *   text_status         → 状态文本
 */

#include "bluetooth.h"
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
 * 使用 HAL_UART_Transmit_DMA 替代阻塞模式，防止 HAL 状态机冲突
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

    /* 等待 TC 置位确保上一帧完全移出 */
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

#define FIELD_MATCH(k, s) (key_len == sizeof(s)-1 && memcmp(k, s, key_len) == 0)

/*
 * parse_field —— 解析单个 key:value 并写入对应系统参数
 * 处理小程序下发的命令：设置类别、单价，或触发重称
 */
static void parse_field(const char *key, uint16_t key_len, const char *val) {
    if (FIELD_MATCH(key, "selected_category")) {
        /* 小程序设置类别：传递类别名称，在 oled 中查找匹配 */
        extern void OLED_SetCategoryByName(const char *name);
        OLED_SetCategoryByName(val);
        return;
    }
    if (FIELD_MATCH(key, "number_unit_price")) {
        float v = strtof(val, NULL);
        if (v > 0) {
            extern void OLED_SetUnitPrice(float price);
            OLED_SetUnitPrice(v);
        }
        return;
    }
    if (FIELD_MATCH(key, "cmd_reweigh")) {
        /* 小程序触发重称 */
        extern void Scale_TriggerReweigh(void);
        Scale_TriggerReweigh();
        return;
    }
}

/* ========================================================================
 * 命令解析
 * ======================================================================== */

/*
 * Bluetooth_ParseCommand —— 解析文本协议命令
 * 流程：校验前缀 → BCC 校验 → 逐字段解析 → 回复 OK
 */
void Bluetooth_ParseCommand(const char *cmd) {
    if (!cmd) return;

    if (strncmp(cmd, "$X#D#", 5) != 0) {
        send_response("ERR:D");
        return;
    }

    const char *amp = strchr(cmd, '&');
    if (!amp) {
        send_response("ERR:D");
        return;
    }

    uint8_t calc_bcc = BCC_Calculate((const uint8_t *)cmd, amp - cmd);
    char hex_str[3] = {amp[1], amp[2], '\0'};
    uint8_t expected_bcc = (uint8_t)strtol(hex_str, NULL, 16);

    if (calc_bcc != expected_bcc) {
        send_response("ERR:XOR");
        return;
    }

    const char *content = cmd + 5;
    const char *end = amp;

    while (content < end) {
        const char *semi = memchr(content, ';', end - content);
        if (!semi) semi = end;

        const char *colon = memchr(content, ':', semi - content);
        if (colon) {
            uint16_t key_len = (uint16_t)(colon - content);
            parse_field(content, key_len, colon + 1);
        }

        content = semi + 1;
    }

    send_response("OK");
    Bluetooth_RequestUpload();
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

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
 * Bluetooth_SendData —— 向手机 APP 上报电子秤数据
 *
 * 文本协议格式（5 个字段）：
 *   $X#D#text_category:xxx;number_unit_price:xx.x;
 *        number_weight:xx.x;number_total_price:xx.x;
 *        text_status:xxx&XX\r\n
 */
void Bluetooth_SendData(void) {
    int len = snprintf(tx_buf, sizeof(tx_buf),
        "$X#D#"
        "text_category:%s;"
        "number_unit_price:%.2f;"
        "number_weight:%.3f;"
        "number_total_price:%.2f;"
        "text_status:%s",
        hbt.category,
        hbt.unit_price,
        hbt.weight,
        hbt.total_price,
        hbt.status_text);

    if (len <= 0 || len >= (int)sizeof(tx_buf) - 5) return;

    uint8_t bcc = BCC_Calculate((uint8_t *)tx_buf, len);
    char bcc_str[2];
    BCC_Format(bcc, bcc_str);
    len += snprintf(tx_buf + len, sizeof(tx_buf) - len,
                    "&%c%c\r\n", bcc_str[0], bcc_str[1]);

    if (len > 0 && len < BT_TX_BUF_SIZE) {
        extern volatile uint32_t sys_tick_ms;

        /* TC 等待：确保上一帧已从移位寄存器完全移出 */
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
 * IDLE 中断机制参见 stm32f1xx_it.c USART1_IRQHandler
 */
void Bluetooth_ProcessCommand(void) {
    if (!hbt.cmd_ready) return;
    hbt.cmd_ready = 0;

    CLEAR_BIT(huart1.Instance->CR3, USART_CR3_DMAR);
    if (huart1.hdmarx) HAL_DMA_Abort(huart1.hdmarx);
    huart1.RxState = HAL_UART_STATE_READY;

    uint16_t len = BT_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);

    if (len > 0 && len < BT_RX_BUF_SIZE) {
        memcpy(hbt.cmd_buf, hbt.rx_buf, len);
        hbt.cmd_buf[len] = '\0';
    }

    HAL_UART_Receive_DMA(&huart1, hbt.rx_buf, BT_RX_BUF_SIZE);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    if (len == 0 || len >= BT_RX_BUF_SIZE) return;

    /* 遍历所有 $X#D# 帧 */
    const char *scan = hbt.cmd_buf;
    while (1) {
        const char *next = strstr(scan, "$X#D#");
        if (!next) break;
        scan = next + 1;
        const char *amp = strchr(next, '&');
        if (amp && amp[1] && amp[2]) {
            Bluetooth_ParseCommand(next);
        }
    }
}

/* ========================================================================
 * 数据同步与状态更新
 * ======================================================================== */

void Bluetooth_SetScaleData(const char *category, float unit_price,
                            float weight, float total_price) {
    strncpy(hbt.category, category, sizeof(hbt.category) - 1);
    hbt.unit_price = unit_price;
    hbt.weight = weight;
    hbt.total_price = total_price;
}

void Bluetooth_SetStatus(const char *status) {
    strncpy(hbt.status_text, status, sizeof(hbt.status_text) - 1);
}

void Bluetooth_RequestUpload(void) {
    hbt.immediate_upload = 1;
}

/*
 * Bluetooth_CheckTXStuck —— 检测蓝牙 TX DMA 是否卡死并尝试恢复
 * @return 1=已执行恢复, 0=正常
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
 * HAL 回调
 * ======================================================================== */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    (void)huart;
}
