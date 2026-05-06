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
 * 上行单帧 6 个动态字段（全 ASCII），参照 A 工程结构。
 */

#include "bluetooth.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* USART1 句柄（蓝牙模块），在 usart.c/dma.c 中定义 */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

/* 全局蓝牙通信实例 */
Bluetooth_t hbt = {0};

/* 发送缓冲区 */
static char tx_buf[BT_TX_BUF_SIZE];

/* 回复缓冲区（独立于 tx_buf，避免与 SendData 的 DMA 传输冲突） */
static char resp_buf[64];

/* ========================================================================
 * BCC 异或校验
 * ======================================================================== */

static uint8_t BCC_Calculate(const uint8_t *data, uint16_t len) {
    uint8_t c = 0;
    while (len--) c ^= *data++;
    return c;
}

static void BCC_Format(uint8_t c, char out[2]) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[c >> 4];
    out[1] = hex[c & 0x0F];
}

/* ========================================================================
 * 回复发送
 * ======================================================================== */

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

static void parse_field(const char *key, uint16_t key_len, const char *val) {
    if (FIELD_MATCH(key, "selected_Goods")) {
        uint8_t idx = (uint8_t)strtol(val, NULL, 10);
        extern void OLED_SetCategoryByIndex(uint8_t idx);
        OLED_SetCategoryByIndex(idx);
        return;
    }
    if (FIELD_MATCH(key, "number_Price")) {
        float v = strtof(val, NULL);
        if (v > 0) {
            extern void OLED_SetUnitPrice(float price);
            OLED_SetUnitPrice(v);
        }
        return;
    }
}

/* ========================================================================
 * 命令解析
 * ======================================================================== */

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
 * 数据上传 —— 单帧 14 字段：8 个中文标签 + 6 个动态数值
 *
 * 所有数据在一帧内，与 A 工程格式一致。单 buffer，单 DMA，零碰撞。
 *
 * 参考 A 工程 HVAC，不加 gState 检查，不加 TC 等待，
 * 直接裸调 HAL_UART_Transmit_DMA。
 * ======================================================================== */

void Bluetooth_SendData(void) {
    int len = snprintf(tx_buf, sizeof(tx_buf),
        "$X#D#"
        "selected_Goods:%u;"
        "number_Price:%.2f;"
        "text_float_Weight:%.2f;"
        "text_NetWeight:%.2f;"
        "text_float_Total:%.2f;"
        "text_state:%s",
        hbt.selected_goods,
        hbt.number_price,
        hbt.weight,
        hbt.net_weight,
        hbt.total_price,
        hbt.text_state);

    if (len <= 0 || len >= (int)sizeof(tx_buf) - 5) return;

    uint8_t bcc = BCC_Calculate((uint8_t *)tx_buf, len);
    char bcc_str[2];
    BCC_Format(bcc, bcc_str);
    len += snprintf(tx_buf + len, sizeof(tx_buf) - len,
                    "&%c%c\r\n", bcc_str[0], bcc_str[1]);

    if (len > 0 && len < BT_TX_BUF_SIZE) {
        extern volatile uint32_t sys_tick_ms;
        hbt.last_tx_start = sys_tick_ms;
        HAL_UART_Transmit_DMA(&huart1, (uint8_t *)tx_buf, len);
    }
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
 * 命令处理
 * ======================================================================== */

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

void Bluetooth_SetLiveData(uint8_t goods_idx, float price,
                           float weight, float net_weight, float total) {
    hbt.selected_goods = goods_idx;
    hbt.number_price = price;
    hbt.weight = weight;
    hbt.net_weight = net_weight;
    hbt.total_price = total;
}

void Bluetooth_SetStatus(const char *status) {
    strncpy(hbt.text_state, status, sizeof(hbt.text_state) - 1);
    hbt.text_state[sizeof(hbt.text_state) - 1] = '\0';
}

void Bluetooth_RequestUpload(void) {
    hbt.immediate_upload = 1;
}

/* ========================================================================
 * TX 卡死检测与恢复（参照 A 工程）
 * ======================================================================== */

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
