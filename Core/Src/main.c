/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 生鲜结算系统主入口 —— 模块编排器
  *                   所有业务逻辑分散在独立模块中：
  *                     buzzer.c 蜂鸣器 / alarm.c 报警 / led.c 心跳灯
  *                     oled.c 显示 / key.c 按键 / sensor.c 称重
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "key.h"
#include "sensor.h"
#include "buzzer.h"
#include "alarm.h"
#include "led.h"
#include "voice.h"
#include "bluetooth.h"
#include <string.h>
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
extern volatile uint32_t sys_tick_ms;
extern volatile uint8_t  task_flag_10ms;
extern volatile uint8_t  task_flag_100ms;
static uint32_t last_bt_upload = 0;
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
  MX_I2C1_Init();
  MX_USART1_UART_Init();   /* BLE5.4 蓝牙 */
  MX_USART2_UART_Init();   /* ASR-PRO 语音 */
  MX_TIM2_Init();           /* 蜂鸣器 PWM */

  /* USER CODE BEGIN 2 */
  OLED_Setup();
  Boot_Interface_Show();   /* 标题立即可见，进度条+Ready 在 Sensor_Init 之后 */
  oled_force_render = 1;
  OLED_UpdateDisplay(sys_tick_ms);

  Key_Init();
  Buzzer_Init();

  Alarm_Init();
  LED_Init();
  Voice_Init();
  Bluetooth_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = sys_tick_ms;

    /* ============================================================
     * 10ms 周期：按键扫描
     * ============================================================ */
    if (task_flag_10ms) {
        task_flag_10ms = 0;
        Key_Scan();
    }

    /* ============================================================
     * 100ms 周期：传感器 → 重量 → 报警
     * ============================================================ */
    if (task_flag_100ms) {
        task_flag_100ms = 0;

        Sensor_Update();
        float w_display = Sensor_GetWeight();
        float w_alarm   = Sensor_GetRawWeight();

        /* 钳位：传感器卸掉重物后可能有微小负漂，强制 ≥0 */
        if (w_display < 0.0f) w_display = 0.0f;
        if (w_alarm   < 0.0f) w_alarm   = 0.0f;

        /* 去皮：净重 = 毛重 - 皮重 */
        float w_net = w_display;
        if (holog.tare_active) {
            w_net -= holog.tare_weight;
            if (w_net < 0.0f) w_net = 0.0f;
        }

        /* 显示重量截取 2 位小数参与总价计算 */
        float w_rounded = (float)((int)(w_net * 100.0f + 0.5f)) / 100.0f;

        float prev = holog.scale.weight;
        holog.scale.weight = w_rounded;
        holog.scale.total_price = holog.scale.unit_price * w_rounded;
        float d = w_rounded - prev;
        if (d < 0) d = -d;
        if (d >= 0.002f) oled_force_render = 1;

        /* 报警使用未经死区的值（净重），以检测小幅抖动 */
        {
            float w_alarm_net = w_alarm;
            if (holog.tare_active) {
                w_alarm_net -= holog.tare_weight;
                if (w_alarm_net < 0.0f) w_alarm_net = 0.0f;
            }
            Alarm_Update(w_alarm_net, now);
        }

        if (Alarm_ShouldEnterPage() && holog.current_page != PAGE_ALARM) {
            uint8_t ow = (Alarm_GetState() == ALARM_OVERWEIGHT);
            uint8_t we = (Alarm_GetState() == ALARM_WEIGHT_ERR);
            OLED_SetAlarmFlags(ow, we);
            holog.prev_page = holog.current_page;
            holog.current_page = PAGE_ALARM;
        }
        if (!Alarm_IsActive() && holog.current_page == PAGE_ALARM) {
            OLED_SetAlarmFlags(0, 0);
            holog.current_page = holog.prev_page;
            holog.edit_mode = 0;
            holog.selected_item = 0;
        }
    }

    /* ============================================================
     * 按键事件消费
     * ============================================================ */
    {
        KeyId id;
        uint8_t is_long;
        while (Key_GetEvent(&id, &is_long)) {
            if (holog.current_page == PAGE_ALARM) {
                /* 报警页：任意键 → 退出页面，报警条件保留 */
                Alarm_Dismiss();
                holog.current_page = holog.prev_page;
                holog.edit_mode = 0;
                holog.selected_item = 0;
                /* 清空剩余按键事件，防止误触下层页面 */
                while (Key_GetEvent(&id, &is_long)) {}
                break;
            } else {
                Buzzer_ShortBeep();
                OLED_HandleKey(id, is_long);
            }
        }
    }

    /* ============================================================
     * 周期刷新
     * ============================================================ */
    OLED_UpdateDisplay(now);
    Buzzer_Process(now,
                   holog.current_page == PAGE_ALARM,
                   Alarm_IsActive());
    LED_Process(now, Alarm_IsActive());
    Voice_Process(now,
                  holog.current_page == PAGE_ALARM,
                  Alarm_GetState() == ALARM_OVERWEIGHT,
                  Alarm_GetState() == ALARM_WEIGHT_ERR);

    /* ============================================================
     * 蓝牙：数据同步 + 定时上报 + 命令处理 + TX 卡死恢复
     * ============================================================ */
    {
        /* 活数据：计算重量/去皮值 */
        float weight   = holog.scale.weight;
        float tare_val = 0.0f;
        if (holog.tare_active) {
            tare_val = holog.tare_weight;
        }
        Bluetooth_SetLiveData(holog.scale.category_idx,
                              holog.scale.unit_price,
                              weight, tare_val,
                              holog.scale.total_price,
                              holog.tare_active ? 1 : 0);

        /* 状态管理：
         * - 报警激活 → 覆盖为报警文本，保存之前的操作状态
         * - 报警结束 → 恢复之前的操作状态（2s 后自动清空）
         * - 无报警   → 操作状态 2s 后自动清空
         */
        if (Alarm_IsActive()) {
            if (!hbt.status_from_alarm) {
                /* 报警首次触发：保存当前操作状态 */
                strncpy(hbt.prev_operation_status, hbt.text_state,
                        sizeof(hbt.prev_operation_status) - 1);
                hbt.prev_operation_status[sizeof(hbt.prev_operation_status) - 1] = '\0';
                hbt.status_from_alarm = 1;
            }
            if (Alarm_GetState() == ALARM_OVERWEIGHT)
                Bluetooth_SetStatus("Overweight! Please lose weight");
            else
                Bluetooth_SetStatus("Weight abnormal! Please measure again");
        } else {
            if (hbt.status_from_alarm) {
                /* 报警结束：恢复之前的操作状态 */
                strncpy(hbt.text_state, hbt.prev_operation_status,
                        sizeof(hbt.text_state) - 1);
                hbt.text_state[sizeof(hbt.text_state) - 1] = '\0';
                hbt.status_from_alarm = 0;
                if (hbt.text_state[0] != '\0') {
                    /* 有操作状态：重置 2s 计时 */
                    hbt.status_set_time = now;
                } else {
                    /* 无操作状态：清空计时 */
                    hbt.status_set_time = 0;
                }
            } else if (hbt.status_set_time != 0 &&
                       now - hbt.status_set_time >= 2000) {
                /* 操作状态 2s 后自动清空 */
                Bluetooth_SetStatus("");
                hbt.status_set_time = 0;
            }
        }

        if (hbt.immediate_upload || now - last_bt_upload >= BT_PERIOD_MS) {
            hbt.immediate_upload = 0;
            last_bt_upload = now;
            Bluetooth_SendData();
        }

        Bluetooth_ProcessCommand();
        Bluetooth_CheckTXStuck(now);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

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
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
