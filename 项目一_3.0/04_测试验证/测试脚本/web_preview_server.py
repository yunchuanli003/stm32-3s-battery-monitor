#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
手机端网页的本地预览服务 —— 在没有 ESP8266 / PCB 的情况下先把界面跑起来。

用法：
    python web_preview_server.py            # 默认 8080
    python web_preview_server.py 9000

然后用手机浏览器打开（手机与电脑连同一 WiFi）：
    http://<电脑局域网IP>:8080

它做两件事：
    1. 把 02_固件代码/ESP8266/web/index.html 发给浏览器
    2. 提供 /api/data，每秒返回一条与固件同格式的 JSON

等 ESP8266 到货后，这个脚本就可以停掉 —— 板上跑同样的 HTML 和 /api/data。
"""

import json
import math
import os
import socket
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080

HERE = os.path.dirname(os.path.abspath(__file__))
INDEX = os.path.normpath(
    os.path.join(HERE, "..", "..", "02_固件代码", "ESP8266", "web", "index.html")
)

START = time.time()


def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def sensor_frame():
    """与 STM32 固件 WIFI_SendDataFrame() 同格式、同字段"""
    el = time.time() - START
    volt = 11.9 + 0.75 * math.sin(el / 25.0)
    temp = 27.0 + 6.0 * math.sin(el / 40.0)

    fan = 2 if temp > 32.0 else (1 if temp > 28.0 else 0)
    alarm = 1 if volt > 12.6 else (2 if volt < 9.0 else 0)

    return {
        "v": round(volt, 2),
        "t": round(temp, 1),
        "fan": fan,
        "alarm": alarm,
        "sim": 0,
        "ts": int(el),
    }


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = self.path.split("?")[0]

        if path in ("/", "/index.html"):
            try:
                with open(INDEX, "rb") as f:
                    body = f.read()
            except OSError:
                self.send_error(500, "找不到 index.html")
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return

        if path == "/api/data":
            body = json.dumps(sensor_frame(), separators=(",", ":")).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_error(404, "Not Found")

    def log_message(self, fmt, *args):
        msg = fmt % args
        if "/api/data" not in msg:
            print("[http]", msg, flush=True)


def main():
    if not os.path.isfile(INDEX):
        print("找不到页面文件：", INDEX)
        sys.exit(1)

    ip = local_ip()
    print("=" * 52)
    print("  网页预览服务已启动")
    print(f"  电脑上打开 : http://127.0.0.1:{PORT}")
    print(f"  手机上打开 : http://{ip}:{PORT}")
    print("=" * 52)
    print("  手机需与电脑连同一个 WiFi。按 Ctrl+C 退出")
    print()

    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[!] 已停止")
