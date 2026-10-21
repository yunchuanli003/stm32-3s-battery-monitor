/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    oled.h
  * @brief   OLED SSD1306 I2C驱动头文件
  *          128*64点阵，SSD1306驱动，I2C通信
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "i2c.h"

/* OLED I2C地址和配置 */
#define OLED_I2C_ADDR    (0x3C << 1)  // OLED I2C从机地址
#define OLED_TIMEOUT     100           // 超时时间(ms)

/* 写命令/数据标志 */
#define OLED_CMD         0x00
#define OLED_DATA        0x40

/* 字体大小 */
#define FONT_6x8         0
#define FONT_8x16        1

/* 函数声明 */
void OLED_Init(void);
void OLED_Clear(void);
void OLED_SetPos(uint8_t y, uint8_t x);
void OLED_ShowChar(uint8_t y, uint8_t x, char ch, uint8_t font_size);
void OLED_ShowString(uint8_t y, uint8_t x, char *str, uint8_t font_size);
void OLED_ShowNum(uint8_t y, uint8_t x, float num, uint8_t len, uint8_t font_size);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */

