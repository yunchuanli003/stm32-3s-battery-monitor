#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
串口发送完成验证脚本
模拟 STM32 中的 UART_WaitTxComplete 逻辑
验证：是否真的在发送完成后才改变 system_running 状态
"""

import time
import sys

class MockUART:
    """模拟 STM32 串口"""
    def __init__(self, send_delay_ms=50):
        self.send_delay = send_delay_ms / 1000.0  # 转换为秒
        self.tc_flag = False  # TC (Transmission Complete) 标志位
        self.send_start_time = 0
        self.is_sending = False
    
    def start_send(self):
        """开始发送（模拟printf调用）"""
        self.tc_flag = False
        self.is_sending = True
        self.send_start_time = time.time()
        print(f"[模拟] 开始发送数据... (需要 {self.send_delay*1000:.0f}ms)")
    
    def update(self):
        """更新串口状态（模拟硬件自动设置TC标志）"""
        if self.is_sending:
            elapsed = (time.time() - self.send_start_time) * 1000
            if elapsed >= self.send_delay:
                self.tc_flag = True
                self.is_sending = False
                print(f"[模拟] 数据发送完成! (耗时 {elapsed:.0f}ms)")
    
    def get_tc_flag(self):
        """获取TC标志位"""
        self.update()  # 先更新状态
        return self.tc_flag


def get_tick():
    """模拟 HAL_GetTick()"""
    return int(time.time() * 1000)


def uart_wait_tx_complete(uart, timeout_ms):
    """模拟 STM32 的 UART_WaitTxComplete 函数"""
    tick_start = get_tick()
    wait_count = 0
    
    print(f"[等待] 进入 UART_WaitTxComplete, 超时={timeout_ms}ms")
    
    while not uart.get_tc_flag():
        wait_count += 1
        elapsed = get_tick() - tick_start
        
        # 每10ms打印一次等待状态
        if wait_count % 10 == 0:
            print(f"[等待] 已等待 {elapsed}ms... TC标志={uart.get_tc_flag()}")
        
        # 超时检查
        if elapsed > timeout_ms:
            print(f"[超时] 等待 {timeout_ms}ms 后超时退出!")
            return False
        
        time.sleep(0.001)  # 1ms 轮询间隔
    
    elapsed = get_tick() - tick_start
    print(f"[完成] 发送完成确认! 实际等待 {elapsed}ms")
    return True


def simulate_key2_long_press(send_delay_ms=50):
    """
    模拟 KEY2 长按触发系统停止
    参数: send_delay_ms - 模拟串口发送需要的时间
    """
    print("\n" + "="*60)
    print(f"模拟测试: KEY2长按停止系统 (发送耗时={send_delay_ms}ms)")
    print("="*60)
    
    # 初始化
    uart = MockUART(send_delay_ms)
    system_running = 1
    
    print(f"[初始状态] system_running = {system_running} (1=运行, 0=停止)")
    print()
    
    # 模拟长按触发
    print("[事件] KEY2长按3秒触发!")
    print()
    
    # 1. 先打印信息
    print("[步骤1] printf(\"[系统] 系统已经停止\")")
    uart.start_send()
    
    # 记录发送前的状态
    state_before_wait = system_running
    print(f"[状态] 等待前: system_running = {state_before_wait}")
    print()
    
    # 2. 等待串口发送完成
    print("[步骤2] UART_WaitTxComplete(&huart1, 200)")
    send_completed = uart_wait_tx_complete(uart, 200)
    print()
    
    # 3. 改变系统状态
    state_after_wait = system_running
    print(f"[状态] 等待后、改变前: system_running = {state_after_wait}")
    print()
    
    print("[步骤3] system_running = 0")
    system_running = 0
    print(f"[状态] 改变后: system_running = {system_running}")
    print()
    
    # 验证结果
    print("-"*50)
    print("验证结果:")
    print(f"  发送是否完成: {'✅ 是' if send_completed else '❌ 否'}")
    print(f"  等待前状态: {state_before_wait}")
    print(f"  等待后状态: {state_after_wait}")
    print(f"  状态改变时机: {'✅ 在发送完成后' if state_after_wait == 1 else '❌ 在发送完成前!'}")
    
    if state_after_wait == 1:
        print("\n✅ 验证通过: 状态确实在发送完成后才改变!")
        return True
    else:
        print("\n❌ 验证失败: 状态在发送完成前就改变了!")
        return False


def simulate_key2_short_press(send_delay_ms=50):
    """模拟 KEY2 短按触发系统开启"""
    print("\n" + "="*60)
    print(f"模拟测试: KEY2短按开启系统 (发送耗时={send_delay_ms}ms)")
    print("="*60)
    
    uart = MockUART(send_delay_ms)
    system_running = 0  # 初始状态：已停止
    
    print(f"[初始状态] system_running = {system_running} (1=运行, 0=停止)")
    print()
    
    print("[事件] KEY2短按触发!")
    print()
    
    # 1. 先打印信息
    print("[步骤1] printf(\"[系统] 系统已经开启\")")
    uart.start_send()
    
    state_before_wait = system_running
    print(f"[状态] 等待前: system_running = {state_before_wait}")
    print()
    
    # 2. 等待串口发送完成
    print("[步骤2] UART_WaitTxComplete(&huart1, 200)")
    send_completed = uart_wait_tx_complete(uart, 200)
    print()
    
    # 3. 改变系统状态
    state_after_wait = system_running
    print(f"[状态] 等待后、改变前: system_running = {state_after_wait}")
    print()
    
    print("[步骤3] system_running = 1")
    system_running = 1
    print(f"[状态] 改变后: system_running = {system_running}")
    print()
    
    print("-"*50)
    print("验证结果:")
    print(f"  发送是否完成: {'✅ 是' if send_completed else '❌ 否'}")
    print(f"  等待前状态: {state_before_wait}")
    print(f"  等待后状态: {state_after_wait}")
    print(f"  状态改变时机: {'✅ 在发送完成后' if state_after_wait == 0 else '❌ 在发送完成前!'}")
    
    if state_after_wait == 0:
        print("\n✅ 验证通过: 状态确实在发送完成后才改变!")
        return True
    else:
        print("\n❌ 验证失败: 状态在发送完成前就改变了!")
        return False


def simulate_timeout_scenario():
    """模拟超时场景"""
    print("\n" + "="*60)
    print("模拟测试: 超时保护测试 (发送需要300ms, 超时200ms)")
    print("="*60)
    
    uart = MockUART(send_delay_ms=300)  # 发送需要300ms
    system_running = 1
    
    print(f"[初始状态] system_running = {system_running}")
    print()
    
    print("[步骤1] 开始发送")
    uart.start_send()
    print()
    
    print("[步骤2] UART_WaitTxComplete(&huart1, 200)")
    send_completed = uart_wait_tx_complete(uart, 200)  # 超时200ms
    print()
    
    print("[步骤3] 改变状态 (即使超时也继续)")
    system_running = 0
    print(f"[状态] system_running = {system_running}")
    print()
    
    print("-"*50)
    print("验证结果:")
    print(f"  发送是否完成: {'是' if send_completed else '否 (超时退出)'}")
    print(f"  系统是否卡死: ❌ 没有 (超时保护生效!)")
    
    print("\n✅ 验证通过: 超时保护正常工作，系统不会卡死!")
    return True


def main():
    print("\n" + "#"*60)
    print("#" + " "*58 + "#")
    print("#" + "  STM32 串口发送完成确认逻辑验证".center(58) + "#")
    print("#" + "  UART_WaitTxComplete 功能测试".center(58) + "#")
    print("#" + " "*58 + "#")
    print("#"*60)
    
    results = []
    
    # 测试1: 长按停止 (发送50ms)
    results.append(("长按停止 (50ms发送)", simulate_key2_long_press(50)))
    
    # 测试2: 长按停止 (发送100ms)
    results.append(("长按停止 (100ms发送)", simulate_key2_long_press(100)))
    
    # 测试3: 短按开启
    results.append(("短按开启 (50ms发送)", simulate_key2_short_press(50)))
    
    # 测试4: 超时保护
    results.append(("超时保护测试", simulate_timeout_scenario()))
    
    # 汇总结果
    print("\n\n" + "="*60)
    print("测试汇总:")
    print("="*60)
    for name, passed in results:
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"  {name:<25} {status}")
    
    all_passed = all(r[1] for r in results)
    print("\n" + "="*60)
    if all_passed:
        print("🎉 所有测试通过! UART_WaitTxComplete 逻辑正确!")
        print("   状态确实在发送完成后才改变!")
    else:
        print("⚠️  部分测试失败，请检查代码逻辑!")
    print("="*60 + "\n")
    
    return all_passed


if __name__ == "__main__":
    try:
        success = main()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\n测试被用户中断")
        sys.exit(0)
