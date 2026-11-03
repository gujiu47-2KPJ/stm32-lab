/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 多传感器采集系统主程序
  * @note            外设列表:
  *                  - OLED SSD1306 (I2C1, 0x3C)   : 分页显示传感器数据
  *                  - MPU6xxx x2  (I2C1, 0x68/0x69): 6 轴姿态传感器
  *                  - W25Qxx Flash (SPI1)          : 断电保存页码和参数
  *                  - 旋转编码器   (TIM2 ENCODER)   : 翻页操作
  *                  - JDY-3x 蓝牙 (UART1, 9600)     : 数据透传到手机
  *                  - PC13 LED     (GPIO)           : 心跳指示灯
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "ssd1306.h"    /* OLED 128x64 I2C 显示驱动 */
#include "encode.h"     /* 旋转编码器驱动 (TIM2) */
#include "w25qxx.h"     /* W25Qxx SPI Flash 驱动 */
#include "mpu6050.h"    /* MPU6xxx 六轴传感器驱动 */
#include "sgp30.h"      /* SGP30 空气质量传感器驱动 */
#include "warm_damp.h"  /* AHT20+BMP280 温湿压一体化传感器驱动 */
#include "jdy31.h"      /* JDY-3x 蓝牙模块驱动 */
#include <stdio.h>      /* sprintf 格式化输出 */

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

/** @brief I2C1 句柄: OLED + MPUx2 + SGP30 + AHT20/BMP280 共享总线 */
I2C_HandleTypeDef hi2c1;

/** @brief SPI1 句柄: W25Qxx Flash */
SPI_HandleTypeDef hspi1;

/** @brief TIM2 句柄: 编码器模式 (CH1=PA0, CH2=PA1) */
TIM_HandleTypeDef htim2;

/** @brief UART1 句柄: JDY-3x 蓝牙模块 (9600 8N1) */
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int ax_m,ay_m,az_m,gx_m,gz_m,gy_m; /* MPU 物理单位换算值 (x1000 整数) */
/**
 * @brief  将 MPU6xxx 原始 ADC 值换算为物理单位 (整数放大)
 * @param  ax 加速度 X 轴原始值 (LSB)
 * @param  ay 加速度 Y 轴原始值 (LSB)
 * @param  az 加速度 Z 轴原始值 (LSB)
 * @param  gx 陀螺仪 X 轴原始值 (LSB)
 * @param  gy 陀螺仪 Y 轴原始值 (LSB)
 * @param  gz 陀螺仪 Z 轴原始值 (LSB)
 * @note   换算公式 (x1000 避免浮点):
 *         加速度: 原始值 * 1000 / 16384  (g*1000, 量程 ±2g)
 *         陀螺仪: 原始值 * 1000 / 131    (°/s*1000, 量程 ±250°/s)
 *         结果存入全局变量 ax_m/ay_m/az_m/gx_m/gy_m/gz_m
 */
void accum(int ax,int ay,int az,int gx,int gy,int gz){
      ax_m = (int32_t)ax * 1000 / 16384;
      ay_m = (int32_t)ay * 1000 / 16384;
      az_m = (int32_t)az * 1000 / 16384;      // 加速度灵敏度 16384

      gx_m = (int32_t)gx * 1000 / 131;
      gy_m = (int32_t)gy * 1000 / 131;
      gz_m = (int32_t)gz * 1000 / 131;        // 陀螺仪灵敏度 131
  }

/**
 * @brief  I2C 总线扫描: 检测 1~126 地址范围的所有设备并显示到 OLED
 * @note   用于调试时确认各传感器 I2C 地址是否正确
 *         7 位地址左移 1 位后通过 HAL_I2C_IsDeviceReady 握手
 */
void I2C_Scan(void)
{
    uint8_t a = 0;
    uint8_t x = 0, y = 2;
    OLED_Clear();
    OLED_ShowString(0, 0, "I2C scan:");

    for(a = 1; a < 127; a++)
    {
        if(HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a<<1), 1, 20) == HAL_OK)
        {
            char buf[12];
            sprintf(buf, "0x%02X", a);
            OLED_ShowString(x, y+0, buf);
            x += 42;
            if(x > 100) { x = 0; y++; }         /* 自动换行 */
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  程序入口, 初始化外设后进入数据采集循环
  * @retval int (永远不会返回)
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
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /** @brief 当前 OLED 显示页码 (0~3), 断电前保存到 Flash 地址 1 */
  static uint8_t page = 0;

  /* 从 Flash 恢复上次断电时的页码 */
  W25QXX_ReadByte(1,&page);//读取储存的计数值

  if(page>3) page = 0;      /* 容错: 非法页码归零 */

  HAL_Delay(500);           /* 等待各模块上电稳定 */

  OLED_Init();
	OLED_Clear();
  JDY31_SendAT("AT");   // 测试模块是否响应（可在串口助手看回显）
	W25QXX_Init();            /* SPI Flash 初始化 (读 JEDEC ID) */
	Encode_Init();            /* 编码器模式启动 */

  /* 检测 I2C 总线上的 MPU 模块 (0x68 AD0=0 / 0x69 AD0=1) */
  uint8_t ok1 = MPU6050_Detect(0x68);
  uint8_t ok2 = MPU6050_Detect(0x69);

  if(ok1) MPU6050_Init(0x68);    /* 模块存在则唤醒 */
  if(ok2) MPU6050_Init(0x69);    /* 模块存在则唤醒 */

  /* 检测并初始化 SGP30 空气质量传感器 */
  uint8_t sgp30_ok = SGP30_Detect();
  if(sgp30_ok) SGP30_Init();

  /* 检测并初始化 AHT20+BMP280 温湿压传感器 */
  uint8_t wd_ok = WarmDamp_Detect();
  if(wd_ok) WarmDamp_Init();

	static uint8_t last_val = 0xFF;

	uint8_t saved = 0;

	/* 从 Flash 地址 0 恢复编码器上次保存的累计值 */
	W25QXX_ReadByte(0,&saved);

	Encode_SetValue(saved);       /* 恢复编码器初始值 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  char line[22];                  /* OLED 行缓冲区 */

  int16_t ax, ay, az, gx, gy, gz;/* MPU 6 轴原始 ADC 值 */

  float temp, hum, press;          /* AHT20+BMP280: 温度/湿度/气压 */
  uint16_t eco2, tvoc;             /* SGP30: eCO2/TVOC 浓度 */

  while (1)                        /* ---- 主循环: 周期性采集与显示 ---- */
  {
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);//翻转电平指示代码进入循环正常工作

      /* 检测编码器是否旋动 (翻页触发) */
      if(Encode_Moved()){
        page = Encode_ReadDelta();
      }
      switch(page){
          case 0:
                /* OLED 第 0 行: 显示当前页码和模块状态 */
                sprintf(line, "MP1:%s",
                        ok1 ? "OK" : "FAIL");//使用sprintf和数组打印初始化信息
                OLED_ShowString(0, 0, line);

                /* 若模块在线, 读取 6 轴数据; 否则跳过本次采集 */
                if(ok1){
                  MPU6050_ReadAccel(0x68, &ax, &ay, &az, &gx, &gy, &gz);
                }else break;

                accum(ax,ay,az,gx,gy,gz);//处理mpu数据

                char bt_buf[64];
                sprintf(bt_buf, "AX:%d AY:%d AZ:%d GX:%d GY:%d GZ:%d\r\n",
                  ax_m, ay_m, az_m, gx_m, gy_m, gz_m);
                JDY31_SendString(bt_buf);/* 通过蓝牙透传发送传感器数据到手机 */

                /* OLED 第 1~3 行: 显示加速度和陀螺仪数据 */
                sprintf(line, "AX:%-6d GX:%-6d", ax_m, gx_m);
                OLED_ShowString(0, 1, line);

                sprintf(line, "AY:%-6d GY:%-6d", ay_m, gy_m);
                OLED_ShowString(0, 2, line);

                sprintf(line, "AZ:%-6d GZ:%-6d", az_m, gz_m);
                OLED_ShowString(0, 3, line);
                break;
          case 1:
                /* AHT20+BMP280 温湿压页面 (第4页) */
                if(wd_ok){
                  WarmDamp_Read(&temp, &hum, &press);   /* 采集温湿压 */
                  sprintf(line, "T:%.1fC H:%.1f%%", temp, hum);
                  OLED_ShowString(0, 0, line);
                  sprintf(line, "P:%.0fPa", press);
                  OLED_ShowString(0, 1, line);
                  sprintf(line, "STATUS:OK");
                  OLED_ShowString(0, 2, line);
                }else{
                  OLED_ShowString(0, 0, "Sensor: FAIL");
                }
                break;
          case 2:
                /* SGP30 空气质量页面 (第3页) */
                if(sgp30_ok){
                  SGP30_Read(&eco2, &tvoc);
                  sprintf(line, "eCO2:%uppm", eco2);
                  OLED_ShowString(0, 0, line);
                  sprintf(line, "TVOC:%uppb", tvoc);
                  OLED_ShowString(0, 1, line);
                }else{
                  OLED_ShowString(0, 0, "SGP30: FAIL");
                }
                break;
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

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