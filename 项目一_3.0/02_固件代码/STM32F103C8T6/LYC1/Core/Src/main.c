/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "adc.h"
#include "usart.h"
#include "gpio.h"
#include "dma.h"
#include "i2c.h"
#include "tim.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */


#include "stdio.h"  // 用于串口打印
#include "string.h" // strlen（右对齐算法要按实际字符数算起点）
#include "math.h"   // NTC B值法换算需要 logf()
#include "oled.h"
#include "motor.h"


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

/* USER CODE BEGIN PV */

// DMA缓冲区 - 双通道ADC数据
// 2026-10-23 PCB 适配：通道号由 IN0/IN1 改为 IN4/IN5，缓冲索引顺序不变
#define ADC_CHANNEL_NUM 2
uint16_t adc_dma_buf[ADC_CHANNEL_NUM];  // [0]=IN4(PA4)电池电压, [1]=IN5(PA5)NTC温度

uint16_t adc_value;
float total_volt;
// 校准后的最终参数
const float adc_ref = 3.3f;
// 分压比：R1 = 51kΩ(±1%) 上臂，R2 = 10kΩ(±1%) 下臂
//   理论值 (51+10)/10 = 6.10；6.21 是上一版面包板实测校准值。
//   新板电阻批次未必一致，首次上电要用万用表实测电池端电压与串口打印值比对，
//   按 实测电压 / 打印电压 × 6.21 修正这个系数。
const float divider_k = 6.21f;

/* ===== D1 正向压降补偿（2026-11-10 增加）=====
 * 分压点取在 D1(SS34) 之后（网络 VBAT_F），所以 ADC 量到的是
 *     V_adc_net = V_J1 − Vf_D1
 * 而不是电池端子电压。
 *
 * 实测（2026-11-10，电池供电、风扇未接）：
 *     J1（万用表） = 12.43 V
 *     OLED 显示    = 12.09 V
 *     差值         = 0.34 V  ← 正好是 SS34 在约 0.1 A 下的正向压降
 *
 * SS34 的 Vf 在 0.02~0.5 A 区间为 0.30~0.45 V，取 0.34 V 覆盖板子常态负载
 * （核心板 50 mA + OLED 20 mA + LDO 静态 ≈ 0.1 A）。风扇转起来后 VM 多
 * 0.2 A，Vf 升到约 0.42 V，补偿会欠 0.08 V，对 3S 电池的阈值判断无影响。
 *
 * 注：这是系统性固定偏差，不是分压比错误。divider_k = 6.21 本身仍然有效
 *     （它标定的是「VBAT_F → ADC 读数」这一段链路）。
 */
#define DIODE_VF_COMP   0.34f

/* ===== NTC 温度换算参数（B 值法）=====
 * 电路：R3 = 10 kΩ(±1%) 上拉至 +3V3，防水探头经 J7 接 GND，PA5 测中点。
 *   V_adc = 3V3 × R_ntc / (R3 + R_ntc)
 *   R_ntc = R3 × V_adc / (3V3 − V_adc)
 *   1/T  = 1/T0 + ln(R_ntc / R0) / B
 */
#define NTC_R_PULLUP   10000.0f   // R3 上拉阻值
#define NTC_R0         10000.0f   // 25 °C 标称阻值
#define NTC_B          3950.0f    // B25/50 值，下单时必须核对是 3950 不是 3435
#define NTC_T0_K       298.15f    // 25 °C 对应的绝对温度

// 探头异常约定值（负温度指数，不会与真实工况混淆）
#define TEMP_NTC_OPEN   (-99.0f)  // J7 未插探头 / 探头断线
#define TEMP_NTC_SHORT  (-98.0f)  // 探头短路到地
#define TEMP_NTC_BAD(t) ((t) < -90.0f)

// 温度相关变量
float temperature = 25.0f;

// 温度阈值 - 迟滞控制
#define TEMP_FAN_ON    28.0f   // 启动风扇温度
#define TEMP_FAN_OFF   26.0f   // 停止风扇温度
#define TEMP_FAN_HIGH  32.0f   // 高速风扇温度

// 电压报警阈值（3S 锂电：满电 12.6 V，过放保护点 9.0 V）
#define VOLT_OVP       12.6f   // 过压
#define VOLT_UVP        9.0f   // 欠压

// 风扇状态
uint8_t fan_status = 0;  // 0=停止, 1=低速, 2=高速
uint8_t fan_speed = 0;   // 0-100

// 系统运行状态
uint8_t system_running = 1;

/* ===== 报警类型（2026-11-14 细分）=====
 * 原来只返回 1/2（电压/温度），屏幕上没法区分是过压还是欠压。
 * 改中文后报警信息要并进第 4 行的状态词里（"过压报警"/"欠压报警"），
 * 所以这里把电压报警拆成两种。 */
typedef enum
{
    ALARM_NONE = 0,   /* 一切正常 */
    ALARM_OVP,        /* 过压：V > 12.6 V */
    ALARM_UVP,        /* 欠压：V < 9.0 V  */
    ALARM_TEMP        /* 高温：风扇已进低速/高速档 */
} Alarm_t;

/* ===== WiFi 链路心跳（2026-11-14 新增）=====
 * ESP8266 每收到一帧回一个 'P'，USART3 接收中断刷新 wifi_rx_tick。
 * 6 秒没收到（本机发送周期 1 秒，容得下连丢 5 帧）就认为对端不在。
 *
 * 为什么非要这一步：
 *   HAL_UART_Transmit 返回 HAL_OK 只代表数据进了移位寄存器，
 *   不代表对面有人听。没有应答的话，屏幕上的 WiFi 状态就恒为"正常"，
 *   拔掉 ESP8266 也不会变——那种指示还不如没有。
 *
 * wifi_rx_tick 中断里写、主循环里读，32 位对齐变量在 Cortex-M3 上是原子读写。
 */
#define WIFI_LINK_TIMEOUT  6000
volatile uint32_t wifi_rx_tick = 0;
uint8_t wifi_rx_byte = 0;

/* ===== 按键状态机（2026-11-14 二次重构：改用真实时间基准）=====
 * 原实现把 K2 的消抖/短按/长按逻辑内联写在 Key_Scan 里，
 * 新增 K1 时如果复制一份，两处时序迟早会走偏。抽出成参数化状态机：
 *   每个主循环调一次，喂入当前电平，返回"无 / 短按 / 长按"三种事件。
 *
 * 【为什么必须改成毫秒基准】—— 2026-11-14 实测 bug 的根因
 * 上一版用"循环圈数"当时间，注释按每圈 50 ms 折算，可主循环一圈实际是：
 *     HAL_Delay(50) + OLED_Clear(8 页 × 129 B) + 四行文字(约 800 B)
 *   ≈ 50 ms + 44 ms = 94 ms        （I2C 400 kHz，实测字节数按 oled.c 算）
 * 于是所有时间被放大 1.9 倍：
 *     消抖 10 格 → 940 ms（设计 500 ms）
 *     长按 60 格 → 5.6 s，再加消抖共 6.5 s（设计 3 s）
 * K1 的短按窗口 press_cnt ≤ 20 实际只剩 0.94 s ~ 2.8 s 这一条窄缝，
 * 人手习惯性的 2~3 秒按压正好落在 1~3 s 死区里 → 完全没反应；
 * 而 K2 短按不设上限，同样按 2~3 秒一律算短按 → 看起来"K2 完全正常"。
 * 要触发 K1 只能按住 6.5 s 以上，就是"长按很久才响应"。
 *
 * 现在直接用 HAL_GetTick() 算毫秒，主循环快慢、OLED 刷多久都不再影响手感。
 * 另加 armed 标志：必须先在某个循环里读到"松开"电平，才允许认定下一次按下。
 * 万一以后引脚极性配错、上电瞬间被拉高，也只会一直不响应，不会凭空连发。
 *
 * 【2026-11-14 晚补】上面那个 94 ms 已经是历史值。oled.c 改成"显存 + 差异刷新"
 * 之后，稳态刷一帧只要 1 ms 左右（数值不变时 0 字节），主循环回到约 55 ms。
 * 所以任何"一圈 ≈ 多少毫秒"的注释都不要再当成计算依据 —— 一律用 HAL_GetTick()。
 */
#define KEY_DEBOUNCE_MS    30      // 消抖窗口：30 ms，够滤机械抖动
#define KEY_LONG_MS      3000      // 长按判定：3 s
/* K1 短按上限。
 * 上一版是 1000 ms，也就是"按住 1~3 s 松手什么也不发生"（原意是防误触）。
 * 但实测下来这条静默死区正是"按键没反应"的主要体感来源：人检验按键时
 * 很自然地按 1.5~2.5 s，全落在这条缝里。现在放到 2999 ms，
 * 任何一次按压都有结果：< 3 s 过压、≥ 3 s 欠压，中间不再有静默段。
 * 想恢复防误触，把这里改回 1000 即可（其余代码不用动）。 */
#define KEY_SHORT_MAX_K1 2999
#define KEY_SHORT_UNLIMIT 0xFFFF   // K2 短按不设上限

typedef enum { KEY_EV_NONE = 0, KEY_EV_SHORT, KEY_EV_LONG } KeyEvent_t;

typedef struct {
  uint8_t  pressed;         // 已确认按下
  uint8_t  armed;           // 已见过松开电平，才允许认定下一次按下
  uint8_t  long_triggered;  // 长按已触发（防止连发）
  uint32_t t_press;         // 按下时刻（HAL_GetTick，ms）
  uint16_t short_max_ms;    // 短按最大按压时长
  uint16_t long_ms;         // 长按判定时长
} KeyCtx_t;

// KEY2：温度模拟切换 / 长按停机
KeyCtx_t key2_ctx = { 0, 0, 0, 0, KEY_SHORT_UNLIMIT, KEY_LONG_MS };
// KEY1：过压模拟切换 / 长按欠压模拟切换
KeyCtx_t key1_ctx = { 0, 0, 0, 0, KEY_SHORT_MAX_K1,  KEY_LONG_MS };

// 温度模拟模式：0=正常, 1=模拟高温(35℃)
uint8_t temp_simulate = 0;

// ADC采样节拍：改用 HAL_GetTick() 判断 1 秒，不再用"循环圈数"折算
// （原来的 adc_sample_cnt 已删除：一圈的真实长度随刷屏量变化，折算必然失真）

/* 电压模拟模式：0=正常, 1=过压, 2=欠压（2026-11-14 新增，K1 控制）
 * 直接改写 total_volt 参与显示、上报、报警，走的是同一条判据，
 * 不是另开一套假数据 —— 这样验证的是真实报警链路。 */
uint8_t volt_simulate = 0;
#define VOLT_SIM_OVP    13.50f   // 过压模拟值（> 12.6 触发报警）
#define VOLT_SIM_UVP     8.00f   // 欠压模拟值（< 9.0 触发报警）

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void Key_Scan(void);
void WiFi_SendFrame(float volt, float temp, int fan);
Alarm_t Alarm_Update(float volt, uint8_t fan_st);
uint8_t WiFi_LinkOK(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// 等待串口发送完成（带超时保护）
void UART_WaitTxComplete(UART_HandleTypeDef *huart, uint32_t timeout)
{
  uint32_t tickstart = HAL_GetTick();
  while(__HAL_UART_GET_FLAG(huart, UART_FLAG_TC) == RESET)
  {
    if((HAL_GetTick() - tickstart) > timeout)
    {
      break;
    }
  }
}

/* ===== 通用按键状态机（2026-11-14 二次重构，K2 / K1 共用）=====
 * 每个主循环调一次，喂入当前电平（1=按下），返回"无 / 短按 / 长按"。
 *
 * 流程：
 *   ① armed == 0 时不认任何按下 —— 上电第一次采样若恰好是高电平（极性配错、
 *      引脚被别的东西拉住、按键卡住），只会一直沉默，不会自己连发事件。
 *      必须先读到一次低电平，才把这一次"按下"当数。
 *   ② 首次读到高电平 → 记 t_press，进入按下状态；30 ms 内的再次采样仍在
 *      消抖窗口里，不产生事件（机械抖动会被这一步吃掉）。
 *   ③ 一直按住且 held ≥ long_ms → 出一次长按事件，long_triggered 置位防连发。
 *   ④ 松手时：held ≥ long_ms 补发一次长按（补掉采样点漏判的 3 s 边界）；
 *      否则 held 落在 [消抖窗口, short_max_ms] 内出短按。
 *
 * 时间全部来自 HAL_GetTick()，单位 ms。主循环一圈 90 ms 也好、60 ms 也好，
 * 都不影响 3 s 就是 3 s。
 */
KeyEvent_t Key_Process(KeyCtx_t *k, uint8_t level)
{
  uint32_t now  = HAL_GetTick();
  uint32_t held;

  if(level)
  {
    /* ---- 按下 ---- */
    if(!k->armed)
    {
      return KEY_EV_NONE;          /* 还没见过松开电平，不认 */
    }

    if(!k->pressed)
    {
      k->pressed        = 1;
      k->long_triggered = 0;
      k->t_press        = now;     /* 记下按下时刻，消抖窗口从这里算 */
      return KEY_EV_NONE;
    }

    held = now - k->t_press;

    if(held < KEY_DEBOUNCE_MS)
    {
      return KEY_EV_NONE;          /* 还在消抖窗口内 */
    }

    if(!k->long_triggered && (held >= k->long_ms))
    {
      k->long_triggered = 1;       /* 只触发一次，一直按住不会反复执行 */
      return KEY_EV_LONG;
    }
    return KEY_EV_NONE;
  }
  else
  {
    /* ---- 松开 ---- */
    KeyEvent_t ev = KEY_EV_NONE;

    if(k->pressed)
    {
      held = now - k->t_press;

      /* 松手时补一次长按判定。长按本来只在"仍按住"的采样点上触发，
       * 而采样周期约 90 ms：按住整 3.0 s 时可能刚好卡在
       * "上一个采样点 2.91 s 还没到、下一个采样点已经松手"之间，长按被判漏。
       * 在这里再判一次，3 s 边界就补严了（先判长按，再判短按，不会重复出事件）。 */
      if(!k->long_triggered && (held >= k->long_ms))
      {
        ev = KEY_EV_LONG;
      }
      else if(!k->long_triggered
              && (held >= KEY_DEBOUNCE_MS)
              && (held <= k->short_max_ms))
      {
        ev = KEY_EV_SHORT;
      }
    }

    k->pressed        = 0;
    k->long_triggered = 0;
    k->armed          = 1;          /* 见到松开了，下一次按下才有效 */
    return ev;
  }
}

/* ===== 蜂鸣器节拍器（2026-11-14 新增，取代所有 HAL_Delay 鸣叫）=====
 *
 * 【为什么必须改】原来报警是这么写的：
 *     BEEP=1; HAL_Delay(100); BEEP=0; HAL_Delay(80); BEEP=1; HAL_Delay(100); BEEP=0;
 * 这一串把主循环整整卡死 280 ms。加上原来的全屏清屏，报警那一秒会看到
 * "卡一下 + 闪一下"叠在一起。现在清屏已经没有了，但 280 ms 的卡顿还在。
 *
 * 【现在的做法】把"响多久、停多久"写成表，主循环每轮调一次 Beep_Task()，
 * 时间到了才翻一次电平，其余时间立刻返回。整段鸣叫占 0 ms 主循环时间。
 *
 * 表的结构：{响 ms, 停 ms}，最后一项 {0,0} 作为结束标记。
 *   PAT_ALARM_VOLT  {100,80},{100,0}  → 双短音，一声长一声短由"停 80"隔开
 *   PAT_ALARM_TEMP  {100,0}           → 单短音
 * 注意 off=0 有特殊含义：表示"这一声是最后一声，响完直接关掉收摊"，
 * 不能写成 {100,80},{100,80}，那样最后会一直响下去。
 */
typedef struct
{
  uint16_t on;    /* 响多久 (ms) */
  uint16_t off;   /* 停多久 (ms)，0 = 到此结束 */
} BeepStep_t;

static const BeepStep_t PAT_ALARM_VOLT[] = {{100, 80}, {100, 0}, {0, 0}};
static const BeepStep_t PAT_ALARM_TEMP[] = {{100,  0},        {0, 0}};
static const BeepStep_t PAT_KEY1[]       = {{ 50,  0},        {0, 0}};
static const BeepStep_t PAT_KEY2[]       = {{ 50, 60}, { 50, 0}, {0, 0}};

static const BeepStep_t *beep_pat    = 0;   /* 正在播的表 */
static uint8_t           beep_sp     = 0;   /* 播到第几步 */
static uint8_t           beep_phase  = 0;   /* 0=正在响  1=间隔中 */
static uint8_t           beep_active = 0;   /* 0=没有在播 */
static uint32_t          beep_t_next = 0;   /* 下一次翻电平的时刻 */

/* 起播一段。打断正在播的那一段（报警优先级天然高于按键反馈）。 */
static void Beep_Play(const BeepStep_t *pat)
{
  if((pat == 0) || (pat[0].on == 0)) return;

  beep_pat    = pat;
  beep_sp     = 0;
  beep_phase  = 0;
  beep_active = 1;
  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
  beep_t_next = HAL_GetTick() + pat[0].on;
}

/* 每个主循环调一次。没到翻电平的时刻就立刻返回，阻塞时间为 0。 */
static void Beep_Task(void)
{
  uint32_t now = HAL_GetTick();

  if(beep_active == 0) return;
  if((int32_t)(now - beep_t_next) < 0) return;   /* 有符号比较，防 tick 回绕 */

  if(beep_phase == 0)
  {
    /* 这一声该结束了 */
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
    if(beep_pat[beep_sp].off == 0)               /* 也是最后一声 */
    {
      beep_active = 0;
      return;
    }
    beep_phase  = 1;
    beep_t_next = now + beep_pat[beep_sp].off;
  }
  else
  {
    /* 间隔结束，进下一声 */
    beep_sp++;
    if(beep_pat[beep_sp].on == 0)                /* 表到头了 */
    {
      beep_active = 0;
      return;
    }
    beep_phase  = 0;
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    beep_t_next = now + beep_pat[beep_sp].on;
  }
}

/* 按键反馈音：短按 1 声、长按 2 声，闭眼也能分清按到了哪一种。
 * 现在只负责"排节目"，真正的发声由 Beep_Task() 在主循环里推进。 */
static void Key_Beep(uint8_t n)
{
  Beep_Play((n >= 2) ? PAT_KEY2 : PAT_KEY1);
}

/* ===== 按键扫描：K2（温度模拟 / 长按停机）+ K1（电压模拟）=====
 *
 * K1 = 核心板 PA0（野火原理图上叫 SW1，WKUP 键）。与 K2 的区别只有短按上限：
 *   短按 < 3 s       过压模拟 ⇄ 常规
 *   长按 ≥ 3 s       欠压模拟 ⇄ 常规
 *   （上一版是"1~3 s 松手忽略"的防误触死区，实测下来那条静默带正是
 *     "按了没反应"的主因，已去掉；要恢复见 KEY_SHORT_MAX_K1 的注释。）
 * K2 的短按同样不设实际上限，且长按 3 s 是停机，所以它只分两档。
 */
void Key_Scan(void)
{
  /* ---------- K2：温度模拟 / 长按停机 ---------- */
  switch(Key_Process(&key2_ctx,
                     (HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_SET) ? 1 : 0))
  {
    case KEY_EV_SHORT:
      temp_simulate = !temp_simulate;
      printf("\r\n[测试] 温度模拟模式: %s (35C)\r\n", temp_simulate ? "ON" : "OFF");
      Key_Beep(1);
      break;

    case KEY_EV_LONG:
      if(system_running == 1)
      {
        printf("\r\n[系统] 系统已经停止\r\n");
        UART_WaitTxComplete(&huart1, 200);
        HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
        Motor_Stop();
        fan_status = 0;
        fan_speed  = 0;
        system_running = 0;
        Key_Beep(1);
      }
      break;

    default:
      break;
  }

  /* ---------- K1：过压 / 欠压模拟切换 ---------- */
  switch(Key_Process(&key1_ctx,
                     (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET) ? 1 : 0))
  {
    case KEY_EV_SHORT:   /* 短按：过压 ⇄ 常规 */
      if(volt_simulate == 1)
      {
        volt_simulate = 0;
        printf("\r\n[模拟] 电压模拟: OFF\r\n");
      }
      else
      {
        volt_simulate = 1;
        printf("\r\n[模拟] 电压模拟: 过压 %.2fV\r\n", VOLT_SIM_OVP);
      }
      Key_Beep(1);
      break;

    case KEY_EV_LONG:    /* 长按：欠压 ⇄ 常规 */
      if(volt_simulate == 2)
      {
        volt_simulate = 0;
        printf("\r\n[模拟] 电压模拟: OFF\r\n");
      }
      else
      {
        volt_simulate = 2;
        printf("\r\n[模拟] 电压模拟: 欠压 %.2fV\r\n", VOLT_SIM_UVP);
      }
      Key_Beep(2);
      break;

    default:
      break;
  }
}

/* ===== 报警判定（2026-11-14 重构）=====
 *
 * 原来只有电压越限才动 LED 和蜂鸣器，温度到了让风扇转却一声不响 ——
 * 按钮切进"高温模拟"时整机静默，等于漏掉了告警这一整套功能。
 *
 * 现在两条独立告警，同时成立时电压优先（电池越限才是真正危险）：
 *
 *   ① 电压越限   V > 12.6 或 V < 9.0   →  双短音
 *   ② 高温告警   风扇已进入低速/高速档   →  单短音
 *                （低速档起始温度 28 ℃，含高温模拟模式 35 ℃）
 *
 * 为什么用 fan_status 而不是直接比温度：
 *   fan_status 才是"此刻真的在驱动风扇"的准确表示。它自动带上了
 *   迟滞死区（26~28 ℃ 保持原状态）和 NTC 故障（fan_status=0，不报警），
 *   拿温度再判一次就要把这些边界重写一遍，容易漏。
 *
 * 蜂鸣器不用长鸣：主循环一轮 1 秒，长鸣会盖住按键反馈。
 * 双短音 / 单短音靠耳朵就能分清是电压问题还是温度问题，不看屏也成立。
 * 返回：Alarm_t —— ALARM_OVP / ALARM_UVP / ALARM_TEMP / ALARM_NONE
 *       （2026-11-14 由 1/2/0 改成枚举，好让屏幕能分出过压还是欠压）
 *
 * 【2026-11-14 晚】本函数不再阻塞。原来三个分支里各有一串 HAL_Delay，
 * 电压报警要卡 280 ms（100+80+100），高温报警卡 100 ms。现在只做两件事：
 * 定 LED 电平、把鸣叫表交给 Beep_Play()，然后立刻返回。
 * 代价：报警音从"每次调用响完再返回"变成"每秒起播一次"，听感完全一样。
 */
Alarm_t Alarm_Update(float volt, uint8_t fan_st)
{
  /* 电压越限优先：3S 锂电过压/欠压是真危险，温度高只是要吹风 */
  if(volt > VOLT_OVP)
  {
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    Beep_Play(PAT_ALARM_VOLT);
    return ALARM_OVP;
  }

  if(volt < VOLT_UVP)
  {
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    Beep_Play(PAT_ALARM_VOLT);
    return ALARM_UVP;
  }

  if((fan_st == 1) || (fan_st == 2))
  {
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
    Beep_Play(PAT_ALARM_TEMP);
    return ALARM_TEMP;
  }

  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  return ALARM_NONE;
}

/* ===== WiFi 数据帧发送（2026-11-13 新增）=====
 * 走 USART3 → PB10 → J6-3 → NodeMCU 的 RX。
 *
 * 帧格式（纯 ASCII，冒号分隔，一行一帧）：
 *     V:12.09,T:28.5,F:30\n
 *       V = 电池端电压，已补 D1 压降，单位 V，2 位小数
 *       T = NTC 温度，单位 °C，1 位小数；探头异常时发 NTC
 *       F = 风扇占空比，整数百分比（就是 OLED 上显示的那个数）
 *
 * 为什么用这种格式而不是 JSON：ESP8266 侧用 indexOf 就能取值，
 * 不用引入 ArduinoJson，RAM 和 Flash 都省，调试时肉眼也能读。
 */

/* 没有独立 USB-TTL 时，用调试口回显 USART3 的帧内容做自证。
 * 1 = 开启（开发期），0 = 关闭（独立运行时关掉，少一路无谓输出）
 * 注意：它只能证明"帧内容、周期、F 值"正确，
 *       不能证明 PB10 引脚真的输出了电平——那一步只能靠外部接收器（NodeMCU）。 */
#define WIFI_DEBUG_ECHO  1

void WiFi_SendFrame(float volt, float temp, int fan)
{
  char buf[64];
  int len;

  if (TEMP_NTC_BAD(temp))
  {
    len = snprintf(buf, sizeof(buf), "V:%.2f,T:NTC,F:%d\n", volt, fan);
  }
  else
  {
    len = snprintf(buf, sizeof(buf), "V:%.2f,T:%.1f,F:%d\n", volt, temp, fan);
  }

  if (len > 0)
  {
#if WIFI_DEBUG_ECHO
    /* %.*s 只取前 len-1 个字符，把帧尾的 \n 掐掉，避免和 \r\n 撞出空行 */
    printf("[TX3] %.*s\r\n", (int)(len - 1), buf);
#endif

    /* 100 ms 超时：115200 下 30 字节只要 3 ms，给足余量但不会卡死主循环 */
    HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, 100);
  }
}

/* ===== WiFi 链路是否真的通着（2026-11-14 新增）=====
 * 判据只有一条：6 秒内收到过 ESP8266 的应答字符 'P'。
 * 没收到就老实返回 0 —— 屏幕会显示"无线发"（本机在发，对端没应答），
 * 而不是假装"通"。拔掉 J6-4 那根线，"通"会在一轮超时内变回"发"，
 * 这就是这条指示唯一的价值：它能被人为证伪。
 */
uint8_t WiFi_LinkOK(void)
{
  return (wifi_rx_tick != 0) && ((HAL_GetTick() - wifi_rx_tick) < WIFI_LINK_TIMEOUT);
}

/* USART3 接收完成回调（HAL 的 weak 函数，这里覆盖）
 * 只收 1 字节就中断一次，处理完立刻重新挂上，保证不丢下一帧。
 * 只认 'P'：ESP 侧回的是 "PONG\n"，首字符对上了才算心跳；
 * 这样即使 PB11 悬空捡到噪声字节，也不会误判成"通"。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    if (wifi_rx_byte == 'P')
    {
      wifi_rx_tick = HAL_GetTick();
    }
    HAL_UART_Receive_IT(&huart3, &wifi_rx_byte, 1);
  }
}

/* USART3 中断入口。
 * 本工程没在 CubeMX 里勾 USART3 中断，stm32f1xx_it.c 里也就没有这个函数；
 * 启动文件的向量表里给的是 weak 默认实现（死循环 B .），不在这里覆盖，
 * 一开接收中断就会卡死。函数名必须和 startup_stm32f103xb.s 里完全一致。 */
void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart3);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
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
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  // WiFi 数据通道：USART3 → PB10/PB11 → J6-3/J6-4 → NodeMCU
  // 2026-11-13 新增。不接模块时也不影响其他功能，只是没人收这帧数据。
  MX_USART3_UART_Init();
  printf("[DEBUG] USART3 (WiFi) 初始化完成\r\n");

  /* 挂上 1 字节接收中断，等 ESP8266 的应答（心跳）。
   * 这时 J6 上可能还没接模块，不影响运行：收不到就显示"无线发"。 */
  HAL_UART_Receive_IT(&huart3, &wifi_rx_byte, 1);
  printf("[DEBUG] USART3 接收中断已挂载\r\n");

  /* ===== K1（PA0 / WKUP 键）初始化（2026-11-14 新增）=====
   * 放在 main 里而不是 gpio.c，是为了不被 CubeMX 重新生成时覆盖掉。
   *
   * 极性问题已经查过核心板原理图（[野火小智]STM32F103C8 核心板 双USB款，第 5 页「按键」）：
   *     PA0 --[R8 1K]--+--[SW1]-- 3V3
   *                    |
   *                 [R9 4.7K]      [C14 0.1uF]
   *                    |              |
   *                   GND            GND
   * 即：平时被 R9 下拉到 GND，按下由 SW1 接到 3V3 —— **高电平有效**。
   * PC13 上的 SW2（K2）是完全相同的接法（R11 4.7K 下拉 + 接 3V3）。
   * 所以这里用 GPIO_PULLDOWN、Key_Scan 里判 == GPIO_PIN_SET，与硬件一致，
   * 不要再按"按下接地"去改 PULLUP/RESET，那样反而会让两个键都反相。 */
  {
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = KEY1_Pin;
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLDOWN;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KEY1_GPIO_Port, &gi);
  }
  printf("[DEBUG] KEY1 (PA0) 初始化完成，短按=过压模拟 / 长按=欠压模拟\r\n");

  // 启动ADC DMA双通道扫描
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf, ADC_CHANNEL_NUM);

  // 电机初始化（风扇插在电机转轴上，电机转=风扇转）
  printf("[DEBUG] 开始电机/风扇初始化...\r\n");
  Motor_Init();      // 配置电机引脚，启动TIM3_CH1 PWM
  Motor_Enable();    // 使能电机（退出待机模式）
  printf("[DEBUG] 电机/风扇初始化完成！\r\n");

  // OLED初始化
  printf("[DEBUG] 开始OLED初始化...\r\n");
  OLED_Init();
  printf("[DEBUG] OLED初始化完成！\r\n");
  OLED_Clear();
  OLED_ShowCNStr(0, 32, 4, CN_DIAN, CN_CHI, CN_JIAN, CN_CE);      /* 电池监测 */
  OLED_ShowCNStr(4, 32, 4, CN_XI, CN_TONG, CN_QI, CN_DONG);      /* 系统启动 */
  OLED_Refresh();          /* 画完必须刷一次，否则屏幕上什么都没有 */
  printf("[DEBUG] OLED显示完成！\r\n");

  /* 开机画面停留 500 ms，但这 500 ms 必须继续扫键。
   * 原来这里是连着两次 HAL_Delay(500)，上电后整整 1 秒 Key_Scan 一次都不跑，
   * 这 1 秒内按下（哪怕一直按住）的键全部被吞掉 —— 这是"重启后按键不灵"
   * 的第一层原因，也是唯一真正"只在启动初期"出现的那一层。
   * 改成每 50 ms 扫一次，开机画面期间按键照常生效；顺带把原来那句
   * "等待串口外设/DAP 就绪"也合并进来（printf 本身不需要等待）。 */
  {
    uint8_t i;
    for(i = 0; i < 10; i++)
    {
      Key_Scan();
      Beep_Task();        /* 开机画面期间按下的键也要叫得出来 */
      HAL_Delay(50);
    }
  }

  printf("=========================================\r\n");
  printf("  3S Battery + Temp Monitor System\r\n");
  printf("=========================================\r\n");
  printf("  Volt Range: 9.0V ~ 12.6V\r\n");
  printf("  Temp Thresholds: %.1f/%.1f/%.1f C\r\n", TEMP_FAN_OFF, TEMP_FAN_ON, TEMP_FAN_HIGH);
  printf("  ADC: DMA Dual Channel (IN4=Bat/PA4, IN5=NTC/PA5)\r\n");
  printf("=========================================\r\n");
  
  /* 开机画面就停在上面那一屏，这里不再另画一闪而过的 "Ready"
   * —— 主循环约 1 秒后就会被第一帧实时数据覆盖，中间多插一屏只会闪一下。 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
while (1)
{
  /* 2026-11-14：这两个变量提到循环体开头（原来声明在 if 块里）。
   * 原因：显示刷新被绑在"每 20 圈进一次"的节拍上，整屏每秒黑一次。
   * 现在显示搬出 if、每圈都刷，变量必须活在循环作用域内。
   * alarm 用 static：它只在数据更新那一圈重算，其余圈沿用上次结果。 */
  static Alarm_t alarm = ALARM_NONE;
  static uint32_t t_last_sample = 0;
  char buf[24];

  // 按键扫描：每个主循环一次。循环周期约 55 ms（50 ms 延时 + 几 ms 差异刷新，
  // 见 oled.c 的 OLED_Refresh），状态机内部用 HAL_GetTick() 计时，
  // 所以这个周期只影响"多久采样一次"，不影响"按住多久算长按"。
  Key_Scan();

  /* 蜂鸣器节拍推进。必须每轮都调，且必须在 Key_Scan 之后 ——
   * Key_Scan 里排了节目（按键反馈音），这里负责按时翻电平，两者都不阻塞。 */
  Beep_Task();

  if(system_running == 1)
  {
    // 温度模拟模式：每次循环都检查，立即生效
    static uint8_t last_temp_simulate = 0;
    if(temp_simulate)
    {
      temperature = 35.0f;
      // 刚进入模拟模式，立即启动风扇和电机
      if(last_temp_simulate == 0)
      {
        last_temp_simulate = 1;
        fan_status = 2;
        fan_speed = MOTOR_SPEED_MAX;
        // 电机转=风扇转，设置电机PWM=MOTOR_SPEED_MAX（高速档，见 motor.h），正转
        Motor_Forward(MOTOR_SPEED_MAX);
        printf("[Fan] High Speed %d%% (Simulate)\r\n", fan_speed);
      }
    }
    else
    {
      // 刚退出模拟模式，重置状态，让下一次采样重新判断
      if(last_temp_simulate == 1)
      {
        last_temp_simulate = 0;
        fan_status = 99;  // 特殊值，强制让下一次采样重新判断
        Motor_Stop();  // 停止电机=停止风扇
        printf("[Fan] Simulate OFF, resume normal control\r\n");
      }
    }
    
    // 系统运行中：执行电压监测
    /* 采样 / 串口 / WiFi 固定 1 秒一次，用 HAL_GetTick() 判断。
     * 原来靠"循环圈数"折算（20 圈 ≈ 1 s），但一圈的真实长度随刷屏数据量变化：
     * 6×8 小字时约 90 ms，换成 16×16 汉字后变成约 95 ms，改成差异刷新后又回到
     * 约 55 ms —— 这种折算每次改显示都会失真，所以直接换成毫秒判定。
     * 显示刷新不在这个 if 里，每圈都刷，跟采样快慢无关。 */
    if((uint32_t)(HAL_GetTick() - t_last_sample) >= 1000)
    {
      t_last_sample = HAL_GetTick();
      
      // 从DMA缓冲区读取数据（DMA一直在后台自动更新）
      uint16_t adc_volt = adc_dma_buf[0];  // IN4 - 电池电压（PA4）
      uint16_t adc_temp = adc_dma_buf[1];  // IN5 - NTC 温度（PA5）

      // 校准后的电压计算
      //   先按分压比还原 VBAT_F，再加上 D1 的正向压降，得到 J1 端真实电池电压
      total_volt = (adc_volt * adc_ref / 4095.0f) * divider_k + DIODE_VF_COMP;

      /* 电压模拟（K1 控制）：直接改写参与显示 / 上报 / 报警的那个值。
       * 注意是"改 result"而不是"造假数据源"—— ADC 原始码 adc_volt 不动，
       * 所以 OLED 上的 V-ADC 仍是真实采样，一眼能分辨是模拟还是真过压。 */
      if(volt_simulate == 1)
      {
        total_volt = VOLT_SIM_OVP;
      }
      else if(volt_simulate == 2)
      {
        total_volt = VOLT_SIM_UVP;
      }

      // 非模拟模式才读取真实温度
      if(!temp_simulate)
      {
        /* ===== NTC 温度换算（B 值法），替换原先的线性简化公式 ===== */
        float ntc_volt = adc_temp * adc_ref / 4095.0f;

        /* 2026-11-13：阈值 4040 → 3980。
         * 实测探头未插时 PA5 只落在 4031~4045，横跨原阈值 4040：
         *   - 4031~4035 走 else 分支算出约 -47 °C，被限幅成 -40.0 °C；
         *   - 4041 以上判为 open。
         * 于是每秒在 "-40.0C" 和 "NTC--(open)" 之间来回跳。
         * 原因：PA5 悬空时仍有约 5 µA 漏电，10 k 上拉撑不到满量程。
         * 3980 对应 R_ntc ≈ 346 kΩ、等效约 -37.9 °C，仍远低于任何真实工况。 */
        if(adc_temp > 3980)
        {
          // 探头未插或断线：R3 把 PA5 拉到接近 3V3
          temperature = TEMP_NTC_OPEN;
        }
        else if(adc_temp < 15)
        {
          // 探头短路到地：PA5 接近 0 V
          temperature = TEMP_NTC_SHORT;
        }
        else
        {
          float r_ntc   = NTC_R_PULLUP * ntc_volt / (adc_ref - ntc_volt);
          float inv_T   = 1.0f / NTC_T0_K + logf(r_ntc / NTC_R0) / NTC_B;
          temperature   = 1.0f / inv_T - 273.15f;

          // 传感器物理范围限幅（-40 ~ 125 °C）
          if(temperature < -40.0f) temperature = -40.0f;
          if(temperature > 125.0f) temperature = 125.0f;
        }
      }

      // 串口打印（探头异常时明确标出，避免把 -99 误当成真实温度）
      if(TEMP_NTC_BAD(temperature))
      {
        printf("Battery: %.2fV | Temp: NTC-- (%s) | V-ADC: %d | T-ADC: %d\r\n",
               total_volt,
               (temperature < -98.5f) ? "open" : "short",
               adc_volt, adc_temp);
      }
      else
      {
        printf("Battery: %.2fV | Temp: %.1fC | V-ADC: %d | T-ADC: %d\r\n",
               total_volt, temperature, adc_volt, adc_temp);
      }

      // ========== 温控逻辑（迟滞控制）- 非模拟模式才执行 ==========
      // 风扇插在电机转轴上，电机转=风扇转
      if(!temp_simulate)
      {
        if(TEMP_NTC_BAD(temperature))
        {
          // 探头异常（未插/断线/短路）：没有可信温度依据，停止风扇
          if(fan_status != 0)
          {
            fan_status = 0;
            fan_speed = 0;
            Motor_Stop();
            printf("[Fan] Stop (NTC fault)\r\n");
          }
        }
        else if(temperature > TEMP_FAN_HIGH)
        {
          // 高温：全速
          if(fan_status != 2)
          {
            fan_status = 2;
            fan_speed = MOTOR_SPEED_MAX;
            Motor_Forward(MOTOR_SPEED_MAX);  // 电机正转=MOTOR_SPEED_MAX = 风扇全速
            printf("[Fan] High Speed %d%%\r\n", fan_speed);
          }
        }
        else if(temperature > TEMP_FAN_ON)
        {
          // 中温：低速档（独立值 MOTOR_SPEED_LOW，不是高速档的一半）
          // 2026-11-14 实测：12 V 有刷电机 40% 档 J2 = 4.7 V，稳定起转。
          // （原注释按 BLDC 风扇的 6~7 V 启动电压写"40% 起不来"，已作废）
          if(fan_status != 1)
          {
            fan_status = 1;
            fan_speed = MOTOR_SPEED_LOW;
            Motor_Forward(fan_speed);   // 电机正转 = MOTOR_SPEED_LOW = 风扇低速
            printf("[Fan] Low Speed %d%%\r\n", fan_speed);
          }
        }
        else if(temperature < TEMP_FAN_OFF)
        {
          // 低温：停止
          if(fan_status != 0)
          {
            fan_status = 0;
            fan_speed = 0;
            Motor_Stop();        // 停止电机 = 停止风扇
            printf("[Fan] Stop\r\n");
          }
        }
      }

      // 同一节拍（约 1 秒）把本轮数据发给 NodeMCU。
      // 放在温控逻辑之后，F 才是本轮刚更新的 fan_speed，不是上一轮的。
      WiFi_SendFrame(total_volt, temperature, fan_speed);

      /* ===== 报警判定（先算，后显示）=====
       * 原来 Alarm_Update 排在显示之后，报警信息只能另开一行（page 7）。
       * 改中文后一行占 2 页、整屏只剩 4 行，没有第 5 行可用了，
       * 所以把报警并进第 4 行的状态词，必须先拿到结果再画。 */
      alarm = Alarm_Update(total_volt, fan_status);
    }

    /* ===== OLED 显示（4 行 × 16 px 中文）=====
     * 中文用 OLED_ShowCNStr 画（每字 16 px 宽），数字用 FONT_8x16 画
     * （每字符 8 px 宽）。三行数值统一右对齐到 x=124，让小数点排成一列。
     *
     * 布局（参数 y 是页号，一个汉字吃 y 与 y+1 两页）：
     *   y=0   电池 12.48V
     *   y=2   温度  25.3C
     *   y=4   风扇  低速     40%
     *   y=6   系统正常          无线通
     */

    OLED_Clear();

    // Line 0: 电池电压
    OLED_ShowCNStr(0, 0, 2, CN_DIAN, CN_CHI);
    snprintf(buf, sizeof(buf), "%.2fV", total_volt);
    OLED_ShowString(0, (uint8_t)(124 - (uint8_t)strlen(buf) * 8), buf, FONT_8x16);

    // Line 2: 温度
    OLED_ShowCNStr(2, 0, 2, CN_WEN, CN_DU);
    if(TEMP_NTC_BAD(temperature))
    {
      snprintf(buf, sizeof(buf), "----");   // 探头未接 / 断线 / 短路
    }
    else
    {
      snprintf(buf, sizeof(buf), "%.1fC", temperature);
    }
    OLED_ShowString(2, (uint8_t)(124 - (uint8_t)strlen(buf) * 8), buf, FONT_8x16);

    // Line 4: 风扇档位
    OLED_ShowCNStr(4, 0, 2, CN_FENG, CN_SHAN);
    if(fan_status == 0)
    {
      OLED_ShowCNStr(4, 48, 2, CN_TING, CN_ZHI);        // 停止
    }
    else if(fan_status == 1)
    {
      OLED_ShowCNStr(4, 48, 2, CN_DI, CN_SU);           // 低速
      snprintf(buf, sizeof(buf), "%d%%", fan_speed);
      OLED_ShowString(4, (uint8_t)(124 - (uint8_t)strlen(buf) * 8), buf, FONT_8x16);
    }
    else if(fan_status == 2)
    {
      OLED_ShowCNStr(4, 48, 2, CN_GAO, CN_SU);          // 高速
      snprintf(buf, sizeof(buf), "%d%%", fan_speed);
      OLED_ShowString(4, (uint8_t)(124 - (uint8_t)strlen(buf) * 8), buf, FONT_8x16);
    }
    else
    {
      // fan_status == 99：退出温度模拟后的重判死区，状态尚未确定
      OLED_ShowString(4, 48, "--", FONT_8x16);
    }

    /* Line 6: 系统状态 + 无线链路
     * 左边 4 个汉字（x 0~63），右边"无线"2 字（x 76~107）+ 1 字状态（x 108）。
     * 优先级：模拟标记 > 实际报警 > 正常。
     *   模拟时第一行本来就会显示 13.50V / 8.00V，用户得能一眼分清
     *   "这是我按键造出来的"还是"电池真出问题了"，所以模拟标记排最前。 */
    if(volt_simulate == 1)
    {
      OLED_ShowCNStr(6, 0, 4, CN_GUO, CN_YA, CN_MO, CN_NI);      // 过压模拟
    }
    else if(volt_simulate == 2)
    {
      OLED_ShowCNStr(6, 0, 4, CN_QIAN, CN_YA, CN_MO, CN_NI);     // 欠压模拟
    }
    else if(temp_simulate)
    {
      OLED_ShowCNStr(6, 0, 4, CN_GAO, CN_WEN, CN_MO, CN_NI);     // 高温模拟
    }
    else if(alarm == ALARM_OVP)
    {
      OLED_ShowCNStr(6, 0, 4, CN_GUO, CN_YA, CN_BAO, CN_JING);   // 过压报警
    }
    else if(alarm == ALARM_UVP)
    {
      OLED_ShowCNStr(6, 0, 4, CN_QIAN, CN_YA, CN_BAO, CN_JING);  // 欠压报警
    }
    else if(alarm == ALARM_TEMP)
    {
      OLED_ShowCNStr(6, 0, 4, CN_GAO, CN_WEN, CN_BAO, CN_JING);  // 高温报警
    }
    else
    {
      OLED_ShowCNStr(6, 0, 4, CN_XI, CN_TONG, CN_ZHENG, CN_CHANG); // 系统正常
    }

    // 无线：通 = 对面在应答；发 = 本机在发但没人应答（拔了 ESP 就是这个）
    OLED_ShowCNStr(6, 76, 2, CN_WU, CN_XIAN);
    if(WiFi_LinkOK())
    {
      OLED_ShowCN(6, 108, CN_TONG_LINK);
    }
    else
    {
      OLED_ShowCN(6, 108, CN_FA);
    }

    /* 一屏内容画完了，差异刷新一次。稳态下只有几个数字在变，
     * 一帧约 50~150 字节、2~4 ms，屏幕全程不黑。 */
    OLED_Refresh();

    // 主循环延时（50ms），防止CPU占用100%
    HAL_Delay(50);
  }
  else
  {
    // 系统已停止
    t_last_sample = HAL_GetTick();
    fan_status = 0;
    fan_speed = 0;
    Motor_Stop();  // 停止电机=停止风扇
    OLED_Clear();
    OLED_ShowCNStr(0, 32, 4, CN_XI, CN_TONG, CN_YI, CN_TING);               // 系统已停
    OLED_ShowCNStr(4, 24, 5, CN_QING, CN_DUAN, CN_DIAN, CN_CHONG, CN_QI);   // 请断电重启
    OLED_Refresh();
    HAL_Delay(50);
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
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
