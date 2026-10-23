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
#include "usart.h"
#include "gpio.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */


#include "stdio.h"  // 用于串口打印
#include "math.h"   // NTC B值法换算需要 logf()
#include "oled.h"
#include "motor.h"


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

// DMA缓冲区 - 双通道ADC数据
// 2026-10-23 PCB 适配：通道号由 IN0/IN1 改为 IN4/IN5，缓冲索引顺序不变
#define ADC_CHANNEL_NUM 2
uint16_t adc_dma_buf[ADC_CHANNEL_NUM];  // [0]=IN4(PA4)电池电压, [1]=IN5(PA5)NTC温度

uint16_t adc_value;
float total_volt;
// 校准后的最终参数
const float adc_ref = 3.3f;
// 分压比：R1 = 51kΩ(±1%) 上臂，R2 = 10kΩ(±1%) 下臂
//   理论值 (51+10)/10 = 6.10；6.21 是上一版面包板实测校准值。
//   新板电阻批次未必一致，首次上电要用万用表实测电池端电压与串口打印值比对，
//   按 实测电压 / 打印电压 × 6.21 修正这个系数。
const float divider_k = 6.21f;

/* ===== NTC 温度换算参数（B 值法）=====
 * 电路：R3 = 10 kΩ(±1%) 上拉至 +3V3，防水探头经 J7 接 GND，PA5 测中点。
 *   V_adc = 3V3 × R_ntc / (R3 + R_ntc)
 *   R_ntc = R3 × V_adc / (3V3 − V_adc)
 *   1/T  = 1/T0 + ln(R_ntc / R0) / B
 */
#define NTC_R_PULLUP   10000.0f   // R3 上拉阻值
#define NTC_R0         10000.0f   // 25 °C 标称阻值
#define NTC_B          3950.0f    // B25/50 值，下单时必须核对是 3950 不是 3435
#define NTC_T0_K       298.15f    // 25 °C 对应的绝对温度

// 探头异常约定值（负温度指数，不会与真实工况混淆）
#define TEMP_NTC_OPEN   (-99.0f)  // J7 未插探头 / 探头断线
#define TEMP_NTC_SHORT  (-98.0f)  // 探头短路到地
#define TEMP_NTC_BAD(t) ((t) < -90.0f)

// 温度相关变量
float temperature = 25.0f;

// 温度阈值 - 迟滞控制
#define TEMP_FAN_ON    28.0f   // 启动风扇温度
#define TEMP_FAN_OFF   26.0f   // 停止风扇温度
#define TEMP_FAN_HIGH  32.0f   // 高速风扇温度

// 风扇状态
uint8_t fan_status = 0;  // 0=停止, 1=低速, 2=高速
uint8_t fan_speed = 0;   // 0-100

// 系统运行状态
uint8_t system_running = 1;
// KEY2按键消抖和长按检测
uint8_t key2_debounce = 0;
uint8_t key2_long_press_cnt = 0;
uint8_t key2_pressed_flag = 0;
uint8_t key2_long_triggered = 0;
// ADC采样计数器（每20次循环采样一次 = 约1秒采样一次）
uint8_t adc_sample_cnt = 0;
// 温度模拟模式：0=正常, 1=模拟高温(35℃)
uint8_t temp_simulate = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void Key_Scan(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// 等待串口发送完成（带超时保护）
void UART_WaitTxComplete(UART_HandleTypeDef *huart, uint32_t timeout)
{
  uint32_t tickstart = HAL_GetTick();
  while(__HAL_UART_GET_FLAG(huart, UART_FLAG_TC) == RESET)
  {
    if((HAL_GetTick() - tickstart) > timeout)
    {
      break;
    }
  }
}

void Key_Scan(void)
{
  // KEY2检测 (PC13, 下拉，按下为高电平)
  if(HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_SET)
  {
    // 消抖
    if(key2_debounce < 10)
    {
      key2_debounce++;
    }
    else
    {
      // 按键已确认按下
      if(key2_pressed_flag == 0)
      {
        key2_pressed_flag = 1;
        key2_long_press_cnt = 0;
        key2_long_triggered = 0;
      }
      else
      {
        // 长按计时
        if(key2_long_press_cnt < 60)  // 60 * 50ms = 3秒
        {
          key2_long_press_cnt++;
        }
        else if(key2_long_triggered == 0)
        {
          // 长按3秒触发：停止系统
          key2_long_triggered = 1;
          
          // 只在系统运行时执行停止操作
          if(system_running == 1)
          {
            // 1. 先打印信息
            printf("\r\n[系统] 系统已经停止\r\n");
            // 2. 等待串口发送完成（最多等200ms）
            UART_WaitTxComplete(&huart1, 200);
            // 3. 关闭LED和蜂鸣器
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
            // 4. 停止风扇（停止电机）
            Motor_Stop();
            fan_status = 0;
            fan_speed = 0;
            // 5. 再改变系统状态
            system_running = 0;
            // 6. 最后蜂鸣器反馈
            HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
            HAL_Delay(100);
            HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
          }
        }
      }
    }
  }
  else
  {
    // 按键释放
    if(key2_pressed_flag == 1)
    {
      // 如果没有触发长按，就是短按
      if(key2_long_triggered == 0 && key2_debounce >= 10)
      {
        // 短按触发：切换温度模拟模式
        temp_simulate = !temp_simulate;
        if(temp_simulate)
        {
          printf("\r\n[测试] 温度模拟模式: ON (35C)\r\n");
        }
        else
        {
          printf("\r\n[测试] 温度模拟模式: OFF\r\n");
        }
        // 蜂鸣器反馈
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
        HAL_Delay(50);
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
      }
    }
    // 重置状态
    key2_debounce = 0;
    key2_pressed_flag = 0;
    key2_long_press_cnt = 0;
    key2_long_triggered = 0;
  }
}

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
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  // 启动ADC DMA双通道扫描
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf, ADC_CHANNEL_NUM);

  // 电机初始化（风扇插在电机转轴上，电机转=风扇转）
  printf("[DEBUG] 开始电机/风扇初始化...\r\n");
  Motor_Init();      // 配置电机引脚，启动TIM3_CH1 PWM
  Motor_Enable();    // 使能电机（退出待机模式）
  printf("[DEBUG] 电机/风扇初始化完成！\r\n");

  // OLED初始化
  printf("[DEBUG] 开始OLED初始化...\r\n");
  OLED_Init();
  printf("[DEBUG] OLED初始化完成！\r\n");
  OLED_Clear();
  OLED_ShowString(0, 0, "System Init...", FONT_8x16);
  HAL_Delay(500);
  printf("[DEBUG] OLED显示完成！\r\n");



  // 等待串口外设和DAP模块完全就绪
  HAL_Delay(500);
  
  printf("=========================================\r\n");
  printf("  3S Battery + Temp Monitor System\r\n");
  printf("=========================================\r\n");
  printf("  Volt Range: 9.0V ~ 12.6V\r\n");
  printf("  Temp Thresholds: %.1f/%.1f/%.1f C\r\n", TEMP_FAN_OFF, TEMP_FAN_ON, TEMP_FAN_HIGH);
  printf("  ADC: DMA Dual Channel (IN4=Bat/PA4, IN5=NTC/PA5)\r\n");
  printf("=========================================\r\n");
  
  OLED_Clear();
  OLED_ShowString(0, 0, "Volt+Temp+Fan", FONT_8x16);
  OLED_ShowString(2, 0, "System Ready!", FONT_6x8);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
while (1)
{
  // 按键扫描（每50ms一次）
  Key_Scan();

  if(system_running == 1)
  {
    // 温度模拟模式：每次循环都检查，立即生效
    static uint8_t last_temp_simulate = 0;
    if(temp_simulate)
    {
      temperature = 35.0f;
      // 刚进入模拟模式，立即启动风扇和电机
      if(last_temp_simulate == 0)
      {
        last_temp_simulate = 1;
        fan_status = 2;
        fan_speed = 100;
        // 电机转=风扇转，设置电机PWM=100%，正转
        Motor_Forward(100);
        printf("[Fan] High Speed %d%% (Simulate)\r\n", fan_speed);
      }
    }
    else
    {
      // 刚退出模拟模式，重置状态，让下一次采样重新判断
      if(last_temp_simulate == 1)
      {
        last_temp_simulate = 0;
        fan_status = 99;  // 特殊值，强制让下一次采样重新判断
        Motor_Stop();  // 停止电机=停止风扇
        printf("[Fan] Simulate OFF, resume normal control\r\n");
      }
    }
    
    // 系统运行中：执行电压监测
    // 控制采样频率，每20个循环（约1秒）采样一次
    adc_sample_cnt++;
    if(adc_sample_cnt >= 20)
    {
      adc_sample_cnt = 0;
      
      // 从DMA缓冲区读取数据（DMA一直在后台自动更新）
      uint16_t adc_volt = adc_dma_buf[0];  // IN4 - 电池电压（PA4）
      uint16_t adc_temp = adc_dma_buf[1];  // IN5 - NTC 温度（PA5）

      // 校准后的电压计算
      total_volt = (adc_volt * adc_ref / 4095.0f) * divider_k;

      // 非模拟模式才读取真实温度
      if(!temp_simulate)
      {
        /* ===== NTC 温度换算（B 值法），替换原先的线性简化公式 ===== */
        float ntc_volt = adc_temp * adc_ref / 4095.0f;

        if(adc_temp > 4040)
        {
          // 探头未插或断线：R3 把 PA5 拉到接近 3V3（>3.256 V）
          temperature = TEMP_NTC_OPEN;
        }
        else if(adc_temp < 15)
        {
          // 探头短路到地：PA5 接近 0 V
          temperature = TEMP_NTC_SHORT;
        }
        else
        {
          float r_ntc   = NTC_R_PULLUP * ntc_volt / (adc_ref - ntc_volt);
          float inv_T   = 1.0f / NTC_T0_K + logf(r_ntc / NTC_R0) / NTC_B;
          temperature   = 1.0f / inv_T - 273.15f;

          // 传感器物理范围限幅（-40 ~ 125 °C）
          if(temperature < -40.0f) temperature = -40.0f;
          if(temperature > 125.0f) temperature = 125.0f;
        }
      }

      // 串口打印（探头异常时明确标出，避免把 -99 误当成真实温度）
      if(TEMP_NTC_BAD(temperature))
      {
        printf("Battery: %.2fV | Temp: NTC-- (%s) | V-ADC: %d | T-ADC: %d\r\n",
               total_volt,
               (temperature < -98.5f) ? "open" : "short",
               adc_volt, adc_temp);
      }
      else
      {
        printf("Battery: %.2fV | Temp: %.1fC | V-ADC: %d | T-ADC: %d\r\n",
               total_volt, temperature, adc_volt, adc_temp);
      }

      // ========== 温控逻辑（迟滞控制）- 非模拟模式才执行 ==========
      // 风扇插在电机转轴上，电机转=风扇转
      if(!temp_simulate)
      {
        if(TEMP_NTC_BAD(temperature))
        {
          // 探头异常（未插/断线/短路）：没有可信温度依据，停止风扇
          if(fan_status != 0)
          {
            fan_status = 0;
            fan_speed = 0;
            Motor_Stop();
            printf("[Fan] Stop (NTC fault)\r\n");
          }
        }
        else if(temperature > TEMP_FAN_HIGH)
        {
          // 高温：全速
          if(fan_status != 2)
          {
            fan_status = 2;
            fan_speed = 100;
            Motor_Forward(100);  // 电机正转100% = 风扇全速
            printf("[Fan] High Speed %d%%\r\n", fan_speed);
          }
        }
        else if(temperature > TEMP_FAN_ON)
        {
          // 中温：低速
          if(fan_status != 1)
          {
            fan_status = 1;
            fan_speed = 50;
            Motor_Forward(50);   // 电机正转50% = 风扇低速
            printf("[Fan] Low Speed %d%%\r\n", fan_speed);
          }
        }
        else if(temperature < TEMP_FAN_OFF)
        {
          // 低温：停止
          if(fan_status != 0)
          {
            fan_status = 0;
            fan_speed = 0;
            Motor_Stop();        // 停止电机 = 停止风扇
            printf("[Fan] Stop\r\n");
          }
        }
      }

      // OLED显示更新
      char buf[32];
      OLED_Clear();
      
      // Line 0: Battery voltage
      OLED_ShowString(0, 0, "Bat:", FONT_6x8);
      snprintf(buf, sizeof(buf), "%.2fV", total_volt);
      OLED_ShowString(0, 24, buf, FONT_6x8);
      
      // Line 2: Temperature
      OLED_ShowString(2, 0, "Tmp:", FONT_6x8);
      if(TEMP_NTC_BAD(temperature))
      {
        snprintf(buf, sizeof(buf), "NTC--");   // 探头未接/断线/短路
      }
      else
      {
        snprintf(buf, sizeof(buf), "%.1fC", temperature);
      }
      OLED_ShowString(2, 24, buf, FONT_6x8);
      
      // Line 4: Fan status
      OLED_ShowString(4, 0, "Fan:", FONT_6x8);
      if(fan_status == 0)
      {
        OLED_ShowString(4, 24, "Stop", FONT_6x8);
      }
      else if(fan_status == 1)
      {
        OLED_ShowString(4, 24, "Low ", FONT_6x8);
        snprintf(buf, sizeof(buf), "%d%%", fan_speed);
        OLED_ShowString(4, 54, buf, FONT_6x8);
      }
      else if(fan_status == 2)
      {
        OLED_ShowString(4, 24, "High", FONT_6x8);
        snprintf(buf, sizeof(buf), "%d%%", fan_speed);
        OLED_ShowString(4, 54, buf, FONT_6x8);
      }
      else
      {
        // fan_status == 99：退出温度模拟后的重判死区，状态尚未确定
        // 原先这里走 else 会错误显示成 High 100%，改为占位符
        OLED_ShowString(4, 24, " -- ", FONT_6x8);
      }
      
      // Line 6: System status + 模拟模式指示
      if(temp_simulate)
      {
        OLED_ShowString(6, 0, "Sys:Run [SIM]", FONT_6x8);
      }
      else
      {
        OLED_ShowString(6, 0, "Sys:Running", FONT_6x8);
      }

      // 电压报警逻辑
      if(total_volt > 12.6f || total_volt < 9.0f)
      {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
        HAL_Delay(100);
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
        OLED_ShowString(7, 0, "!!! Volt Err !!!", FONT_6x8);
      }
      else
      {
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
      }
    }
    
    // 主循环延时（50ms），防止CPU占用100%
    HAL_Delay(50);
  }
  else
  {
    // 系统已停止
    adc_sample_cnt = 0;
    fan_status = 0;
    fan_speed = 0;
    Motor_Stop();  // 停止电机=停止风扇
    OLED_Clear();
    OLED_ShowString(2, 16, "Sys Stopped", FONT_6x8);
    OLED_ShowString(4, 8, "Power Reset", FONT_6x8);
    HAL_Delay(50);
  }

/* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
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
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
