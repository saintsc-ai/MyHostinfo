// 컴퓨터 상태 화면(ESP8266 SmallTV 펌웨어와 같은 그림).
//
// 받는 것(server/hostinfo_serial.py 가 보냄):
//   USB 시리얼: HOST·LIST·GEO 줄(main.cpp 가 넘겨줌) — 꽂힌 컴퓨터 하나만 보인다.
//   Wi-Fi: UDP 4210 브로드캐스트·직접. 5초마다 서버 주소로 'DISPLAY <id>'(UDP 4211)를 알려 직접 받는다.
// 화면 순서: 보드 상태 5초 → 서버마다 서버 정보 8초 → 목록 쪽마다 6초 → 날씨 5초 → 처음으로.
//   BOOT 버튼은 보드 상태 화면 30초. 보일 것(서버·시계)이 있으면 백라이트를 켜 둔다.
// 날씨: 10분마다 Open-Meteo(평문 HTTP, 키 없음)를 따로 작업(task)에서 받는다(화면 그리기를 막지 않게).
//   위치는 CFG geo <위도,경도,이름> → 같은 LAN 서버의 GEO 줄 → IP 로 대략(ip-api.com, 통신사에 따라 다른 동네가 나올 수 있음). CFG geo off 면 끔.
// 끄기: CFG hinfo off (예전처럼 보드 상태 + 버튼 30초). 2.25" 막대는 예전 방식 그대로.
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <time.h>
#include "display.h"

namespace hv {

static const int NET_MAX = 8, USB_SLOT = NET_MAX, SLOTS = NET_MAX + 1, MAX_PAGES = 4, UDP_PORT = 4210, ANN_PORT = 4211;
static const uint32_t STALE_MS = 20000, LIST_TTL = 60000, BOARD_MS = 5000, HOST_MS = 8000, LIST_MS = 6000, WX_MS = 5000,
                      BTN_MS = 30000;
// DISPLAY 를 알릴 서버 기본 목록(브로드캐스트에 더해). 빌드 때 -DHOSTINFO_SERVERS='"192.168.0.10"' 처럼 넣을 수 있다.
#ifndef HOSTINFO_SERVERS
#define HOSTINFO_SERVERS ""
#endif
static const char *DEFAULT_SERVERS = HOSTINFO_SERVERS;

static bool on = false;
static String geoCfg, servers, devId, hostGeo;

struct Host { String name, line, page[MAX_PAGES]; uint32_t at = 0, seq = 0, listAt = 0; uint8_t pages = 0; };
static Host hosts[SLOTS];

static Adafruit_ST7789 &T() { return *disp::tft; }

static const uint16_t C_GREEN = 0x07E8, C_YELLOW = 0xFFE0, C_RED = 0xF8A2, C_GRAY = 0x8410,
                      C_DIM = 0x2104, C_SKY = 0x5D9F, C_HEAD = 0x1A6F, C_ORANGE = 0xFC60;

static uint16_t levelColor(int pct) { return pct < 60 ? C_GREEN : pct < 85 ? C_YELLOW : C_RED; }

static void text(int x, int y, const String &s, uint16_t color) {
  T().setTextColor(color);
  T().setCursor(x, y);
  T().print(s);
}

// --- 받은 값 ----------------------------------------------------------------------
static String hostName(const String &s) {
  int bar = s.indexOf('|');
  return s.substring(5, bar < 0 ? s.length() : bar);
}

static int slotFor(const String &name, bool create) {
  int slot = -1, old = 0;
  for (int i = 0; i < NET_MAX; i++) {
    if (hosts[i].at && hosts[i].name == name) return i;
    if (!hosts[i].at && slot < 0) slot = i;
  }
  if (!create) return -1;
  if (slot < 0) {
    for (int i = 1; i < NET_MAX; i++) if (hosts[i].at < hosts[old].at) old = i;
    slot = old;
    hosts[slot].pages = 0;
  }
  return slot;
}

static void storeHost(int slot, const String &s) {
  if (slot < 0) return;
  Host &h = hosts[slot];
  String name = hostName(s);
  if (h.name != name) h.pages = 0;
  h.name = name;
  h.line = s;
  h.at = millis();
  h.seq++;
}

static void storeList(int slot, const String &s) {
  if (slot < 0) return;
  Host &h = hosts[slot];
  int a = s.indexOf('|'), sl = s.indexOf('/', a), b = s.indexOf('|', a + 1);
  if (a < 0 || sl < 0 || b < 0) return;
  int k = s.substring(a + 1, sl).toInt(), n = s.substring(sl + 1, b).toInt();
  if (k < 1 || n < 1 || k > MAX_PAGES) return;
  if (n > MAX_PAGES) n = MAX_PAGES;
  h.pages = n;
  h.page[k - 1] = s.substring(b + 1);
  h.listAt = millis();
}

// --- 날씨 (따로 작업에서) -------------------------------------------------------------
struct Wx { float t = 0, tmin = 0, tmax = 0; int rh = -1, code = -1; bool day = true; uint32_t at = 0; };
static Wx wx;
static String wxCity, wxLatLon;
static SemaphoreHandle_t wxLock = nullptr;
static volatile bool wxReset = false;
static const uint32_t WX_EVERY = 600000, WX_RETRY = 60000, WX_MAX_AGE = 3 * 3600000UL;

static bool wxOk() { return wx.at && millis() - wx.at < WX_MAX_AGE; }

static String httpGet(const String &url) {
  WiFiClient c;
  HTTPClient h;
  String r;
  h.setTimeout(5000);
  if (h.begin(c, url)) {
    if (h.GET() == 200) r = h.getString();
    h.end();
  }
  return r;
}

static bool jnum(const String &s, int from, const char *key, float &out) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k, from);
  if (i < 0) return false;
  i += k.length();
  if (s[i] == '[') i++;
  String v = s.substring(i, i + 12);
  if (!v.length() || !(isDigit(v[0]) || v[0] == '-')) return false;
  out = v.toFloat();
  return true;
}

static void wxTask(void *) {
  String lat, lon, city;
  for (;;) {
    uint32_t wait = WX_RETRY;
    if (wxReset) { wxReset = false; lat = ""; }
    String geo;
    xSemaphoreTake(wxLock, portMAX_DELAY);
    geo = geoCfg.length() ? geoCfg : hostGeo;
    xSemaphoreGive(wxLock);
    if (geo == "off" || WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(wait)); continue; }
    if (!lat.length()) {
      if (geo.length()) {
        int a = geo.indexOf(','), b = geo.indexOf(',', a + 1);
        if (a > 0) {
          lat = geo.substring(0, a);
          lon = b < 0 ? geo.substring(a + 1) : geo.substring(a + 1, b);
          city = b < 0 ? String("") : geo.substring(b + 1);
        }
      } else {
        String r = httpGet("http://ip-api.com/json/?fields=status,lat,lon,city");
        float la, lo;
        if (r.indexOf("success") >= 0 && jnum(r, 0, "lat", la) && jnum(r, 0, "lon", lo)) {
          lat = String(la, 3);
          lon = String(lo, 3);
          int c = r.indexOf("\"city\":\"");
          city = c >= 0 ? r.substring(c + 8, r.indexOf('"', c + 8)) : String("");
        }
      }
      lat.trim(); lon.trim(); city.trim();
    }
    if (lat.length()) {
      String r = httpGet("http://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon +
                         "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
                         "&daily=temperature_2m_max,temperature_2m_min&timezone=Asia%2FSeoul&forecast_days=1");
      int cur = r.indexOf("\"current\":{"), day = r.indexOf("\"daily\":{");
      float t, rh, code, isday, mx, mn;
      if (cur >= 0 && day >= 0 && jnum(r, cur, "temperature_2m", t) && jnum(r, cur, "relative_humidity_2m", rh) &&
          jnum(r, cur, "weather_code", code) && jnum(r, cur, "is_day", isday) &&
          jnum(r, day, "temperature_2m_max", mx) && jnum(r, day, "temperature_2m_min", mn)) {
        xSemaphoreTake(wxLock, portMAX_DELAY);
        wx.t = t; wx.rh = (int)rh; wx.code = (int)code; wx.day = isday > 0.5; wx.tmax = mx; wx.tmin = mn;
        wx.at = millis();
        wxCity = city;
        wxLatLon = lat + "," + lon;
        xSemaphoreGive(wxLock);
        wait = WX_EVERY;
      }
    }
    for (uint32_t w = 0; w < wait && !wxReset; w += 1000) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

static void setHostGeo(String g) {
  if (!on) return;
  g.trim();
  if (g.length() > 40 || g.indexOf(',') < 0 || g == hostGeo) return;
  xSemaphoreTake(wxLock, portMAX_DELAY);
  hostGeo = g;
  xSemaphoreGive(wxLock);
  if (!geoCfg.length()) { wxReset = true; wx.at = 0; }
}

// --- 시계 -------------------------------------------------------------------------
static String clockStr;   // 서버가 보낸 "2026-10-20 Tue 14:32"
static bool ntpOk() { return time(nullptr) > 1700000000; }

static String nowClock(bool stale) {
  if (!stale && clockStr.length()) return clockStr;
  if (!ntpOk()) return "";
  time_t t = time(nullptr);
  char b[24];
  strftime(b, sizeof b, "%Y-%m-%d %a %H:%M", localtime(&t));
  return b;
}

// --- 그리기 -----------------------------------------------------------------------
static String field[disp::LINES], shown[disp::LINES], clockShown, wxShown, listShown;
static int nFields = 0;

static int pctOf(const String &s) {
  if (s.startsWith("RAM ")) {
    int sl = s.indexOf('/');
    float u = s.substring(4, sl).toFloat(), t = s.substring(sl + 1).toFloat();
    return t > 0 ? (int)(100 * u / t + 0.5) : -1;
  }
  int sp = s.indexOf(' '), pc = s.indexOf('%');
  return (sp > 0 && pc > sp) ? s.substring(sp + 1, pc).toInt() : -1;
}

static void fancy(int i, const String &s) {
  if (shown[i] == s) return;
  shown[i] = s;
  int y = disp::topY + i * disp::lineH, x = disp::padX, w = T().width(), ts = disp::textSize, cw = 6 * ts;
  T().fillRect(0, y - 3, w, disp::lineH, ST77XX_BLACK);
  T().setTextSize(ts);
  if (!s.length()) return;
  if (i == 0) {
    T().fillRoundRect(disp::cornerX - 6, y - 3, w - 2 * (disp::cornerX - 6), ts * 8 + 6, 6, C_HEAD);
    int tab = s.indexOf('\t');
    text(disp::cornerX, y, tab < 0 ? s : s.substring(0, tab), ST77XX_WHITE);
    if (tab >= 0) {
      String r = s.substring(tab + 1);
      text(w - disp::cornerX - cw * r.length(), y, r, r.startsWith("!") ? C_RED : C_GREEN);
    }
    return;
  }
  bool cpu = s.startsWith("CPU ") || s.startsWith("GPU "), ram = s.startsWith("RAM "), dsk = s.startsWith("DISK ");
  if (cpu || ram || dsk) {
    int pct = pctOf(s), sp = s.indexOf(' ');
    String label = s.substring(0, sp), rest = s.substring(sp), cnt;
    text(x, y, label, C_SKY);
    if (s.startsWith("CPU ") && rest.endsWith("t")) {   // 끝의 코어·스레드 "20c 40t" 는 따로(회색)
      int b = rest.lastIndexOf(' '), a = rest.lastIndexOf(' ', b - 1);
      if (a > 0 && rest.charAt(b - 1) == 'c') { cnt = rest.substring(a); rest = rest.substring(0, a); }
    }
    if (cnt.length()) text(x + (label.length() + rest.length()) * cw, y, cnt, C_GRAY);
    if (cpu && rest.indexOf("C") > 0) {
      int t = rest.indexOf(' ', rest.indexOf('%'));
      String load = rest.substring(0, t), tc = rest.substring(t);
      int deg = tc.toInt();
      text(x + label.length() * cw, y, load, levelColor(pct));
      text(x + (label.length() + load.length()) * cw, y, tc, deg < 60 ? C_GREEN : deg < 75 ? C_YELLOW : C_RED);
    } else {
      text(x + label.length() * cw, y, rest, ST77XX_WHITE);
    }
    if (pct >= 0) {
      int by = y + ts * 8 + 2, bh = ts == 2 ? 5 : 3, bw = w - 2 * x;
      T().fillRect(x, by, bw, bh, C_DIM);
      T().fillRect(x, by, bw * min(pct, 100) / 100, bh, levelColor(pct));
    }
    return;
  }
  if (s.startsWith("VM ")) {
    int sl = s.indexOf('/');
    int run = s.substring(3, sl).toInt(), all = s.substring(sl + 1).toInt();
    text(x, y, "VM", C_SKY);
    text(x + 2 * cw, y, s.substring(2), run == all ? C_GREEN : run ? C_YELLOW : C_RED);
    return;
  }
  if (s.startsWith("up ")) { text(x, y, s, C_GRAY); return; }
  text(x, y, s, i == 1 ? C_ORANGE : ST77XX_WHITE);
}

static void drawClock(int top) {
  String clk = nowClock(false);
  if (clk == clockShown) return;
  clockShown = clk;
  int w = T().width(), h = T().height(), free = h - top - 6;
  T().fillRect(0, top, w, h - top, ST77XX_BLACK);
  if (free < 20 || !clk.length()) return;
  T().drawFastHLine(disp::cornerX, top + 2, w - 2 * disp::cornerX, C_DIM);
  int sp = clk.lastIndexOf(' ');
  String hm = clk.substring(sp + 1), date = clk.substring(0, sp);
  int big = free >= 70 ? 5 : free >= 50 ? 4 : 3, small = free >= 50 ? 2 : 1;
  int bh = 8 * big, gap = 6, y0 = top + 4 + (free - bh - gap - 8 * small) / 2;
  T().setTextSize(big);
  text((w - 6 * big * hm.length()) / 2, y0, hm, C_YELLOW);
  T().setTextSize(small);
  text((w - 6 * small * date.length()) / 2, y0 + bh + gap, date, C_SKY);
}

static void parseHost(const Host &h) {
  String rest = h.line.substring(5);
  int n = 0;
  clockStr = "";
  while (rest.length()) {
    int bar = rest.indexOf('|');
    String f = bar < 0 ? rest : rest.substring(0, bar);
    if (f.startsWith("@")) clockStr = f.substring(1);
    else if (n < disp::LINES) field[n++] = f;
    if (bar < 0) break;
    rest = rest.substring(bar + 1);
  }
  nFields = n;
  for (int i = n; i < disp::LINES; i++) field[i] = "";
  if (h.pages && h.page[0].length() && millis() - h.listAt < LIST_TTL) {
    const String &p = h.page[0];
    int b1 = p.indexOf('|'), b2 = p.indexOf('|', b1 + 1), b3 = p.indexOf('|', b2 + 1);
    String sum = b2 >= 0 ? p.substring(b2 + 1, b3 < 0 ? p.length() : b3) : String("");
    if (sum.length() && n) field[0] += "\t" + sum;
  }
}

static void drawHost() {
  for (int i = 0; i < nFields; i++) fancy(i, field[i]);
  drawClock(disp::topY + nFields * disp::lineH);
}

static void drawList(int slot, int pg) {
  Host &h = hosts[slot];
  const String &p = h.page[pg];
  String key = String(slot) + ":" + String(pg) + ":" + p;
  if (key == listShown) return;
  listShown = key;
  int w = T().width(), H = T().height();
  T().fillScreen(ST77XX_BLACK);
  String part[3];
  int at = 0;
  for (int i = 0; i < 3; i++) {
    int b = p.indexOf('|', at);
    part[i] = b < 0 ? p.substring(at) : p.substring(at, b);
    at = b < 0 ? p.length() : b + 1;
  }
  String title = part[0];
  if (h.pages > 1) title += " " + String(pg + 1) + "/" + String(h.pages);
  shown[0] = "";
  fancy(0, title + (part[1].length() ? "\t" + part[1] : String("")));
  T().setTextSize(2);
  int y = disp::topY + 30;
  while (at < (int)p.length() && y + 22 <= H) {
    int b = p.indexOf('|', at);
    String row = b < 0 ? p.substring(at) : p.substring(at, b);
    at = b < 0 ? p.length() : b + 1;
    if (!row.length()) continue;
    if (row[0] == '#') {
      String g = row.substring(1);
      T().setTextSize(1);
      text(8, y + 4, g, C_GRAY);
      T().drawFastHLine(8 + 6 * g.length() + 4, y + 7, w - 6 - (8 + 6 * g.length() + 4), C_DIM);
      T().setTextSize(2);
      y += 14;
      continue;
    }
    char st = row[0];
    int t1 = row.indexOf('~'), t2 = row.indexOf('~', t1 + 1);
    String name = row.substring(1, t1 < 0 ? row.length() : t1);
    String val = t1 < 0 ? String("") : row.substring(t1 + 1, t2 < 0 ? row.length() : t2);
    int mem = t2 < 0 || t2 + 1 >= (int)row.length() ? -1 : row.substring(t2 + 1).toInt();
    uint16_t col = st == 'g' ? C_GREEN : st == 'y' ? C_YELLOW : st == 'r' ? C_RED : C_GRAY;
    T().fillCircle(10, y + 7, 4, col);
    int room = (w - 6 - 12 * (int)val.length() - 6 - 20) / 12;
    if ((int)name.length() > room) name = name.substring(0, room);
    text(20, y, name, st == 'o' ? C_GRAY : ST77XX_WHITE);
    text(w - 6 - 12 * val.length(), y, val, st == 'r' ? C_RED : val.endsWith("%") ? levelColor(val.toInt()) : C_GRAY);
    if (mem >= 0) {
      int bw = w - 26;
      T().fillRect(20, y + 18, bw, 3, C_DIM);
      T().fillRect(20, y + 18, bw * min(mem, 100) / 100, 3, C_SKY);
    }
    y += 26;
  }
}

static int wxKind(int c) {
  if (c <= 0) return 0;
  if (c <= 2) return 1;
  if (c == 3) return 2;
  if (c == 45 || c == 48) return 3;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return 5;
  if (c >= 95) return 6;
  if (c >= 51) return 4;
  return 2;
}
static const char *WX_NAME[] = {"Clear", "Partly", "Cloudy", "Fog", "Rain", "Snow", "Storm"};

static void sun(int cx, int cy, int r) {
  for (int a = 0; a < 8; a++) {
    float an = a * PI / 4, c = cos(an), s = sin(an);
    for (int d = -1; d <= 1; d++)
      T().drawLine(cx + c * (r + 4) + d * s, cy + s * (r + 4) - d * c, cx + c * (r + 9) + d * s, cy + s * (r + 9) - d * c, C_YELLOW);
  }
  T().fillCircle(cx, cy, r, C_YELLOW);
}

static void moon(int cx, int cy, int r) {
  T().fillCircle(cx, cy, r, C_YELLOW);
  T().fillCircle(cx + r / 2, cy - r / 3, r - 1, ST77XX_BLACK);
}

static void cloud(int x, int y, uint16_t c) {
  T().fillCircle(x + 16, y + 18, 12, c);
  T().fillCircle(x + 32, y + 13, 13, c);
  T().fillCircle(x + 45, y + 21, 10, c);
  T().fillRoundRect(x + 4, y + 18, 52, 14, 7, c);
}

static void wxIcon(int x, int y, int k, bool day) {
  const uint16_t LIGHT = 0xC618;
  switch (k) {
    case 0: if (day) sun(x + 32, y + 32, 14); else moon(x + 32, y + 32, 17); break;
    case 1:
      if (day) sun(x + 42, y + 20, 10); else moon(x + 44, y + 18, 12);
      cloud(x + 2, y + 26, ST77XX_WHITE);
      break;
    case 2: cloud(x + 12, y + 8, C_GRAY); cloud(x + 2, y + 22, LIGHT); break;
    case 3: for (int i = 0; i < 5; i++) T().fillRoundRect(x + 4 + (i % 2) * 8, y + 10 + i * 10, 48, 5, 2, LIGHT); break;
    case 4:
      cloud(x + 4, y + 6, LIGHT);
      for (int i = 0; i < 4; i++)
        for (int d = 0; d < 2; d++) T().drawLine(x + 16 + i * 11 + d, y + 44, x + 11 + i * 11 + d, y + 58, C_SKY);
      break;
    case 5:
      cloud(x + 4, y + 6, LIGHT);
      for (int i = 0; i < 4; i++) T().fillCircle(x + 14 + i * 11, y + 47 + (i % 2) * 7, 3, ST77XX_WHITE);
      break;
    default:
      cloud(x + 4, y + 6, C_GRAY);
      T().fillTriangle(x + 34, y + 36, x + 22, y + 52, x + 32, y + 52, C_YELLOW);
      T().fillTriangle(x + 30, y + 50, x + 40, y + 50, x + 26, y + 64, C_YELLOW);
      break;
  }
}

static void drawWeather() {
  String clk = nowClock(true);
  Wx w;
  String city;
  xSemaphoreTake(wxLock, portMAX_DELAY);
  w = wx;
  city = wxCity;
  xSemaphoreGive(wxLock);
  bool ok = w.at && millis() - w.at < WX_MAX_AGE;
  String key = clk + "|" + String(ok ? w.at : 0);
  if (key == wxShown) return;
  wxShown = key;
  int W = T().width(), oy = (T().height() - 240) / 2;
  T().fillScreen(ST77XX_BLACK);
  if (!ok) {
    if (!clk.length()) return;
    int sp = clk.lastIndexOf(' ');
    String hm = clk.substring(sp + 1), date = clk.substring(0, sp);
    T().setTextSize(7);
    text((W - 42 * hm.length()) / 2, oy + 72, hm, C_YELLOW);
    T().setTextSize(2);
    text((W - 12 * date.length()) / 2, oy + 146, date, ST77XX_WHITE);
    return;
  }
  T().setTextSize(2);
  if (!city.length()) city = "Weather";
  if (city.length() > 19) city = city.substring(0, 19);
  text((W - 12 * city.length()) / 2, oy + 8, city, C_SKY);
  if (clk.length()) {
    int sp = clk.lastIndexOf(' ');
    String hm = clk.substring(sp + 1), date = clk.substring(0, sp);
    T().setTextSize(6);
    text((W - 36 * hm.length()) / 2, oy + 34, hm, C_YELLOW);
    T().setTextSize(2);
    text((W - 12 * date.length()) / 2, oy + 90, date, ST77XX_WHITE);
  }
  T().drawFastHLine(8, oy + 116, W - 16, C_DIM);
  int k = wxKind(w.code);
  wxIcon(8, oy + 126, k, w.day);
  T().setTextSize(2);
  int nx = 8 + (64 - 12 * (int)strlen(WX_NAME[k])) / 2;
  text(nx < 2 ? 2 : nx, oy + 198, WX_NAME[k], ST77XX_WHITE);
  String t = String(w.t, 1);
  if (t.length() > 4) t = String(lround(w.t));
  uint16_t tc = w.t < 0 ? C_SKY : w.t < 25 ? ST77XX_WHITE : w.t < 30 ? C_ORANGE : C_RED;
  int rx = 92;
  T().setTextSize(4);
  text(rx, oy + 128, t, tc);
  int ex = rx + 24 * t.length();
  T().drawCircle(ex + 5, oy + 132, 4, tc);
  T().drawCircle(ex + 5, oy + 132, 3, tc);
  text(ex + 12, oy + 128, "C", tc);
  T().setTextSize(2);
  text(rx, oy + 172, "RH", C_SKY);
  text(rx + 36, oy + 172, String(w.rh) + "%", ST77XX_WHITE);
  text(rx, oy + 198, String(lround(w.tmin)) + "~" + String(lround(w.tmax)) + "C", C_GRAY);
}

// --- Wi-Fi 받기 -------------------------------------------------------------------
static WiFiUDP udp;
static bool udpOn = false, ntpSet = false;

static void netTick() {
  if (!on || WiFi.status() != WL_CONNECTED) return;
  if (!ntpSet) { configTzTime("KST-9", "time.bora.net", "kr.pool.ntp.org", "time.google.com"); ntpSet = true; }
  if (!udpOn) udpOn = udp.begin(UDP_PORT);
  static uint32_t lastAnn = 0;
  if (!lastAnn || millis() - lastAnn > 5000) {
    lastAnn = millis();
    String l = servers + ",255.255.255.255";
    while (l.length()) {
      int c = l.indexOf(',');
      String one = c < 0 ? l : l.substring(0, c);
      IPAddress ip;
      if (ip.fromString(one)) {
        udp.beginPacket(ip, ANN_PORT);
        udp.print("DISPLAY " + devId + "\n");
        udp.endPacket();
      }
      if (c < 0) break;
      l = l.substring(c + 1);
    }
  }
  static char b[700];
  for (int k = 0; k < 6; k++) {
    int n = udp.parsePacket();
    if (n <= 0) return;
    int len = udp.read(b, sizeof b - 1);
    if (len <= 0) continue;
    b[len] = 0;
    String s(b);
    s.trim();
    if (s.startsWith("HOST ")) storeHost(slotFor(hostName(s), true), s);
    else if (s.startsWith("LIST ")) storeList(slotFor(hostName(s), false), s);
    else if (s.startsWith("GEO ")) setHostGeo(s.substring(4));
  }
}

// main.cpp 의 USB 줄(HOST·LIST·GEO)
static void usbLine(const String &s) {
  if (s.startsWith("HOST ")) storeHost(USB_SLOT, s);
  else if (s.startsWith("LIST ")) storeList(USB_SLOT, s);
  else if (s.startsWith("GEO ")) setHostGeo(s.substring(4));
}

// --- 2.25" 가로 막대(284×76, disp::compact) -----------------------------------------
// 세로 화면 그림이 76px 에 안 들어가 따로 그린다. 순서·시간은 같다(서버 → 목록 → 날씨, BOOT = 보드 상태).
static String cmpShown;

static void cmpBar(int x, int y, int w, int h, int pct, uint16_t c) {
  T().fillRect(x, y, w, h, C_DIM);
  if (pct > 0) T().fillRect(x, y, w * min(pct, 100) / 100, h, c);
}

static String cut(const String &s, int n) { return (int)s.length() > n ? s.substring(0, n) : s; }

// 제목줄(크게): 왼쪽 이름, 오른쪽 시:분 또는 요약
static void cmpHead(const String &name, const String &right, uint16_t rc) {
  int W = T().width();
  T().fillRoundRect(0, 0, W, 18, 4, C_HEAD);
  T().setTextSize(2);
  int room = (W - 4 - 12 * (int)right.length() - 8) / 12;
  text(4, 1, cut(name, room), ST77XX_WHITE);
  text(W - 4 - 12 * right.length(), 1, right, rc);
}

// 서버: 제목 · IP·켜진 시간·VM · CPU/RAM/DISK/GPU 줄마다 글자 + 오른쪽 막대
static void cmpHost(const Host &h) {
  String clk = nowClock(false);
  String key = h.line + "|" + clk;
  if (key == cmpShown) return;
  cmpShown = key;
  T().fillScreen(ST77XX_BLACK);
  int W = T().width();
  String sum;   // 목록 요약(D 11/11 · !1 …)
  if (h.pages && h.page[0].length() && millis() - h.listAt < LIST_TTL) {
    const String &p = h.page[0];
    int b1 = p.indexOf('|'), b2 = p.indexOf('|', b1 + 1), b3 = p.indexOf('|', b2 + 1);
    if (b2 >= 0) sum = p.substring(b2 + 1, b3 < 0 ? p.length() : b3);
  }
  String hm = clk.length() ? clk.substring(clk.lastIndexOf(' ') + 1) : String("");
  bool bad = sum.startsWith("!");
  cmpHead(field[0].length() ? field[0].substring(0, field[0].indexOf('\t') < 0 ? field[0].length() : field[0].indexOf('\t')) : String("?"),
          bad ? sum : hm, bad ? C_RED : C_YELLOW);
  T().setTextSize(1);
  String info = nFields > 1 ? field[1] : String("");
  int y = 21;
  for (int i = 2; i < nFields; i++) {
    if (field[i].startsWith("VM ") || field[i].startsWith("up ")) info += "  " + field[i];
  }
  if (sum.length() && !bad) info += "  " + sum;
  text(2, y, cut(info, 47), C_ORANGE);
  y = 31;
  for (int i = 2; i < nFields && y + 8 <= T().height(); i++) {
    const String &s = field[i];
    bool cpu = s.startsWith("CPU ") || s.startsWith("GPU ");
    if (!cpu && !s.startsWith("RAM ") && !s.startsWith("DISK ")) continue;
    int pct = pctOf(s);
    int sp = s.indexOf(' ');
    text(2, y, s.substring(0, sp), C_SKY);
    text(2 + 6 * (sp + 1), y, cut(s.substring(sp + 1), 22), ST77XX_WHITE);
    if (pct >= 0) cmpBar(150, y + 1, W - 152, 6, pct, levelColor(pct));
    y += 11;
  }
}

// 목록: 제목 + 줄마다 상태 점 · 이름 · 값(작은 글자 두 칸 × 5줄)
static void cmpList(int slot, int pg) {
  Host &h = hosts[slot];
  const String &p = h.page[pg];
  String key = String(slot) + ":" + String(pg) + ":" + p;
  if (key == cmpShown) return;
  cmpShown = key;
  T().fillScreen(ST77XX_BLACK);
  String part[3];
  int at = 0;
  for (int i = 0; i < 3; i++) {
    int b = p.indexOf('|', at);
    part[i] = b < 0 ? p.substring(at) : p.substring(at, b);
    at = b < 0 ? p.length() : b + 1;
  }
  String title = part[0] + (h.pages > 1 ? " " + String(pg + 1) + "/" + String(h.pages) : String(""));
  cmpHead(title, part[1], C_GREEN);
  T().setTextSize(1);
  int W = T().width(), col = 0, y = 22, colW = W / 2;
  while (at < (int)p.length()) {
    int b = p.indexOf('|', at);
    String row = b < 0 ? p.substring(at) : p.substring(at, b);
    at = b < 0 ? p.length() : b + 1;
    if (!row.length() || row[0] == '#') continue;   // 소제목은 좁아서 뺀다
    if (y + 8 > T().height()) {
      if (col) break;
      col = 1; y = 22;
    }
    char st = row[0];
    int t1 = row.indexOf('~'), t2 = row.indexOf('~', t1 + 1);
    String name = row.substring(1, t1 < 0 ? row.length() : t1);
    String val = t1 < 0 ? String("") : row.substring(t1 + 1, t2 < 0 ? row.length() : t2);
    uint16_t c = st == 'g' ? C_GREEN : st == 'y' ? C_YELLOW : st == 'r' ? C_RED : C_GRAY;
    int x = 2 + col * colW;
    T().fillCircle(x + 3, y + 3, 3, c);
    int room = (colW - 12 - 6 * (int)val.length() - 6) / 6;
    text(x + 9, y, cut(name, room), st == 'o' ? C_GRAY : ST77XX_WHITE);
    text(x + colW - 6 - 6 * val.length(), y, val, st == 'r' ? C_RED : val.endsWith("%") ? levelColor(val.toInt()) : C_GRAY);
    y += 11;
  }
}

// 날씨: 왼쪽 큰 시계·날짜·장소, 오른쪽 그림·기온·습도·최저/최고. 날씨가 없으면 시계만 크게.
static void cmpWeather() {
  String clk = nowClock(true);
  Wx w;
  String city;
  xSemaphoreTake(wxLock, portMAX_DELAY);
  w = wx;
  city = wxCity;
  xSemaphoreGive(wxLock);
  bool ok = w.at && millis() - w.at < WX_MAX_AGE;
  String key = clk + "|" + String(ok ? w.at : 0);
  if (key == cmpShown) return;
  cmpShown = key;
  T().fillScreen(ST77XX_BLACK);
  int W = T().width();
  if (!clk.length()) return;
  int sp = clk.lastIndexOf(' ');
  String hm = clk.substring(sp + 1), date = clk.substring(0, sp);
  if (!ok) {
    T().setTextSize(6);
    text((W - 36 * hm.length()) / 2, 4, hm, C_YELLOW);
    T().setTextSize(1);
    text((W - 6 * date.length()) / 2, 60, date, ST77XX_WHITE);
    return;
  }
  T().setTextSize(5);
  text(2, 4, hm, C_YELLOW);
  T().setTextSize(1);
  text(2, 48, date, ST77XX_WHITE);
  text(2, 62, cut(city.length() ? city : String("Weather"), 24), C_SKY);
  int k = wxKind(w.code);
  wxIcon(152, 6, k, w.day);   // 64×64
  String t = String(lround(w.t));
  uint16_t tc = w.t < 0 ? C_SKY : w.t < 25 ? ST77XX_WHITE : w.t < 30 ? C_ORANGE : C_RED;
  int rx = 220;
  T().setTextSize(3);
  text(rx, 6, t, tc);
  int ex = rx + 18 * t.length();
  T().drawCircle(ex + 4, 8, 3, tc);
  T().setTextSize(1);
  text(ex + 9, 6, "C", tc);
  text(rx, 36, "RH " + String(w.rh) + "%", ST77XX_WHITE);
  text(rx, 50, String(lround(w.tmin)) + "~" + String(lround(w.tmax)) + "C", C_GRAY);
  text(rx, 64, cut(WX_NAME[k], 10), C_GRAY);
}

// --- 화면 순서 ---------------------------------------------------------------------
enum { V_BOARD, V_HOST, V_LIST, V_WX };
static int vKind = V_BOARD, vSlot = -1, vPage = 0;
static uint32_t vSince = 0, vSeq = 0xFFFFFFFF, btnUntil = 0;

static bool alive(int i) { return hosts[i].at && millis() - hosts[i].at < STALE_MS; }
static bool candidate(int i) {
  if (!alive(i)) return false;
  if (i == USB_SLOT) return true;
  return !alive(USB_SLOT);   // USB 값이 오는 동안은 USB 만
}
static int listPages(int i) { return hosts[i].pages && millis() - hosts[i].listAt < LIST_TTL ? hosts[i].pages : 0; }
static bool anyCandidate() {
  for (int i = 0; i < SLOTS; i++) if (candidate(i)) return true;
  return false;
}

static void setView(int k, int slot, int page) {
  vSince = millis();
  if (k == vKind && slot == vSlot && page == vPage) return;
  vKind = k; vSlot = slot; vPage = page;
  vSeq = 0xFFFFFFFF;
  T().fillScreen(ST77XX_BLACK);
  for (auto &x : shown) x = "";
  for (auto &x : disp::shown) x = "";
  clockShown = wxShown = listShown = cmpShown = "";
}

static void schedule(uint32_t now) {
  if (btnUntil && (int32_t)(now - btnUntil) < 0) { setView(V_BOARD, -1, 0); return; }
  bool valid = vKind == V_BOARD || (vKind == V_HOST && candidate(vSlot)) ||
               (vKind == V_LIST && candidate(vSlot) && vPage < listPages(vSlot)) ||
               (vKind == V_WX && nowClock(true).length());
  uint32_t dur = vKind == V_BOARD ? BOARD_MS : vKind == V_HOST ? HOST_MS : vKind == V_LIST ? LIST_MS : WX_MS;
  if (valid && now - vSince < dur) return;
  if (valid && vKind == V_HOST && listPages(vSlot)) { setView(V_LIST, vSlot, 0); return; }
  if (valid && vKind == V_LIST && vPage + 1 < listPages(vSlot)) { setView(V_LIST, vSlot, vPage + 1); return; }
  int from = vKind == V_BOARD ? -1 : (vKind == V_HOST || vKind == V_LIST) ? vSlot : SLOTS;
  for (int i = from + 1; i < SLOTS; i++)
    if (candidate(i)) { setView(V_HOST, i, 0); return; }
  if (vKind != V_WX && vKind != V_BOARD && nowClock(true).length()) { setView(V_WX, -1, 0); return; }
  if (vKind == V_BOARD && !anyCandidate() && nowClock(true).length()) { setView(V_WX, -1, 0); return; }
  setView(V_BOARD, -1, 0);
}

static void begin(const String &id, const String &cfgOn, const String &geo, const String &srv) {
  on = disp::tft && cfgOn != "off";   // 2.25" 가로 막대도(cmp* 로 따로 그림)
  devId = id;
  geoCfg = geo;
  servers = srv.length() ? srv : String(DEFAULT_SERVERS);
  if (!on) return;
  wxLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(wxTask, "wx", 8192, nullptr, 1, nullptr, 0);
}

// loop 에서. board() = 예전 보드 상태 화면 한 번 그리기.
template <typename F>
static void tick(uint32_t now, F board) {
  static uint32_t last = 0;
  if (now - last < 200) return;
  last = now;
  if (digitalRead(disp::PIN_BTN) == LOW) { btnUntil = now + BTN_MS; disp::wake(now); }
  bool content = anyCandidate() || nowClock(true).length();
  if (content) disp::wake(now);   // 보일 것이 있으면 켜 둔다
  else if (disp::lit && disp::hasBl && (int32_t)(now - disp::onUntil) > 0) { disp::backlight(false); return; }
  if (!disp::lit && disp::hasBl) return;
  schedule(now);
  if (disp::compact && vKind != V_BOARD) {
    if (vKind == V_WX) cmpWeather();
    else if (vKind == V_LIST && vSlot >= 0 && vPage < hosts[vSlot].pages) cmpList(vSlot, vPage);
    else if (vKind == V_HOST && vSlot >= 0) {
      if (hosts[vSlot].seq != vSeq) { vSeq = hosts[vSlot].seq; parseHost(hosts[vSlot]); }
      cmpHost(hosts[vSlot]);
    }
    return;
  }
  if (vKind == V_WX) { drawWeather(); return; }
  if (vKind == V_LIST && vSlot >= 0 && vPage < hosts[vSlot].pages) { drawList(vSlot, vPage); return; }
  if (vKind == V_HOST && vSlot >= 0) {
    if (hosts[vSlot].seq != vSeq) { vSeq = hosts[vSlot].seq; parseHost(hosts[vSlot]); }
    drawHost();
    return;
  }
  static uint32_t lastBoard = 0;   // 보드 상태는 2초마다 바뀐 줄만
  if (now - lastBoard >= disp::EVERY_MS || disp::shown[0].isEmpty()) { lastBoard = now; board(); }
}

}  // namespace hv
