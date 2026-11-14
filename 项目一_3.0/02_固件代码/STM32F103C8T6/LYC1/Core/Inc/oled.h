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

/* ===== 16x16 汉字索引（2026-11-14 新增）=====
 * 本工程只带 38 个汉字的点阵（见 oled_cn_font.h），不做整字库，
 * 因为 128x64 屏上的固定文案就那几个词，整字库（GB2312 一级字 3755 个
 * × 32 字节 = 117 KB）根本塞不进 64 KB Flash。
 *
 * 【顺序绝对不能乱】这里的排列必须和 oled_cn_font.h 里 g_cn16x16[]
 * 的排列逐项对应。加字/删字的正确做法是改生成脚本里的 CN_STR，
 * 重新生成字库，再把这里同步成一样的顺序 —— 只改一边会显示成别的字。
 */
typedef enum
{
    CN_DIAN = 0,  /*  0 电 */
    CN_CHI,       /*  1 池 */
    CN_WEN,       /*  2 温 */
    CN_DU,        /*  3 度 */
    CN_FENG,      /*  4 风 */
    CN_SHAN,      /*  5 扇 */
    CN_XI,        /*  6 系 */
    CN_TONG,      /*  7 统 */
    CN_ZHENG,     /*  8 正 */
    CN_CHANG,     /*  9 常 */
    CN_GUO,       /* 10 过 */
    CN_YA,        /* 11 压 */
    CN_QIAN,      /* 12 欠 */
    CN_GAO,       /* 13 高 */
    CN_DI,        /* 14 低 */
    CN_SU,        /* 15 速 */
    CN_TING,      /* 16 停 */
    CN_ZHI,       /* 17 止 */
    CN_BAO,       /* 18 报 */
    CN_JING,      /* 19 警 */
    CN_MO,        /* 20 模 */
    CN_NI,        /* 21 拟 */
    CN_WU,        /* 22 无 */
    CN_XIAN,      /* 23 线 */
    CN_TONG_LINK, /* 24 通（"统" 已占用 CN_TONG，这里用 _LINK 区分）*/
    CN_FA,        /* 25 发 */
    CN_DUAN,      /* 26 断 */
    CN_YI,        /* 27 已 */
    CN_JI,        /* 28 机 */
    CN_CHU,       /* 29 初 */
    CN_SHI,       /* 30 始 */
    CN_HUA,       /* 31 化 */
    CN_QI,        /* 32 启 */
    CN_DONG,      /* 33 动 */
    CN_JIAN,      /* 34 监 */
    CN_CE,        /* 35 测 */
    CN_QING,      /* 36 请 */
    CN_CHONG      /* 37 重 */
} CnChar_t;

/* 函数声明
 *
 * 【重要】2026-11-14 起改为"显存 + 差异刷新"结构：
 *   OLED_Clear() / OLED_ShowString() / OLED_ShowCN() 等**只画在 RAM 里**，
 *   一个字节 I2C 都不会发。必须在一屏内容全部画完之后调用一次
 *   OLED_Refresh()，才会把与屏幕不同的部分真正刷上去。
 *   漏掉 OLED_Refresh() 的现象是：屏幕停在旧画面不动。
 */
void OLED_Init(void);
void OLED_Clear(void);          /* 只清 RAM 显存 */
void OLED_Refresh(void);        /* 差异刷新：只发变化的列，没变不发 */
void OLED_SetPos(uint8_t y, uint8_t x);   /* 只移动 RAM 光标 */
void OLED_ShowChar(uint8_t y, uint8_t x, char ch, uint8_t font_size);
void OLED_ShowString(uint8_t y, uint8_t x, char *str, uint8_t font_size);
void OLED_ShowNum(uint8_t y, uint8_t x, float num, uint8_t len, uint8_t font_size);

/* 显示一个 16x16 汉字：占 y、y+1 两页，宽 16 px。
 * y 必须是偶数页（0/2/4/6），否则下半截会压到下一行文字上。 */
void OLED_ShowCN(uint8_t y, uint8_t x, CnChar_t ch);

/* 连续显示 n 个汉字，每字 16 px 宽，超出右边界自动截断。
 * 用法：OLED_ShowCNStr(0, 0, 2, CN_DIAN, CN_CHI);   // 显示"电池" */
void OLED_ShowCNStr(uint8_t y, uint8_t x, uint8_t n, ...);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */

