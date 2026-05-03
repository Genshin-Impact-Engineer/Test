/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sensor.h"
#include "key.h"
#include "stepper.h"
#include "oled.h"
#include "alarm.h"
#include "bluetooth.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/*
 * 全局系统滴答计数器 —— 以毫秒为单位的时间基准
 * 在 HAL_TIM_PeriodElapsedCallback(TIM3) 中递增（每 1ms 加 1）
 * 所有模块（按键消抖、报警检测、OLED 刷新、蓝牙定时）均基于此计时
 * 声明为 volatile 防止编译器优化，确保 ISR 和主循环间的一致性
 */
volatile uint32_t sys_tick_ms = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /*
   * ── 外设启动 ──
   * TIM2 = 步进电机 100Hz 驱动（100Hz 更新中断 → Stepper_TIM_Step）
   * TIM3 = 系统滴答（1kHz 更新中断 → sys_tick_ms++）
   * 先启动定时器再初始化各模块，确保模块初始化中如果需要延时也能正常工作
   */
  HAL_TIM_Base_Start_IT(&htim2);
  HAL_TIM_Base_Start_IT(&htim3);
  Sensor_Init();       /* 启动 ADC DMA 循环采集 */
  Key_Init();          /* 按键状态初始化 */
  Stepper_Init();      /* 步进电机归零 */
  OLED_Setup();        /* OLED 显示参数初始化 */
  Boot_Interface_Show();  /* 显示启动画面（进度条动画） */
  Alarm_Init();        /* 报警模块初始化 */
  Bluetooth_Init();    /* 蓝牙模块初始化（启动 USART1 DMA 接收） */
  /* USER CODE END 2 */

  /*
   * ──── 主循环：事件驱动的时间片轮询架构 ────
   *
   * 不使用 RTOS，采用超级循环 + 时间片轮询方式实现多任务并发。
   * 每个功能模块按固定间隔执行，主干每 1ms 循环一次。
   *
   * 时间片分配：
   *   ┌────────────┬────────┬───────────────────────────────┐
   *   │ 模块        │ 间隔    │ 功能                          │
   *   ├────────────┼────────┼───────────────────────────────┤
   *   │ Key_Scan   │ 10ms   │ 按键消抖 + 事件生成             │
   *   │ Sensor_Update│ 10ms  │ ADC 数据处理（由 dma_flag 触发）│
   *   │ AutoGear   │ 15s    │ 自动模式档位重新计算             │
   *   │ Alarm      │ 100ms  │ 阈值检测 + 语音报警             │
   *   │ OLED       │ 100/500ms │ 显示刷新（编辑/正常模式）    │
   *   │ Bluetooth  │ 500ms  │ 数据上发文本协议到手机 APP      │
   *   │ Heartbeat  │ 1s     │ LED 闪烁指示系统运行中           │
   *   └────────────┴────────┴───────────────────────────────┘
   *
   * 所有时间基准基于 sys_tick_ms（TIM3 中断 1ms 递增一次）
   * 使用差值比较（now - last_xxx），而非累加计数器，避免溢出问题
   */
  uint32_t last_key_scan = 0, last_alarm = 0, last_oled = 0, last_bt = 0, last_auto = 0, last_heartbeat = 0;
  uint32_t now;

  while (1)
  {
    now = sys_tick_ms;

    /*
     * ── 1. 按键扫描 + 传感器更新（10ms） ──
     * 按键采用状态机检测（消抖→短按/长按判定），事件存入环形缓冲
     * USB 传感器 ADC 由 DMA 连续循环采集，10ms 检查一次 dma_flag
     */
    if (now - last_key_scan >= 10) {
      last_key_scan = now;
      Key_Scan();  /* 按键消抖状态机 */

      /* 处理按键事件队列（可能存在多个同时累积的事件） */
      KeyId kid; uint8_t is_long;
      while (Key_GetEvent(&kid, &is_long)) {
        OLED_HandleKey(kid, is_long);   /* 菜单导航或参数编辑 */
        /*
         * 以下情况触发蓝牙立即上传（更新手机 APP 显示）：
         * - 报警激活中（任何按键都可确认报警，需同步状态）
         * - 长按事件（页面切换或特殊功能）
         * - K2 按键（确认/进入编辑 → 参数一定变了）
         */
        if (halarm.temp_alarmed || halarm.turb_alarmed || is_long || kid == KEY_K2) {
          Bluetooth_RequestUpload();
        }
      }

      /* ADC DMA 转换完成标志：处理传感器新数据 */
      if (hsensor.dma_flag) {
        hsensor.dma_flag = 0;       /* 清零标志（由 ISR 置位，主循环消费） */
        Sensor_Update();             /* 加权滤波 + 物理量换算 */
      }
    }

    /*
     * ── 2. 自动模式档位调节（5 秒） ──
     * 在自动模式下根据当前温度与预设温度的偏差计算目标档位，
     * 并驱动步进电机转到对应位置。
     * 5 秒检查一次，避免电机过于频繁动作造成磨损。
     */
    if (now - last_auto >= 5000 && holog.settings.valve_mode_auto) {
      last_auto = now;
      uint8_t gear = Stepper_CalcAutoGear(hsensor.temperature, holog.data.preset_temp);
      holog.data.water_valve = gear;      /* 同步到 OLED 显示 */
      Stepper_SetGear(gear);               /* 驱动步进电机 */
    }

    /*
     * ── 3. 报警检测（100ms）──
     * 双路独立状态机：温度/浊度各走各的 5s 触发 / 2s 恢复
     */
    if (now - last_alarm >= 100) {
      last_alarm = now;
      Alarm_Process(now, hsensor.temperature, hsensor.turbidity,
                    holog.settings.temp_max, holog.settings.temp_min,
                    holog.settings.turb_threshold);

      /* 处理新报警触发：跳转页面 + 蓝牙强制上传 */
      if (halarm.new_trigger) {
        uint8_t type = halarm.new_trigger_type;
        halarm.new_trigger = 0;

        if (type) {  /* 1=temp 2=turb 3=both */
          OLED_SetAlarmFlags(halarm.temp_alarmed, halarm.turb_alarmed);
          if (holog.current_page != PAGE_ALARM)
            holog.prev_page = holog.current_page;
          holog.current_page = PAGE_ALARM;
        } else {     /* 0=消警 */
          OLED_SetAlarmFlags(0, 0);
        }
        Bluetooth_RequestUpload();
      }

      /* 同步报警状态到蓝牙 */
      const char *tstr, *wstr;
      Alarm_GetStatusStrings(&tstr, &wstr);
      Bluetooth_SetSensorData(hsensor.temperature, hsensor.turbidity);
      Bluetooth_SetStatus(tstr, wstr);
    }

    /*
     * ── 4. OLED 显示刷新 ──
     * 编辑模式：100ms 刷新（闪烁效果需要）
     * 正常模式：500ms 刷新（降低 I2C 总线负载）
     * 内置 I2C 故障恢复机制（见 oled.c 中 _oled_probe / OLED_ForceRecover）
     */
    if (now - last_oled >= (holog.edit_mode ? 100 : 500)) {
      last_oled = now;
      OLED_UpdateDisplay(now);
    }

    /*
     * ── 5. 蓝牙命令处理 ──
     * 当 USART1 IDLE 中断检测到完整消息帧时 cmd_ready=1
     * 解析文本协议命令并更新对应参数
     * 详见 bluetooth.c Bluetooth_ProcessCommand()
     */
    if (hbt.cmd_ready) {
      Bluetooth_ProcessCommand();
    }
    /* UART 空闲时才发，忙时保留 immediate_upload 下轮重试 */
    if (now - last_bt >= BT_PERIOD_MS || hbt.immediate_upload) {
      if (huart1.gState == HAL_UART_STATE_READY) {
        last_bt = now;
        hbt.immediate_upload = 0;
        Bluetooth_SendData();
      } else {
        Bluetooth_CheckTXStuck(now);
      }
    }

    /* 心跳：1s */
    if (now - last_heartbeat >= 1000) {
      last_heartbeat = now;
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV2;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        Stepper_TIM_Step();
    } else if (htim->Instance == TIM3) {
        sys_tick_ms++;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    KeyId id;
    switch (GPIO_Pin) {
        case GPIO_PIN_12: id = KEY_K1; break;
        case GPIO_PIN_13: id = KEY_K2; break;
        case GPIO_PIN_14: id = KEY_K3; break;
        case GPIO_PIN_15: id = KEY_K4; break;
        default: return;
    }
    Key_ISR(id);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
