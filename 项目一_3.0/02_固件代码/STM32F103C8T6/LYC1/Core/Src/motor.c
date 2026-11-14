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

/* 当前实际输出的占空比，用于软启动时判断从哪一档开始爬升 */
static uint8_t motor_speed_now = 0;

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

/* 电机正转（带软启动）
 *
 * 2026-11-07 增加软启动：电机启动瞬间电流是稳态的 5~8 倍，
 * 直接从 0 跳到上限会把 12.6V 一步压上去，冲击电流可能把 5V 轨拽塌，
 * 表现为 MCU 复位或电机"猛转一下"。现在改为每 25 ms 升 10%，
 * 0 → 40 约需 100 ms，属于人感觉不到的延时。
 * 减速不设斜坡：降低占空比不会产生冲击电流。
 */
void Motor_Forward(uint8_t speed)
{
    if(speed > MOTOR_SPEED_MAX) speed = MOTOR_SPEED_MAX;

    /* AIN1=1, AIN2=0 正转 */
    HAL_GPIO_WritePin(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN, GPIO_PIN_RESET);

    /* 100% 直通，不走软启动。
     * 2026-11-11：软启动的阶梯本身也是斩波，BLDC 风扇的驱动 IC 会在
     * 10%~90% 这 250 ms 里反复欠压复位，等爬到 100% 时可能已经进入锁死保护。
     * 满速必须一步到位，给纯直流让它完成启动流程。 */
    if(speed >= 100)
    {
        Motor_SetSpeed(speed);
        return;
    }

    /* 软启动：仅在升速时阶梯逼近目标值 */
    if(speed > motor_speed_now)
    {
        uint8_t s = motor_speed_now;
        while((uint8_t)(s + MOTOR_RAMP_INC) < speed)
        {
            s = (uint8_t)(s + MOTOR_RAMP_INC);
            Motor_SetSpeed(s);
            HAL_Delay(MOTOR_RAMP_STEP_MS);
        }
    }
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

/* 设置电机速度（PWM占空比 0 ~ MOTOR_SPEED_MAX）
 *
 * 2026-11-07 改动：
 *   1) 占空比上限由 100 改为 MOTOR_SPEED_MAX（当前值见 motor.h 宏定义）
 *   2) CCR 不再写死 ARR=999，改为从 htim3 读实际 ARR，
 *      这样以后改 PWM 频率（ARR 变化）不用再同步改这里
 */
void Motor_SetSpeed(uint8_t speed)
{
    if(speed > MOTOR_SPEED_MAX) speed = MOTOR_SPEED_MAX;

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim3);
    uint32_t ccr = (uint32_t)speed * arr / 100;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr);
    motor_speed_now = speed;
}

