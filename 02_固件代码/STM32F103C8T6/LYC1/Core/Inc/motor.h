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

/* 电机控制引脚定义 */
#define MOTOR_AIN1_PIN    GPIO_PIN_2
#define MOTOR_AIN1_PORT   GPIOA
#define MOTOR_AIN2_PIN    GPIO_PIN_3
#define MOTOR_AIN2_PORT   GPIOA
#define MOTOR_STBY_PIN    GPIO_PIN_8
#define MOTOR_STBY_PORT   GPIOA

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

