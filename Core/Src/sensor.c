#include "sensor.h"

static int32_t zero_offset  = DEFAULT_ZERO_OFFSET;
static float   scale_factor = DEFAULT_SCALE_FACTOR;
static float   filtered_w   = 0.0f;

/* ---- HX711 底层读取 --------------------------------------------------------- */
static int32_t HX711_Read(void)
{
    int32_t data = 0;
    uint32_t timeout = 200000;

    /* 等待 DOUT 拉低（数据就绪） */
    while (HAL_GPIO_ReadPin(HX711_DOUT_PORT, HX711_DOUT_PIN) == GPIO_PIN_SET) {
        if (--timeout == 0) return -1;
    }

    /* 24 个脉冲读 24 位数据（MSB 先出） */
    for (int i = 0; i < 24; i++) {
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
        data = (data << 1) | HAL_GPIO_ReadPin(HX711_DOUT_PORT, HX711_DOUT_PIN);
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    }

    /* 第 25 个脉冲: 下一次转换 = A 通道 / 增益 128 */
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);

    data ^= 0x800000;   /* 补码→无符号 */
    return data;
}

/* ---- 对外接口 --------------------------------------------------------------- */

void Sensor_Init(void)
{
    /* SCK 拉低使能 HX711 正常转换模式 */
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    HAL_Delay(500);     /* 等待模块稳定 >400ms (HX711.md §4.2) */

    /* 丢弃前几次不稳定读数 */
    for (int i = 0; i < 5; i++) {
        HX711_Read();
    }

    filtered_w = 0.0f;
}

void Sensor_Update(void)
{
    int32_t raw = HX711_Read();
    if (raw < 0) return;            /* 超时，保持上次滤波值 */

    float raw_w = (float)(raw - zero_offset) * scale_factor;

    /* 一阶滞后滤波: Y(n) = α·X(n) + (1-α)·Y(n-1) */
    filtered_w = FILTER_ALPHA * raw_w + (1.0f - FILTER_ALPHA) * filtered_w;
}

float Sensor_GetWeight(void)
{
    return filtered_w;
}
