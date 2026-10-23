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

    /* GPIO时钟使能（2026-10-23 PCB 适配：GPIOA → GPIOB） */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 配置控制引脚 AIN1 PB12, AIN2 PB13, STBY PB14 */
    GPIO_InitStruct.Pin = MOTOR_AIN1_PIN | MOTOR_AIN2_PIN | MOTOR_STBY_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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

/* 电机反转 —— 已禁用，调用后等同 Motor_Stop()
 *
 * 原因（PCB 设计输入 v1.8 §8.2「D2 方向说明」）：
 *   D2 跨接在 AO1/AO2 之间，用于吸收电机换向反电动势。
 *   该文档按「单向续流二极管」描述，若是单向的，反转时 D2 正向导通
 *   会把 AO1-AO2 直接短路，TB6612 输出级将过流。
 *
 * 已知矛盾：同一份文档 §2.2 元件清单登记 D2 = SMBJ15CA（双向 TVS），
 *   双向 TVS 正反转都能钳位、不会短路；但 §8.2 又写成 SS34（单向肖特基）。
 *   实物未核对之前，一律按最坏情况（单向）处理。
 *
 * 本项目风扇只需要单方向吹风，反转本身没有使用场景，禁用无功能损失。
 * 若日后确认 D2 确为双向 TVS 且需要反转，删除本函数内的 Motor_Stop() 调用
 * 并恢复下面注释掉的三行即可。
 */
void Motor_Backward(uint8_t speed)
{
    (void)speed;
    Motor_Stop();
    /* 原实现（禁用中）：
    HAL_GPIO_WritePin(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, GPIO_PIN_SET);
    Motor_SetSpeed(speed);
    */
}

/* 设置电机速度（PWM占空比 0-100） */
void Motor_SetSpeed(uint8_t speed)
{
    if(speed > 100) speed = 100;
    
    /* 计算CCR值，ARR=999 */
    uint32_t ccr = (uint32_t)speed * 999 / 100;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr);
}

