/**
 * @file    sensor.c
 * @brief   传感器数据采集与处理：PT100 温度 + TSW-30 浊度
 *          集成 ADC DMA 循环采样、滑动加权滤波、温度补偿校准
 */

#include "sensor.h"
#include "adc.h"

/* 全局传感器数据实例，所有模块通过 extern 引用 */
Sensor_t hsensor = {0};

/*
 * 滑动加权滤波权重表 —— 最新样本权重最高，提高瞬态响应
 *
 * 之前权重固定映射到 buf[i]：buf[0]×4 + buf[1]×3 + buf[2]×2 + buf[3]×1，
 * 但 buf 是环形写入（0→1→2→3→0…），最新样本不一定在 buf[0]。
 * 修复后按 age 计算：age 0（最新）→ 权重 4，age 3（最旧）→ 权重 1。
 * 加权和分母 = 4+3+2+1 = 10
 */
static const uint8_t age_weights[SENSOR_FILTER_DEPTH] = {4, 3, 2, 1};
static const uint8_t filter_weight_sum = 10;

/*
 * adc_to_voltage —— 将 ADC 原始读数转换为电压值（V）
 *
 * 注意：TSW-30 AO 输出 0~5V，STM32F103 ADC 参考电压 3.3V。
 * 若硬件使用了电阻分压（如 2:1），需在返回值乘以分压比恢复实际电压：
 *   return (float)raw / SENSOR_ADC_RES * SENSOR_VREF * DIVIDER_RATIO;
 * 若无分压电路，>3.3V 的输入将被 ADC 钳位，无法正确测量高浊度（低电压）段。
 */
static float adc_to_voltage(uint16_t raw) {
    return (float)raw / SENSOR_ADC_RES * SENSOR_VREF;
}

/*
 * Sensor_Init —— 初始化传感器模块
 * 1. 设置浊度 K 值（初始默认 2500.0，反映无浊水下的基准电压）
 * 2. 温度偏移设为 0（可通过校准调整）
 * 3. 滤波索引清零
 * 4. 启动 ADC1 DMA 循环采集：双通道连续转换
 *    - 通道 x：PT100 温度传感器（通过运放放大后输入）
 *    - 通道 y：TSW-30 浊度传感器
 *    DMA 工作在循环模式，自动更新 adc_buf[0]=温度, adc_buf[1]=浊度
 */
void Sensor_Init(void) {
    hsensor.K_value = 2500.0f;
    hsensor.temp_offset = 0.0f;
    hsensor.filter_index = 0;
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)hsensor.adc_buf, SENSOR_ADC_BUF_SIZE);
}

/*
 * Sensor_Update —— 处理 ADC 最新一轮采集数据
 * 由 stm32f1xx_it.c 中的 HAL_ADC_ConvCpltCallback 设置 dma_flag 后，
 * 在 main 循环中调用（见 main.c ~line 141-144）
 *
 * 处理流程：
 * 1. 从 DMA 缓冲区读取原始 ADC 值
 * 2. 存入环形滤波器缓冲，更新索引（0→1→2→3→0…循环）
 * 3. 对 4 个样本进行加权平均得到滤波后的 ADC 值
 * 4. 将滤波后的 ADC 值转换为电压
 * 5. PT100 温度计算：T = V * 51.2 + offset
 *    - PT100 通过电桥+运放连接到 ADC，斜率 51.2°C/V 由电路设计决定
 *    - temp_offset 用于系统校准补偿
 * 6. TSW-30 浊度计算：
 *    - 基本公式：NTU = -865.68 * V + K
 *    - 温度补偿：ΔU = -0.0192 * (T - 25°C)
 *    - 补偿后的浊度电压：V_comp = V_turb - ΔU
 *    - 最终浊度：NTU = -865.68 * V_comp + K
 *    温度补偿修正了 LED 输出随温度漂移的特性
 */
void Sensor_Update(void) {
    /* 环形写入：idx 指向下一写入位置，写入后 filter_index 递增 */
    uint16_t raw_temp = hsensor.adc_buf[0];
    uint16_t raw_turb = hsensor.adc_buf[1];
    uint8_t idx = hsensor.filter_index++ % SENSOR_FILTER_DEPTH;

    hsensor.raw_temp_buf[idx] = raw_temp;
    hsensor.raw_turb_buf[idx] = raw_turb;

    /*
     * 按样本年龄加权求和：age 0（最新，buf[idx]）→ 权重 4
     *                     age 1（次新，buf[idx-1]）→ 权重 3 … 最旧 → 权重 1
     * 环形逆向遍历 (idx - age) mod DEPTH 以适配环形写入顺序
     */
    uint32_t temp_sum = 0, turb_sum = 0;
    for (int age = 0; age < SENSOR_FILTER_DEPTH; age++) {
        int i = (idx - age + SENSOR_FILTER_DEPTH) % SENSOR_FILTER_DEPTH;
        temp_sum += hsensor.raw_temp_buf[i] * age_weights[age];
        turb_sum += hsensor.raw_turb_buf[i] * age_weights[age];
    }
    uint16_t filtered_temp = (uint16_t)(temp_sum / filter_weight_sum);
    uint16_t filtered_turb = (uint16_t)(turb_sum / filter_weight_sum);

    /* 将滤波后的值转换为实际物理量 */
    float v_temp = adc_to_voltage(filtered_temp);
    float v_turb = adc_to_voltage(filtered_turb);

    /* 温度 = 电压 * PT100 电路增益 + 偏移校准 */
    hsensor.temperature = v_temp * PT100_SLOPE + hsensor.temp_offset;

    /* 浊度温度补偿：TSW-30 传感器的温度漂移系数 -0.0192 V/°C */
    float delta_u = TEMP_COMP_COEFF * (hsensor.temperature - 25.0f);
    float v_compensated = v_turb - delta_u;
    /* 浊度 = 线性变换 + K 值校准 */
    hsensor.turbidity = TURB_SLOPE * v_compensated + hsensor.K_value;
    /* 防止负值（低浊度时可能因噪声出现） */
    if (hsensor.turbidity < 0.0f) hsensor.turbidity = 0.0f;
}

/* 获取最近一次采集的温度值（°C） */
float Sensor_GetTemperature(void) {
    return hsensor.temperature;
}

/* 获取最近一次采集的浊度值（NTU） */
float Sensor_GetTurbidity(void) {
    return hsensor.turbidity;
}

/*
 * Sensor_SetCalibration —— 更新传感器校准参数
 * @param K      浊度校准常数（NTU），调节标准液对应的基准值
 * @param offset 温度校准偏移（°C），在已知参考温度下修正
 * 通常在系统校准时调用，可通过蓝牙命令远程更新
 */
void Sensor_SetCalibration(float K, float offset) {
    hsensor.K_value = K;
    hsensor.temp_offset = offset;
}
