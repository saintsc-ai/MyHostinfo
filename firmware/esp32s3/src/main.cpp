// ESP32-S3 + ST7789 화면 = 서버 상태 화면(hostinfo).
//
// 서버(server/hostinfo_serial.py)가 보내는 HOST·LIST·GEO 줄을 Wi-Fi(UDP 4210) 또는 USB 시리얼로 받아,
// 보드 상태 → 서버마다 서버 정보·Docker/VM 목록 → 날씨·시계를 번갈아 보인다(hostview.h).
//
// 배선(display.h): SCL→GPIO6, SDA→GPIO7, CS→15, DC→16, RES→17, BLK→18, VCC→3V3, GND. BOOT 버튼(GPIO0) = 보드 상태 30초.
//
// 시리얼(921600) 한 줄 명령:
//   CFG ...                   → 설정(net.h)
//   PING                      → PONG hostinfo-esp32s3 <화면 종류> <id>
//   NETS                      → 받고 있는 서버와 날씨
//   BL 0|1                    → 백라이트 시험(10분 유지)
//   HOST·LIST·GEO <...>       → USB 로 꽂힌 컴퓨터의 상태(hostinfo_serial.py 가 보냄, 응답 없음)
// 신뢰성: 하드웨어 워치독(loop 가 8초 멈추면 재시작).
#include <Arduino.h>
#include <esp_task_wdt.h>
#include "net.h"
#include "display.h"
#include "hostview.h"

static String line;
static uint32_t lastByte = 0;

static String ago(uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) return String(s) + "s";
  if (s < 3600) return String(s / 60) + "m";
  if (s < 86400) return String(s / 3600) + "h";
  return String(s / 86400) + "d " + String(s % 86400 / 3600) + "h";
}

static disp::Info dispInfo() {
  disp::Info d;
  uint32_t now = millis();
  d.id = net::cId;
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (net::cSsid.isEmpty()) d.wifi = "WiFi off (USB)";
  else d.wifi = wifi ? WiFi.SSID() + " " + String(WiFi.RSSI()) + "dBm" : String("WiFi down");
  d.ip = wifi ? WiFi.localIP().toString() : String("-");
  int n = 0;
  for (int i = 0; i < hv::SLOTS; i++)
    if (hv::hosts[i].at && now - hv::hosts[i].at < hv::STALE_MS) n++;
  d.srv = "servers " + String(n);
  d.wx = hv::wxOk() ? "weather " + String(hv::wx.t, 1) + "C" : String("weather -");
  d.mem = "RAM " + String(ESP.getFreeHeap() / 1024) + "K";
  d.up = "up " + ago(now);
  return d;
}

static void handleLine(String raw) {
  raw.trim();
  if (raw.isEmpty()) return;
  if (net::command(raw)) return;
  int sp = raw.indexOf(' ');
  String cmd = sp < 0 ? raw : raw.substring(0, sp);
  String rest = sp < 0 ? String("") : raw.substring(sp + 1);
  if (cmd == "PING") {
    net::outf("PONG hostinfo-esp32s3 %s %s", net::cDisp.isEmpty() ? "nodisp" : net::cDisp.c_str(), net::cId.c_str());
  } else if (cmd == "HOST") {            // USB 로 꽂힌 컴퓨터 상태: 필드를 | 로
    int n = 0;
    while (n < disp::LINES) {
      int bar = rest.indexOf('|');
      disp::host[n++] = bar < 0 ? rest : rest.substring(0, bar);
      if (bar < 0) break;
      rest = rest.substring(bar + 1);
    }
    for (int i = n; i < disp::LINES; i++) disp::host[i] = "";
    disp::hostSeen(millis());            // 응답은 하지 않는다(3초마다 오므로)
    hv::usbLine(raw);
  } else if (cmd == "LIST" || cmd == "GEO") {
    hv::usbLine(raw);
  } else if (cmd == "NETS") {
    String l = "NETS";
    for (int i = 0; i < hv::SLOTS; i++)
      if (hv::hosts[i].at)
        l += " " + hv::hosts[i].name + (i == hv::USB_SLOT ? "(usb)" : "") + ":" + String((millis() - hv::hosts[i].at) / 1000) +
             "s/" + String(hv::hosts[i].pages) + "p";
    l += hv::wxOk() ? " wx=" + String(hv::wx.t, 1) + "C," + hv::wxCity : String(" wx=-");
    net::outln(l);
  } else if (cmd == "BL") {
    int v = rest.toInt();
    pinMode(disp::PIN_BL, OUTPUT);
    digitalWrite(disp::PIN_BL, v ? HIGH : LOW);
    disp::lit = v != 0;
    disp::onUntil = millis() + 600000;
    net::outf("OK bl gpio%d=%d", disp::PIN_BL, v ? 1 : 0);
  } else {
    net::outln("ERR unknown command");
  }
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(921600);
  net::begin();
  esp_task_wdt_init(8, true);
  esp_task_wdt_add(nullptr);
  net::otaHook = []() {
    if (!disp::tft) return;
    disp::backlight(true);
    disp::tft->fillScreen(ST77XX_BLACK);
    disp::tft->setTextColor(ST77XX_YELLOW);
    disp::tft->setTextSize(2);
    disp::tft->setCursor(disp::padX, disp::topY);
    disp::tft->print("OTA update...");
  };
  net::outf("READY hostinfo-esp32s3 id=%s", net::cId.c_str());
  if (net::cDisp.length()) {
    bool d = disp::begin(net::cDisp);
    net::outf("DISP %s %s", net::cDisp.c_str(), d ? "ok" : "unknown model");
  }
  hv::begin(net::cId, net::cHinfo, net::cGeo, net::cHsrv);
}

void loop() {
  esp_task_wdt_reset();
  while (Serial.available()) {
    char c = Serial.read();
    lastByte = millis();
    if (c == '\n') { handleLine(line); line = ""; }
    else if (line.length() < 1000) line += c;   // LIST 줄이 600바이트쯤
  }
  if (line.length() && millis() - lastByte > 2000) line = "";   // 중간에 끊긴 줄은 버린다
  net::poll();
  hv::netTick();
  uint32_t now = millis();
  if (hv::on) hv::tick(now, []() { disp::drawBoard(dispInfo()); });
  else disp::tick(now, dispInfo);
}
