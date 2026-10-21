#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
模拟 ESP8266 的 TCP 数据源 —— 用于在没有硬件时开发 / 调试手机 App。

用法：
    python mock_esp_server.py            # 默认端口 8888
    python mock_esp_server.py 9000       # 指定端口

手机与电脑连同一个 WiFi，App 连 电脑局域网IP:8888 即可。
脚本启动会打印本机 IP，直接照着填。

数据格式与 STM32 固件 WIFI_SendDataFrame() 完全一致：
    {"v":12.35,"t":28.5,"fan":1,"alarm":0,"sim":0,"ts":123}
每行一条，行尾 \\n，1 Hz。
"""

import json
import math
import socket
import sys
import threading
import time

HOST = "0.0.0.0"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8888


def local_ip():
    """取本机在局域网中的 IP（不会真的发包）"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def build_frame(elapsed):
    """用正弦波模拟一条缓慢变化的电压/温度曲线，便于观察 App 刷新"""
    volt = 11.9 + 0.75 * math.sin(elapsed / 25.0)          # 11.15 ~ 12.65 V
    temp = 27.0 + 6.0 * math.sin(elapsed / 40.0)           # 21 ~ 33 ℃

    if temp > 32.0:
        fan = 2
    elif temp > 28.0:
        fan = 1
    else:
        fan = 0

    if volt > 12.6:
        alarm = 1          # 过压
    elif volt < 9.0:
        alarm = 2          # 欠压
    else:
        alarm = 0

    return {
        "v": round(volt, 2),
        "t": round(temp, 1),
        "fan": fan,
        "alarm": alarm,
        "sim": 0,
        "ts": int(elapsed),
    }


def serve(conn, addr):
    print(f"[+] 客户端接入 {addr}")
    start = time.time()
    try:
        while True:
            frame = build_frame(time.time() - start)
            line = json.dumps(frame, ensure_ascii=False, separators=(",", ":")) + "\n"
            conn.sendall(line.encode("utf-8"))
            print(f"    -> {line.strip()}", flush=True)
            time.sleep(1.0)
    except (BrokenPipeError, ConnectionResetError):
        print(f"[-] 客户端断开 {addr}")
    finally:
        conn.close()


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(1)

    print("=" * 52)
    print("  模拟 ESP 数据源已启动")
    print(f"  监听端口 : {PORT}")
    print(f"  电脑 IP  : {local_ip()}")
    print(f"  App 填这个: {local_ip()}:{PORT}")
    print("=" * 52)
    print("  保持本窗口运行，按 Ctrl+C 退出")
    print()

    try:
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=serve, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        print("\n[!] 已停止")
    finally:
        srv.close()


if __name__ == "__main__":
    main()
