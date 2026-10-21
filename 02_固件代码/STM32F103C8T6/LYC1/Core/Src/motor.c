/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    motor.c
  * @brief   TB6612电机驱动
  *          控制TB6612驱动130直流电机
  ******************************************************************************
  */
/* USER CODE END Header */

#include "motor.h"

/* 电机初始化 */
void Motor_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* GPIO时钟使能 */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* 配置控制引脚 AIN1 PA2, AIN2 PA3, STBY PA8 */
    GPIO_InitStruct.Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_STBY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 初始状态：待机禁用 */
    HAL_GPIO_WritePin(MOTOR_STBY_PORT, MOTOR_STBY_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);

    /* 启动PWM */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    Motor_SetSpeed(0);
}

/* 使能电机（退出待机模式） */
void Motor_Enable(void)
{
    HAL_GPIO_WritePin(MOTOR_STBY_PORT, MOTOR_STBY_PIN, GPIO_PIN_SET);
}

/* 禁用电机（进入待机模式） */
void Motor_Disable(void)
{
    HAL_GPIO_WritePin(MOTOR_STBY_PORT, MOTOR_STBY_PIN, GPIO_PIN_RESET);
}

/* 电机停止（刹车） */
void Motor_Stop(void)
{
    HAL_GPIO_WritePin(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    Motor_SetSpeed(0);
}

/* 电机正转 */
void Motor_Forward(uint8_t speed)
{
    /* AIN1=1, AIN2=0 正转 */
    HAL_GPIO_WritePin(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);
    Motor_SetSpeed(speed);
}

/* 电机反转 */
void Motor_Backward(uint8_t speed)
{
    /* AIN1=0, AIN2=1 反转 */
    HAL_GPIO_WritePin(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, GPIO_PIN_SET);
    Motor_SetSpeed(speed);
}

/* 设置电机速度（PWM占空比 0-100） */
void Motor_SetSpeed(uint8_t speed)
{
    if(speed > 100) speed = 100;
    
    /* 计算CCR值，ARR=999 */
    uint32_t ccr = (uint32_t)speed * 999 / 100;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr);
}

