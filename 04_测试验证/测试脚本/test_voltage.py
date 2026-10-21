# -*- coding: utf-8 -*-
"""
STM32电压监测系统模拟测试
模拟串口打印效果和电压计算逻辑
"""

# STM32参数
adc_ref = 3.3
divider_k = 6.21

# ADC值转电压
def adc_to_voltage(adc_value):
    return (adc_value * adc_ref / 4095.0) * divider_k

# 打印欢迎信息
def print_welcome():
    print("=========================================")
    print("  3串锂电池电压监测系统（终版）")
    print("=========================================")
    print("  电压范围: 9.0V ~ 12.6V")
    print("=========================================")

# 模拟主循环电压监测
def simulate_voltage_monitor():
    # 模拟一些ADC测试值
    test_adc_values = [
        3000, 3050, 3100, 3150, 3200, 3250, 3300, 3350, 3400, 3450,
        2800, 2900, 3500, 3600, 3700, 2500, 2600, 2700, 3800, 3900
    ]
    
    print("\n=== 开始模拟电压监测 ===\n")
    
    system_running = True
    for adc_value in test_adc_values:
        if system_running:
            voltage = adc_to_voltage(adc_value)
            
            output = f"电池总电压: {voltage:.2f}V | ADC原始值: {adc_value}"
            
            # 模拟报警逻辑
            if voltage > 12.6 or voltage < 9.0:
                output += "  [报警] 电压异常!"
            
            print(output)
    
    print("\n=== 模拟结束 ===")

def main():
    print("\n=== STM32电压监测系统模拟测试 ===\n")
    
    # 测试1: 打印欢迎信息
    print("[测试1] 打印欢迎信息:")
    print_welcome()
    
    # 测试2: 电压计算验证
    print("\n[测试2] 电压计算验证:")
    print("ADC值 | 计算电压")
    print("------|----------")
    for adc in range(0, 4096, 512):
        v = adc_to_voltage(adc)
        print(f"{adc:4d}  |  {v:.2f}V")
    
    # 测试3: 阈值边界测试
    print("\n[测试3] 阈值边界测试:")
    print(f"ADC=2880 (理论9.0V):  {adc_to_voltage(2880):.2f}V")
    print(f"ADC=4032 (理论12.6V): {adc_to_voltage(4032):.2f}V")
    
    # 测试4: 完整模拟
    simulate_voltage_monitor()
    
    print("\n=== 所有测试完成! ===\n")

if __name__ == "__main__":
    main()
