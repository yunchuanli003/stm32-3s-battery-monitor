#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

// 模拟STM32的参数
const float adc_ref = 3.3f;
const float divider_k = 6.21f;

// 模拟按键状态
uint8_t system_running = 1;

// 将ADC值转换为电压
float adc_to_voltage(uint16_t adc_value) {
    return (adc_value * adc_ref / 4095.0f) * divider_k;
}

// 模拟打印欢迎信息
void print_welcome(void) {
    printf("=========================================\n");
    printf("  3串锂电池电压监测系统（终版）\n");
    printf("=========================================\n");
    printf("  电压范围: 9.0V ~ 12.6V\n");
    printf("=========================================\n");
}

// 模拟主循环的电压监测
void simulate_voltage_monitor(void) {
    // 模拟一些ADC测试值
    uint16_t test_adc_values[] = {
        3000, 3050, 3100, 3150, 3200, 3250, 3300, 3350, 3400, 3450,
        2800, 2900, 3500, 3600, 3700, 2500, 2600, 2700, 3800, 3900
    };
    int num_tests = sizeof(test_adc_values) / sizeof(test_adc_values[0]);
    
    printf("\n=== 开始模拟电压监测 ===\n\n");
    
    for(int i = 0; i < num_tests; i++) {
        if(system_running) {
            uint16_t adc_value = test_adc_values[i];
            float voltage = adc_to_voltage(adc_value);
            
            printf("电池总电压: %.2fV | ADC原始值: %d", voltage, adc_value);
            
            // 模拟报警逻辑
            if(voltage > 12.6f || voltage < 9.0f) {
                printf("  [报警] 电压异常!");
            }
            
            printf("\n");
            
            // 模拟延时
            usleep(100000); // 100ms
        }
    }
    
    printf("\n=== 模拟结束 ===\n");
}

int main() {
    printf("\n=== STM32电压监测系统模拟测试 ===\n\n");
    
    // 测试1: 打印欢迎信息
    printf("[测试1] 打印欢迎信息:\n");
    print_welcome();
    
    // 测试2: 电压计算测试
    printf("\n[测试2] 电压计算验证:\n");
    printf("ADC值 | 计算电压\n");
    printf("------|----------\n");
    for(uint16_t adc = 0; adc <= 4095; adc += 512) {
        float v = adc_to_voltage(adc);
        printf("%4d  |  %.2fV\n", adc, v);
    }
    
    // 测试3: 阈值测试
    printf("\n[测试3] 阈值边界测试:\n");
    printf("ADC=2880 (理论9.0V):  %.2fV\n", adc_to_voltage(2880));
    printf("ADC=4032 (理论12.6V): %.2fV\n", adc_to_voltage(4032));
    
    // 测试4: 完整模拟
    simulate_voltage_monitor();
    
    printf("\n=== 所有测试完成! ===\n\n");
    
    return 0;
}
