/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body (Umbrella System - 관리실 표시부)
 *
 * UART2: PC 디버그 콘솔 (printf 리다이렉트, 115200bps)
 * UART6: 블루투스 모듈 연결, 9600bps
 * I2C3 : I2C LCD (1602, 16x2)
 *
 * 3초마다 [YGY_BLT]UMB@ALL 요청을 블루투스로 전송해 라즈베리파이 블루투스
 * 중계 클라이언트로부터 전체 슬롯 상태를 받아 LCD에 표시한다.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
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

/* Private includes ------------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <clcd.h>
/* USER CODE END Includes */

/* Private define --------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */
#define ARR_CNT   6
#define CMD_SIZE  60
/* USER CODE END PD */

/* Private variables --------------------------------------------------------*/
I2C_HandleTypeDef hi2c3;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;

/* USER CODE BEGIN PV */
uint8_t rx2char;
volatile unsigned char rx2Flag = 0;
volatile char rx2Data[50];
volatile unsigned char btFlag = 0;
uint8_t btchar;
char btData[CMD_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_I2C3_Init(void);
/* USER CODE BEGIN PFP */
void bluetooth_Event(void);
/* USER CODE END PFP */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
  /* MCU Configuration----------------------------------------------------*/
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();
  MX_I2C3_Init();

  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart2, &rx2char, 1);
  HAL_UART_Receive_IT(&huart6, &btchar, 1);

  LCD_init(&hi2c3);
  LCD_writeStringXY(0, 0, "Umbrella System ");
  LCD_writeStringXY(1, 0, " Connecting...  ");
  printf("start\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 3초마다 자동 요청 */
    static uint32_t lastReq = 0;
    if (HAL_GetTick() - lastReq >= 3000) {
      lastReq = HAL_GetTick();
      char req[] = "[YGY_BLT]UMB@ALL\n";
      HAL_UART_Transmit(&huart6, (uint8_t*)req, strlen(req), 1000);
      printf("BT Req: %s", req);
    }

    if (rx2Flag) {
      printf("recv2 : %s\r\n", rx2Data);
      rx2Flag = 0;
    }

    if (btFlag) {
      btFlag = 0;
      bluetooth_Event();
    }

    HAL_Delay(500);
    /* USER CODE END WHILE */
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

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief I2C3 Initialization Function (I2C LCD 연결용)
 */
static void MX_I2C3_Init(void)
{
  hi2c3.Instance = I2C3;
  hi2c3.Init.ClockSpeed = 10000;
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c3.Init.OwnAddress1 = 0;
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c3.Init.OwnAddress2 = 0;
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief USART2 Initialization Function (PC 디버그 콘솔)
 */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief USART6 Initialization Function (블루투스 모듈)
 */
static void MX_USART6_UART_Init(void)
{
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 9600;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * @brief GPIO Initialization Function
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(DHT11_GPIO_Port, DHT11_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = DHT11_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* [UMB]1:EM@2:DR@3:EM  또는  [SLOT]1@DRYING@45@06-01 11:38  파싱 후 LCD 출력 */
void bluetooth_Event(void)
{
  int i = 0;
  char *pToken;
  char *pArray[ARR_CNT] = {0};
  char  recvBuf[CMD_SIZE] = {0};
  char  lcdLine1[17] = {0};
  char  lcdLine2[17] = {0};

  strcpy(recvBuf, btData);
  printf("BT Recv: %s\r\n", btData);

  pToken = strtok(recvBuf, "[@]");
  while (pToken != NULL) {
    pArray[i] = pToken;
    if (++i >= ARR_CNT) break;
    pToken = strtok(NULL, "[@]");
  }

  if (pArray[0] == NULL) return;
  if (pArray[1] != NULL && !strncmp(pArray[1], " New", 4)) return;
  if (pArray[1] != NULL && !strncmp(pArray[1], " Alr", 4)) return;

  /* [UMB]1:EM@2:DR@3:EM */
  if (!strcmp(pArray[0], "UMB")) {
    if (pArray[1] == NULL || pArray[2] == NULL || pArray[3] == NULL) return;

    char s1[3]={0}, s2[3]={0}, s3[3]={0};
    if (strlen(pArray[1]) >= 4) { s1[0]=pArray[1][2]; s1[1]=pArray[1][3]; }
    if (strlen(pArray[2]) >= 4) { s2[0]=pArray[2][2]; s2[1]=pArray[2][3]; }
    if (strlen(pArray[3]) >= 4) { s3[0]=pArray[3][2]; s3[1]=pArray[3][3]; }

    /* Line1: "1:EM 2:DR 3:EM  " 16자 */
    snprintf(lcdLine1, 17, "1:%s 2:%s 3:%s  ", s1, s2, s3);

    /* Line2: EM=빈칸 US=사용 DR=건조 DN=완료 TF=도난 */
    snprintf(lcdLine2, 17, "%-5s %-5s %-4s",
             !strcmp(s1,"EM")?"Empty":!strcmp(s1,"US")?"Using":
             !strcmp(s1,"DR")?"Dry":!strcmp(s1,"DN")?"Done":"Theft",
             !strcmp(s2,"EM")?"Empty":!strcmp(s2,"US")?"Using":
             !strcmp(s2,"DR")?"Dry":!strcmp(s2,"DN")?"Done":"Theft",
             !strcmp(s3,"EM")?"Emp":!strcmp(s3,"US")?"Use":
             !strcmp(s3,"DR")?"Dry":!strcmp(s3,"DN")?"Don":"Thf");

    LCD_writeStringXY(0, 0, lcdLine1);
    LCD_writeStringXY(1, 0, lcdLine2);
    printf("LCD1:%s\r\nLCD2:%s\r\n", lcdLine1, lcdLine2);
  }
  /* [SLOT]1@DRYING@45@06-01 11:38 */
  else if (!strcmp(pArray[0], "SLOT")) {
    if (pArray[1] == NULL || pArray[2] == NULL) return;

    char dry[5]="0", dt[13]="--";
    if (pArray[3] != NULL) strncpy(dry, pArray[3], 4);
    if (pArray[4] != NULL) { strncpy(dt, pArray[4], 12); dt[12]='\0'; }

    snprintf(lcdLine1, 17, "SL%s:%-8s%s%%", pArray[1], pArray[2], dry);
    snprintf(lcdLine2, 17, "%-16s", dt);

    LCD_writeStringXY(0, 0, lcdLine1);
    LCD_writeStringXY(1, 0, lcdLine2);
    printf("LCD1:%s\r\nLCD2:%s\r\n", lcdLine1, lcdLine2);
  }
}

/**
 * @brief  Retargets the C library printf function to the USART.
 */
PUTCHAR_PROTOTYPE
{
  HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    static int i = 0;
    rx2Data[i] = rx2char;
    if ((rx2Data[i] == '\r') || (rx2Data[i] == '\n'))
    {
      rx2Data[i] = '\0';
      rx2Flag = 1;
      i = 0;
    }
    else
    {
      i++;
    }
    HAL_UART_Receive_IT(&huart2, &rx2char, 1);
  }
  if (huart->Instance == USART6)
  {
    static int i = 0;
    btData[i] = btchar;
    if ((btData[i] == '\n') || btData[i] == '\r')
    {
      btData[i] = '\0';
      btFlag = 1;
      i = 0;
    }
    else
    {
      if (i < CMD_SIZE - 2) i++;
    }
    HAL_UART_Receive_IT(&huart6, &btchar, 1);
  }
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
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
