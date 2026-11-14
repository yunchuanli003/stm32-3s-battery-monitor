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

/* ===== 负载配置（2026-11-10 换 12 V 风扇）=====
 *
 * 当前负载：4010 风扇 12 V / 8000 rpm / 双滚珠 / 2 线
 *
 * MOTOR_SPEED_MAX（高速档）：
 *   80% × 12.6 V ≈ 10.1 V   ← 12 V 风扇额定区间，留 20% 余量避免长期满压
 *
 * MOTOR_SPEED_LOW（低速档）：
 *   60% × 12.6 V ≈ 7.6 V
 *   ⚠ 不能取高速档的一半（40% = 5.0 V）。8000 rpm 这类高速 4010 的
 *     启动电压普遍在 6~7 V，5.0 V 只会"嗡嗡"抖而不转，容易被误判成坏了。
 *     低速档必须是独立值，不是 MOTOR_SPEED_MAX / 2。
 *
 * 【历史】2026-11-07 曾用 MOTOR_SPEED_MAX = 20~40 配 5 V 级 130 电机，
 *   那是临时验证值。20% × 12.6 V = 2.5 V，接 12 V 风扇后完全起不动，
 *   所以 11-10 换扇时必须同步改回 80，否则高温模拟下风扇不转。
 *
 * 软启动：目标占空比高于当前值时，按 MOTOR_RAMP_INC 分步逼近，
 *   每步等待 MOTOR_RAMP_STEP_MS。风扇启动瞬间电流是稳态的 5~8 倍，
 *   阶梯升速可以把这个冲击摊平，避免电压被拽塌导致 MCU 复位。
 */
/* 2026-11-13 负载改为 12 V 直流电机，上限 80% → 30%
 *
 * 实测（80% 档）：J2 = 9.66 V（= 80% × 12.07 V 的 PWM 平均值）、
 *                 电流 0.22 A，电机稳定运转，不堵转
 *   → 板子、TB6612、M1、J2、走线全部正常，此前一切接触/虚焊怀疑均可排除
 *
 * 30% 档对应 30% × 12.07 V ≈ 3.62 V，实测可稳定起转（2026-11-13 23:40）。
 *
 * 2026-11-14 档位重设为 低速 40% / 高速 70%（拉开差距 + 保证低电量可靠起转）
 *
 *   档位链：< 26 ℃ 停 / 26~28 ℃ 保持（迟滞死区）/ 28~32 ℃ 低速 40% / > 32 ℃ 高速 70%
 *
 *   为什么低速不是 20% 或 30%：
 *     实测 30% 在电池 12.07 V 时输出 3.62 V，"刚好"能转 —— 说明 3.62 V 已贴近
 *     本电机的起转阈值。3S 电池正常工作区间为 11~12.6 V，电池过半后
 *     30% × 11.0 V = 3.30 V 大概率抖而不转，低速档会随电量衰减而失效。
 *     12 V 电机的最低可靠启动电压约为额定的 33~38%（4~4.5 V），这是物理下限。
 *     取 40%（12.07 V 下 4.83 V）可在电池跌到 9 V 时仍输出 3.6 V，覆盖全区间。
 *
 *   为什么高速是 70% 而不是 60%：
 *     低速提到 40% 后，若高速仍取 60%，比值仅 1.5 倍，档位差异偏弱。
 *     70% 对应 8.45 V（满电 8.82 V），比值 1.75 倍：
 *       转速 +75%、风速 +75%、噪声 +12 dB（ΔSPL = 50·log₁₀(1.75)）——
 *       人耳"响度翻倍"约 10 dB，12 dB 属于明显可辨。
 *     同时 70% < 80% 红线，保留满电过冲余量。
 *
 *   电流估算：80% 档实测 0.22 A，风阻 ∝ 转速²，70% 档约 0.18~0.20 A，
 *     远低于 TB6612 A 通道 1.2 A 连续能力，无过热风险。
 */
#define MOTOR_SPEED_MAX      70
#define MOTOR_SPEED_LOW      40
#define MOTOR_RAMP_INC       10
#define MOTOR_RAMP_STEP_MS   25

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

