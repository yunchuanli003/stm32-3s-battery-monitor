/*
 * 3S 电池监测 —— ESP8266 数据转发（AP 热点 + 网页显示）
 * 项目：3S 锂电池电压温度监测与智能散热   2026-11-13
 *
 * ── 接线（四根）──────────────────────────────────
 *   板子 J6-1 (+5V)  →  NodeMCU  VIN
 *   板子 J6-2 (GND)  →  NodeMCU  GND
 *   板子 J6-3 (TX)   →  NodeMCU  RX      ← 数据从这根进来
 *   板子 J6-4 (RX)   →  NodeMCU  TX      ← 本程序回的 "PONG" 从这根出去
 *
 *   第四根（J6-4）是 2026-11-14 新增的，用来给 STM32 回心跳。
 *   STM32 屏幕上的"无线通 / 无线发"就靠它区分：
 *     只发不收  → 无线发
 *     收到 PONG → 无线通
 *   不接这根线，屏幕会一直显示"无线发"—— 那是真的，不是屏幕坏了。
 *
 *   这根线上有三样东西：ESP8266 的 TX（唯一输出）、板载 CH340 的 RXD（输入）、
 *   STM32 的 PB11（输入）。只有一个输出，不会打架，所以烧录时 J6-4 可以不拔。
 *
 * ── 用法 ─────────────────────────────────────────
 *   1. 手机连热点：BatteryMonitor / 12345678
 *   2. 浏览器打开：http://192.168.4.1
 *   3. 页面每秒自动刷新，显示电压 / 温度 / 风扇占空比
 *
 * ── 重要提醒 ─────────────────────────────────────
 *   烧录这个程序时，必须拔掉 J6-3。
 *   原因：板上 CH340 的 TXD 和 STM32 的 PB10 都接在 ESP8266 的 RX 上，
 *         烧录时 CH340 要往这根线发数据，两个推挽输出会顶在一起。
 *   烧完之后拔掉 USB 线，CH340 断电、TXD 变高阻，就不打架了。
 *
 * ── 烧录设置（Arduino IDE）───────────────────────
 *   开发板：NodeMCU 1.0 (ESP-12E Module)
 *   端口：  NodeMCU 插上后新出现的那个 COM 口
 *   上传失败提示 "Failed to connect" → 按住板上 FLASH 键再点上传
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define AP_SSID "BatteryMonitor"
#define AP_PASS "12345678"

ESP8266WebServer server(80);

float g_volt  = 0.0f;
float g_temp  = 0.0f;
int   g_fan   = 0;
bool  g_ntcOk = true;
bool  g_hasData = false;
unsigned long g_lastMs = 0;

char    rxBuf[64];
uint8_t rxLen = 0;

static const char PAGE[] =
"<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>3S 电池监测</title><style>"
"body{margin:0;background:#0f1216;color:#e6e9ee;font-family:-apple-system,'PingFang SC',sans-serif;"
"display:flex;align-items:center;justify-content:center;min-height:100vh}"
".c{width:320px;padding:26px 28px;border-radius:18px;background:#181d23;"
"border:1px solid #232a32}"
".t{font-size:13px;color:#8b949e;letter-spacing:2px}"
".v{font-size:54px;font-weight:300;margin:4px 0 20px;font-variant-numeric:tabular-nums}"
".r{display:flex;justify-content:space-between;align-items:center;"
"padding:11px 0;border-top:1px solid #232a32}"
".k{color:#8b949e;font-size:14px}.n{font-size:18px;font-variant-numeric:tabular-nums}"
".g{color:#4ade80}.y{color:#fbbf24}.d{color:#5b636e}"
"</style></head><body><div class=\"c\">"
"<div class=\"t\">电池电压</div>"
"<div class=\"v d\" id=\"v\">--</div>"
"<div class=\"r\"><span class=\"k\">温度</span><span class=\"n d\" id=\"tp\">--</span></div>"
"<div class=\"r\"><span class=\"k\">风扇占空比</span><span class=\"n d\" id=\"fn\">--</span></div>"
"<div class=\"r\"><span class=\"k\">数据链路</span><span class=\"n d\" id=\"st\">连接中</span></div>"
"</div><script>"
"function u(){fetch('/data').then(function(r){return r.json()}).then(function(d){"
"var v=document.getElementById('v');"
"v.textContent=d.v.toFixed(2)+' V';"
"v.className='v '+(d.v>=12.0&&d.v<=12.6?'g':(d.v<11.0?'y':'d'));"
"var t=document.getElementById('tp');"
"t.textContent=d.ok?d.t.toFixed(1)+' \\u00b0C':'探头异常';"
"t.className='n '+(d.ok?'g':'y');"
"document.getElementById('fn').textContent=d.f+' %';"
"document.getElementById('fn').className='n '+(d.f>0?'g':'d');"
"var s=document.getElementById('st');"
"s.textContent=d.live?'正常':'中断';"
"s.className='n '+(d.live?'g':'y');"
"}).catch(function(){});}"
"u();setInterval(u,1000);"
"</script></body></html>";

/* 从 "键:数值" 里取数值，取不到就返回默认值 */
float pick(const char *s, char key, float def)
{
  const char *p = strchr(s, key);
  if (!p) return def;
  p++;
  if (*p == ':') p++;
  return atof(p);
}

/* 解析一帧：V:12.48,T:8.1,F:30   （探头异常时是 T:NTC） */
void parseFrame(char *s)
{
  g_volt = pick(s, 'V', g_volt);
  g_fan  = (int)pick(s, 'F', g_fan);

  const char *pt = strstr(s, "T:");
  if (pt)
  {
    pt += 2;
    if (*pt == 'N')       // T:NTC —— 探头未插/断线
    {
      g_ntcOk = false;
    }
    else
    {
      g_ntcOk = true;
      g_temp  = atof(pt);
    }
  }

  g_hasData = true;
  g_lastMs  = millis();
}

void handleRoot()
{
  server.send(200, "text/html; charset=utf-8", PAGE);
}

void handleData()
{
  bool live = g_hasData && (millis() - g_lastMs < 3000);

  String j = "{\"v\":" + String(g_volt, 2) +
             ",\"t\":" + String(g_temp, 1) +
             ",\"f\":" + String(g_fan) +
             ",\"ok\":" + (g_ntcOk ? "true" : "false") +
             ",\"live\":" + (live ? "true" : "false") + "}";

  server.send(200, "application/json", j);
}

void setup()
{
  Serial.begin(115200);          // UART0：RX=GPIO3 接 STM32 的 J6-3
  Serial.setTimeout(20);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Serial.println();
  Serial.println("3S monitor bridge ready");
  Serial.print("AP: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void loop()
{
  server.handleClient();

  while (Serial.available())
  {
    char c = (char)Serial.read();

    if (c == '\n')
    {
      rxBuf[rxLen] = 0;
      if (rxLen > 0)
      {
        parseFrame(rxBuf);

        /* 回心跳：让 STM32 确知对端还在（2026-11-14 新增）
         * STM32 那边开了 USART3 接收中断，只认首字符 'P'，
         * 收到就在 6 秒窗口内把屏幕上的"无线发"改成"无线通"。
         * 这一行不怕多发：1 秒 5 字节，占用可以忽略。 */
        Serial.print("PONG\n");
      }
      rxLen = 0;
    }
    else if (c != '\r')
    {
      if (rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = c;
    }
  }
}
