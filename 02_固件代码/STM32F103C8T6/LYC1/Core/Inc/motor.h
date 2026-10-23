/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    motor.h
  * @brief   TB6612电机驱动头文件
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MOTOR_H
#define __MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "tim.h"

/* 电机控制引脚定义
 * 2026-10-23 PCB 适配：原 PA2/PA3/PA8 → PB12/PB13/PB14
 *   PA2(AIN1) → PB12   U1-2
 *   PA3(AIN2) → PB13   U2-10
 *   PA8(STBY) → PB14   U2-9
 * PWMA 仍在 PA6（TIM3_CH1），未变动
 */
#define MOTOR_AIN1_PIN    GPIO_PIN_12
#define MOTOR_AIN1_PORT   GPIOB
#define MOTOR_AIN2_PIN    GPIO_PIN_13
#define MOTOR_AIN2_PORT   GPIOB
#define MOTOR_STBY_PIN    GPIO_PIN_14
#define MOTOR_STBY_PORT   GPIOB

/* 函数声明 */
void Motor_Init(void);
void Motor_Enable(void);
void Motor_Disable(void);
void Motor_Stop(void);
void Motor_Forward(uint8_t speed);
void Motor_Backward(uint8_t speed);
void Motor_SetSpeed(uint8_t speed);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H */

