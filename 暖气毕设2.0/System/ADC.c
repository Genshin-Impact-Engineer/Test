/**
 * @file    ADC.c
 * @brief   ADC驱动实现，使用DMA进行无阻塞采集
 * @note    温度传感器：A0/PA0（ADC1_CH0）
 *          浊度传感器：A1/PA1（ADC1_CH1）
 *          使用8点滑动平均滤波减少噪声
 */

#include "stm32f10x.h"
#include "ADC.h"

// ======================== 常量定义 ========================
#define ADC_RESOLUTION     4095.0f    // 12位ADC分辨率（对应PT100调理电路）
#define ADC_REF_VOLTAGE    3.3f      // ADC参考电压
#define TU_SENSOR_VOLT_MAX 4.5f      // TSW-30最大输出电压（5V供电时）
#define TU_MAX_VALUE       1000.0f   // TSW-30量程上限（NTU）
#define TU_TEMP_COEFF      -0.0192f  // TSW-30温度补偿系数

// ======================== DMA缓冲区 ========================
#define ADC_DMA_BUF_SIZE    16        // DMA缓冲区大小
static u16 adc_dma_buf[ADC_DMA_BUF_SIZE];

// ======================== 8点滑动滤波缓冲区 ========================
#define FILTER_SIZE         8         // 滤波窗口大小
static float temp_filter_buf[FILTER_SIZE];
static float turb_filter_buf[FILTER_SIZE];
static u8 temp_buf_idx = 0;
static u8 turb_buf_idx = 0;
static u8 temp_fill_cnt = 0;
static u8 turb_fill_cnt = 0;
// 滤波总和，用于快速滑动平均计算
static float sum_temp = 0.0f;
static float sum_turb = 0.0f;

// ======================== 传感器采集结果 ========================
volatile float g_adc_temp = 0.0f;
volatile float g_adc_turbidity = 0.0f;
volatile uint8_t g_adc_flag = 0;

// ======================== DMA初始化 ========================
static void ADC_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    DMA_DeInit(DMA1_Channel1);
    
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&ADC1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)adc_dma_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = ADC_DMA_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);
    
    DMA_Cmd(DMA1_Channel1, ENABLE);
}

// ======================== ADC初始化 ========================
void ADC_MyInit(void)
{ 	
    ADC_InitTypeDef ADC_InitStructure; 
    GPIO_InitTypeDef GPIO_InitStructure;

    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);  // ADC时钟6分频 = 12MHz

    // 配置GPIO为模拟输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 复位ADC
    ADC_DeInit(ADC1);

    // 配置ADC
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = ENABLE;           // 扫描模式
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;      // 连续转换
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 2;               // 2个通道
    ADC_Init(ADC1, &ADC_InitStructure);

    // 配置ADC通道顺序和采样时间
    // CH0=PA0(温度传感器), CH1=PA1(浊度传感器)
    ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_55Cycles5);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_1, 2, ADC_SampleTime_55Cycles5);

    // 使能ADC DMA
    ADC_DMACmd(ADC1, ENABLE);

    // 使能ADC
    ADC_Cmd(ADC1, ENABLE);

    // ADC校准
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
    
    // 初始化DMA
    ADC_DMA_Init();
    
    // 启动ADC转换
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
}

// ======================== 8点滑动平均滤波 ========================
/**
 * @brief   滑动平均滤波
 * @param   buf 滤波缓冲区
 * @param   new_val 新采样值
 * @param   idx 缓冲区索引
 * @param   fill_cnt 已填充计数
 * @return  滤波后的平均值
 */
static float Sliding_Average_Filter(float buf[], float new_val, u8* idx, u8* fill_cnt, float* sum)
{
    // 存入新值（覆盖最旧的值）
    buf[*idx] = new_val;
    
    // 更新索引（环形）
    *idx = (*idx + 1) % FILTER_SIZE;
    
    // 更新填充计数
    if(*fill_cnt < FILTER_SIZE) {
        (*fill_cnt)++;
    }
    
    // 计算平均值
    *sum = 0.0f;
    for(u8 i = 0; i < *fill_cnt; i++) {
        *sum += buf[i];
    }
    return *sum / (*fill_cnt);
}

// ======================== 传感器换算公式 ========================
/**
 * @brief   ADC值转换为温度
 * @param   ad_value ADC原始值（0~4095）
 * @return  温度值（℃）
 * @note    温度传感器公式：温度 = 电压 * 系数
 *          电压 = (ADC值 / 4095) * 3.3V
 */
static float ADC_To_Temperature(u16 ad_value)
{
    // PT100传感器配套调理电路的转换公式
    // 公式：温度 = (ADC值 / 4095) × 3.3 × 100 × 0.512
    // 等效于：温度 = ADC值 × 168.96 / 4095
    float voltage_temp = (ad_value / ADC_RESOLUTION) * ADC_REF_VOLTAGE * 100.0f;
    float temperature = voltage_temp * 0.512f;
    return temperature;
}

/**
 * @brief   ADC值转换为浊度
 * @param   ad_value ADC原始值（0~4095）
 * @param   temperature 当前温度（用于温度补偿）
 * @return  浊度值（NTU）
 * @note    TSW-30浊度传感器公式：
 *          1. ADC值转电压：电压 = (ADC值 / 4095) * 3.3V
 *          2. 电压转标准输出：标准电压 = 电压 * (5.0 / 3.3)
 *          3. 温度补偿：补偿电压 = 标准电压 + ΔU, ΔU = -0.0192 * (T - 25)
 *          4. 浊度计算：浊度 = (4.5 - 补偿电压) / 4.5 * 1000
 */
static float ADC_To_Turbidity(u16 ad_value, float temperature)
{
    // 1. ADC值转电压
    float voltage = (ad_value / ADC_RESOLUTION) * ADC_REF_VOLTAGE;
    
    // 2. 转换为5V系统电压下的标准输出
    // TSW-30标定电压为5V供电，实际使用3.3V ADC
    float standard_voltage = voltage * (5.0f / ADC_REF_VOLTAGE);
    
    // 3. 温度补偿
    float delta_u = TU_TEMP_COEFF * (temperature - 25.0f);
    float compensated_voltage = standard_voltage + delta_u;
    
    // 4. 浊度计算
    float turbidity = (TU_SENSOR_VOLT_MAX - compensated_voltage) / TU_SENSOR_VOLT_MAX * TU_MAX_VALUE;
    
    // 限幅
    if(turbidity < 0) turbidity = 0;
    if(turbidity > TU_MAX_VALUE) turbidity = TU_MAX_VALUE;
    
    return turbidity;
}

// ======================== 数据处理 ========================
/**
 * @brief   传感器采集处理
 * @note    从DMA缓冲区读取数据，进行8点滑动平均滤波
 */
void ADC_Process(void)
{
    if(g_adc_flag == 0) return;
    g_adc_flag = 0;
    
    u16 temp_ad = 0;
    u16 turb_ad = 0;
    u8 temp_count = 0;
    u8 turb_count = 0;
    
    // 从DMA缓冲区读取数据
    // DMA循环填充，CH0在偶数索引，CH1在奇数索引
    for(u8 i = 0; i < ADC_DMA_BUF_SIZE; i++) {
        u16 ad_val = adc_dma_buf[i];
        
        if(i % 2 == 0) {
            // 偶数索引 = 温度通道 (CH0)
            temp_ad += ad_val;
            temp_count++;
        } else {
            // 奇数索引 = 浊度通道 (CH1)
            turb_ad += ad_val;
            turb_count++;
        }
    }
    
    // 计算平均ADC值
    float temp_ad_avg = (temp_count > 0) ? (temp_ad / (float)temp_count) : 0;
    float turb_ad_avg = (turb_count > 0) ? (turb_ad / (float)turb_count) : 0;
    
    // ADC值转物理量
    float temp_raw = ADC_To_Temperature((u16)temp_ad_avg);
    float turb_raw = ADC_To_Turbidity((u16)turb_ad_avg, temp_raw);
    
    // 8点滑动平均滤波
    g_adc_temp = Sliding_Average_Filter(temp_filter_buf, temp_raw, &temp_buf_idx, &temp_fill_cnt, &sum_temp);
    g_adc_turbidity = Sliding_Average_Filter(turb_filter_buf, turb_raw, &turb_buf_idx, &turb_fill_cnt, &sum_turb);
}

float Get_Temperature(void) { return g_adc_temp; }
float Get_Turbidity(void) { return g_adc_turbidity; }
