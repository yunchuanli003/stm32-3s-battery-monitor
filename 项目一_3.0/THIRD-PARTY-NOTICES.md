# 第三方组件与商标声明

本仓库包含以下第三方组件，版权归各自所有者，遵循各自许可证。本仓库**未修改**这些文件，仅按原样随工程分发。

---

## 1. STM32F1 HAL Driver

| 项 | 内容 |
|---|---|
| 路径 | `02_固件代码/STM32F103C8T6/LYC1/Drivers/STM32F1xx_HAL_Driver/` |
| 版权 | Copyright (c) STMicroelectronics |
| 许可证 | BSD-3-Clause |
| 许可证全文 | 见同目录 `LICENSE.txt` |
| 许可来源 | <https://opensource.org/licenses/BSD-3-Clause> |

## 2. ARM CMSIS Core

| 项 | 内容 |
|---|---|
| 路径 | `02_固件代码/STM32F103C8T6/LYC1/Drivers/CMSIS/` |
| 版权 | Copyright (c) Arm Limited |
| 许可证 | **Apache License 2.0** |
| 许可证全文 | 见同目录 `LICENSE.txt` |
| 许可来源 | <https://www.apache.org/licenses/LICENSE-2.0> |

## 3. STM32F1 Device 支持包

| 项 | 内容 |
|---|---|
| 路径 | `02_固件代码/STM32F103C8T6/LYC1/Drivers/CMSIS/Device/ST/STM32F1xx/` |
| 版权 | Copyright (c) STMicroelectronics |
| 许可证 | **Apache License 2.0** |
| 许可证全文 | 见同目录 `LICENSE.txt` |
| 许可来源 | <https://www.apache.org/licenses/LICENSE-2.0> |

## 4. 启动文件

| 项 | 内容 |
|---|---|
| 路径 | `02_固件代码/STM32F103C8T6/LYC1/MDK-ARM/startup_stm32f103xb.s` |
| 版权 | Copyright (c) 2017-2021 STMicroelectronics |
| 许可证 | BSD-3-Clause |

---

## 5. 中文字模（点阵字形数据）

| 项 | 内容 |
|---|---|
| 路径 | `02_固件代码/STM32F103C8T6/LYC1/Core/Inc/oled_cn_font.h` |
| 规模 | 38 个汉字，宋体 15 px 取模，1 bit/像素 |
| 生成方式 | 用「宋体（SimSun，中易宋体）」渲染后逐字取模，只提取本项目 OLED 界面实际用到的 38 个字符 |
| 分发范围 | 本仓库**不分发任何字库文件**（无 `.ttf` / `.ttc` / `.fon`），只包含 38 个单字的低分辨率点阵数组 |
| 权利归属 | 宋体字库的著作权归字库权利人所有，本项目不主张任何相关权利 |
| 本项目立场 | 单字点阵属功能性显示数据，独创性极低，用途限于本项目界面还原；不构成对字库的复制、破解或再分发，也不向任何第三方授予该字库的使用权 |
| 使用限制 | 二次使用者若需商用或大幅扩充字符集，应自行取得字库授权，或改用开源字体（思源黑体、文泉驿等）重新取模 |
| 替换预案 | 如需彻底规避，可改用 SIL OFL 授权字体重新生成 `oled_cn_font.h` 并重编固件。注意这样做会让仓库内 `05_交付产物/固件/LYC1_v3.0.7.hex` 与源码不再对应，必须同步重编、重烧并更新该 hex |

---

## 说明

`Drivers/` 与 `MDK-ARM/startup_stm32f103xb.s` 由 STM32CubeMX 依 STM32CubeF1 软件包生成，许可证文件随组件一同保留，未做删改。各组件的版权头均保留在源文件顶部。

各许可证对再分发的要求（保留版权声明、随附许可证副本）均已满足。若你需要基于本项目二次分发，请一并保留 `Drivers/` 目录下的三个 `LICENSE.txt`。

---

## 商标声明

- **STM32**、**STM32F103C8T6** 为 STMicroelectronics International N.V. 的商标或注册商标。
- **Arm**、**Cortex**、**CMSIS** 为 Arm Limited 的商标或注册商标。
- **Keil**、**MDK-ARM** 为 Arm Limited 的商标或注册商标。
- **野火**、**野火小智** 为深圳市野火电子科技有限公司的商标。
- **江科大** 指 B 站 UP 主「江协科技」的 STM32 教学套件，与本项目无隶属关系。
- **Mornsun**、**金升阳** 为广州金升阳科技有限公司的商标。
- **Littelfuse** 为 Littelfuse, Inc. 的商标。
- **嘉立创**、**立创EDA**、**立创商城** 为深圳嘉立创科技集团股份有限公司的商标。

上述名称仅用于说明本项目所用元件、工具与参考对象，**不表示任何官方认证、合作、赞助或背书关系**。本项目为个人学习项目，与上述公司无任何隶属关系。

---

**最后更新**：2026-11-14（新增第 5 节「中文字模」）
