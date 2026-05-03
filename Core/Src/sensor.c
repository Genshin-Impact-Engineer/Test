#include "sensor.h"

static int32_t zero_offset   = 0;
static float   scale_factor  = DEFAULT_SCALE_FACTOR;
static float   filtered_w    = 0.0f;
static float   last_out      = 0.0f;
static float   raw_filtered  = 0.0f;  /* 滤波后但未经死区的值 */

/* 微秒级短延时（8MHz HSI 下约 1μs） */
#define HX711_US_DELAY()  do { volatile int _d = 8; while (--_d); } while(0)

/* ---- 滑动均值缓冲 ----------------------------------------------------------- */
#define BUF_SZ 4
static int32_t raw_buf[BUF_SZ];
static uint8_t buf_idx = 0;
static uint8_t buf_full = 0;

static int32_t moving_avg(int32_t new_val)
{
    raw_buf[buf_idx] = new_val;
    buf_idx = (buf_idx + 1) % BUF_SZ;
    if (buf_idx == 0) buf_full = 1;

    int32_t sum = 0;
    int n = buf_full ? BUF_SZ : buf_idx;
    for (int i = 0; i < n; i++) sum += raw_buf[i];
    return sum / n;
}

/* ---- HX711 底层读取 --------------------------------------------------------- */
static int32_t HX711_Read(void)
{
    int32_t data = 0;
    uint32_t timeout = 200000;

    while (HAL_GPIO_ReadPin(HX711_DOUT_PORT, HX711_DOUT_PIN) == GPIO_PIN_SET) {
        if (--timeout == 0) return -1;
    }

    for (int i = 0; i < 25; i++) {
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_SET);
        HX711_US_DELAY();
        if (i < 24) {
            data = (data << 1) | HAL_GPIO_ReadPin(HX711_DOUT_PORT, HX711_DOUT_PIN);
        }
        HX711_US_DELAY();
        HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
        HX711_US_DELAY();
    }

    data ^= 0x800000;
    return data;
}

/* ---- 一阶滞后滤波（自适应 α）----------------------------------------------- */
static float adaptive_filter(float raw_w)
{
    float diff = raw_w - filtered_w;
    if (diff < 0) diff = -diff;

    /* 变化大 → 快速跟踪；变化小 → 强力平滑 */
    float alpha = (diff > 0.010f) ? FILTER_ALPHA_FAST : FILTER_ALPHA_SLOW;
    filtered_w = alpha * raw_w + (1.0f - alpha) * filtered_w;
    return filtered_w;
}

/* ---- 对外接口 --------------------------------------------------------------- */

void Sensor_Init(void)
{
    HAL_GPIO_WritePin(HX711_SCK_PORT, HX711_SCK_PIN, GPIO_PIN_RESET);
    HAL_Delay(500);

    /* 丢弃前 5 次不稳定读数 */
    for (int i = 0; i < 5; i++) {
        HX711_Read();
    }

    /* 开机自动调零: 20 次采样 → 排序 → 去首尾各 3 → 均值 */
    #define TARE_SAMPLES 20
    #define TRIM_EDGE     3
    int32_t s[TARE_SAMPLES];
    int n = 0;
    for (int i = 0; i < TARE_SAMPLES; i++) {
        int32_t raw = HX711_Read();
        if (raw >= 0) s[n++] = raw;
    }

    /* 冒泡排序（20 个元素足够快） */
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1 - i; j++) {
            if (s[j] > s[j + 1]) {
                int32_t t = s[j];
                s[j] = s[j + 1];
                s[j + 1] = t;
            }
        }
    }

    int trim = (n > TRIM_EDGE * 2 + 2) ? TRIM_EDGE : 0;
    int64_t sum = 0;
    for (int i = trim; i < n - trim; i++) sum += s[i];
    zero_offset = (int32_t)(sum / (n - 2 * trim));

    /* 初始化滤波器和缓冲 */
    filtered_w = 0.0f;
    last_out   = 0.0f;
    buf_idx    = 0;
    buf_full   = 0;
    for (int i = 0; i < BUF_SZ; i++) raw_buf[i] = 0;
}

void Sensor_Update(void)
{
    int32_t raw = HX711_Read();
    if (raw < 0) return;

    /* 滑动均值降噪 */
    raw = moving_avg(raw);

    float raw_w = (float)(raw - zero_offset) * scale_factor;

    /* 死区：变化 < 2g 不更新输出，抑制尾数跳变 */
    float w = adaptive_filter(raw_w);
    raw_filtered = w;  /* 保存未经死区的值，供报警抖动检测 */
    float diff = w - last_out;
    if (diff < 0) diff = -diff;
    if (diff >= OUTPUT_DEADBAND) {
        last_out = w;
    }
}

float Sensor_GetWeight(void)
{
    return last_out;
}

float Sensor_GetRawWeight(void)
{
    return raw_filtered;
}
