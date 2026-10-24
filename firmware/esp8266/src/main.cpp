// USB 로 꽂은 컴퓨터의 상태를 보여 주는 화면(NodeMCU ESP8266 + ST7789).
//
// 컴퓨터 쪽 ../hostinfo_serial.py 이 3초마다 한 줄을 보낸다(115200):
//   HOST <호스트 이름>|<IP>|CPU ..|RAM ..|DISK ..|VM ..|up ..
// 다른 명령: DISP <154|169|114|200|225> (화면 종류, EEPROM 에 저장 후 다시 켬), PING,
//   BLINV 1|0 (백라이트가 LOW 일 때 켜지는 모듈이면 1, 저장 후 다시 켬), BL 0|1 (D6 직접 시험)
// 20초 넘게 안 오면 '응답 없음'을 띄우고, 5분 동안 안 오면 백라이트를 끈다.
//
// Wi-Fi(선택): CFG ssid home,office · CFG pass <비밀번호> · CFG ota <OTA 비밀번호> · CFG(보기) · CFG RESTART.
//   EEPROM 에만 저장(펌웨어·저장소에 비밀번호 없음). 붙으면 ① NTP 로 시계(USB 가 끊겨도 아래 시계가 맞게 감)
//   ② ArduinoOTA(포트 8266, OTA 비밀번호를 넣었을 때만). SSID 를 비우면(CFG ssid -) Wi-Fi 를 끈다.
//   ③ 서버들이 뿌리는 HOST 줄(UDP 4210 브로드캐스트, hostinfo_serial.py --broadcast)을 받아 보인다.
//      CFG show all(기본, 10초마다 번갈아) | CFG show <호스트 이름>. USB 로 값이 오는 동안은 USB 가 먼저.
//      무선 컨트롤러가 유선 브로드캐스트를 Wi-Fi 로 넘기지 않아, 5초마다 'DISPLAY <id>'를 UDP 4211 로 알리면
//      서버가 이 주소로 직접 보낸다.
//
//   ④ 날씨: 10분마다 Open-Meteo(키 없음, 평문 HTTP)에서 기온·습도·날씨·오늘 최저/최고를 받아,
//      서버 정보 10초 → 날씨·큰 시계 5초로 번갈아 보인다. 받는 서버가 없거나 20초 넘게 끊기면 날짜·시계·날씨만
//      (날씨를 못 받으면 날짜·시계만 크게, 백라이트 유지).
//      위치는 CFG geo <위도,경도[,이름]> · 비우면 같은 LAN 서버가 보내는 'GEO 위도,경도,이름'(hostinfo_serial.py --geo),
//      그것도 없으면 IP 로 대략(ip-api.com, 통신사에 따라 다른 동네가 나올 수 있음) · CFG geo off 로 끔.
//
// 목록: 서버가 'LIST <호스트>|<쪽>/<전체>|<제목>|<오른쪽>|<요약>|<줄>...' 도 보낸다(Docker 컨테이너·Proxmox VM).
//   줄 = <상태 g|y|r|o><이름>~<값>~<메모리 0-100>, '#이름' = 회색 소제목. 화면 순서는 서버마다 서버 정보 8초 → 목록 쪽마다 6초,
//   모든 서버를 돌면 날씨 5초. '요약'은 서버 정보 제목줄 오른쪽(예 'D 4/4', 문제가 있으면 '!'로 시작해 빨강).
//
// 배선(NodeMCU 표시 = GPIO): SCL→D5(14), SDA→D7(13), RES→D1(5), DC→D2(4), CS→D8(15), BLK→D6(12), VCC→3V3, GND.
//
// GeekMagic SmallTV(Smart Weather Clock, ESP8266 + 1.54" 240x240, pio env smalltv): USB-C 가 전원 전용이라 시리얼 명령을
//   못 받는다. Wi-Fi 설정이 없거나 3분 넘게 못 붙으면 열린 AP '<id>'를 띄우고 http://192.168.4.1 양식으로 설정한다
//   (Wi-Fi 에 붙은 뒤에는 보드 주소로 같은 양식, OTA 비밀번호를 넣었으면 저장할 때 그 비밀번호가 필요).
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <time.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#ifdef BOARD_SMALLTV
// 공개된 분해 기록 기준, 실물로 확인: 화면 CS 없음(SPI 모드 3), DC GPIO0, RST GPIO2, 백라이트 GPIO5(LOW 일 때 켜짐)
static const int PIN_CS = -1, PIN_DC = 0, PIN_RST = 2, PIN_BL = 5;
#else
static const int PIN_CS = 15, PIN_DC = 4, PIN_RST = 5, PIN_BL = 12;
#endif
static const int LINES = 9;
static const uint32_t STALE_MS = 20000, DARK_MS = 300000;

static Adafruit_ST7789 tft(PIN_CS, PIN_DC, PIN_RST);   // 하드웨어 SPI(HSPI: D5·D7)
static String model;
static bool compact = false, blActiveLow = false;
static int textSize = 2, lineH = 26, topY = 6, padX = 4, cornerX = 4;
static String field[LINES], shown[LINES];
static String clockStr, clockShown;   // "2026-10-20 Tue 01:23" — 아래 빈 공간에 큰 시계
static int nFields = 0;
static uint32_t lastHost = 0;
static bool lit = false;
static String buf;

static void backlight(bool on) {
  lit = on;
  digitalWrite(PIN_BL, on != blActiveLow ? HIGH : LOW);
}

static bool blInv = false;   // EEPROM 4번 바이트: 1 이면 백라이트가 LOW 일 때 켜짐(BLINV 1)

// EEPROM: 0~3 화면 종류, 4 BLINV, 16~95 SSID 목록(쉼표), 96~159 Wi-Fi 비밀번호, 160~191 OTA 비밀번호
// 256~383 서버 주소 목록(알림 보낼 곳, 비우면 기본 목록), 384~415 날씨 위치
static const int EE_SIZE = 512, EE_SSID = 16, EE_PASS = 96, EE_OTA = 160, EE_SHOW = 192, EE_SRV = 256, EE_GEO = 384;
// 보드가 '여기 있음'을 알릴 서버 주소 기본 목록(브로드캐스트에 더해). 빌드 때 -DHOSTINFO_SERVERS='"192.168.0.10,192.168.0.11"' 처럼 넣을 수 있다.
#ifndef HOSTINFO_SERVERS
#define HOSTINFO_SERVERS ""
#endif
static const char *DEFAULT_SERVERS = HOSTINFO_SERVERS;
static String cfgSsid, cfgPass, cfgOta, cfgShow, cfgServers, cfgGeo, devId;

// Wi-Fi 로 받은 서버들(UDP 4210). 20초 넘게 안 오면 목록에서 안 고른다.
// 0~7 = Wi-Fi 로 받은 서버, 8 = USB. USB 값이 오는 동안은 USB 서버만 보인다.
static const int NET_MAX = 8, USB_SLOT = NET_MAX, SLOTS = NET_MAX + 1, MAX_PAGES = 4, UDP_PORT = 4210;
static const uint32_t HOST_MS = 8000, LIST_MS = 6000, WX_MS = 5000, LIST_TTL = 60000;
struct Host { String name, line, page[MAX_PAGES]; uint32_t at = 0, seq = 0, listAt = 0; uint8_t pages = 0; };
static Host hosts[SLOTS];
static WiFiUDP udp;
static bool udpOn = false;
static ESP8266WiFiMulti wifiMulti;
static bool wifiOn = false, otaOn = false;

static String eeStr(int at, int len) {
  String r;
  for (int i = 0; i < len; i++) {
    uint8_t c = EEPROM.read(at + i);
    if (c == 0 || c == 0xFF) break;
    r += (char)c;
  }
  return r;
}

static void eeSet(int at, int len, const String &v) {
  for (int i = 0; i < len; i++) EEPROM.write(at + i, i < (int)v.length() ? v[i] : 0);
  EEPROM.commit();
}

static String loadModel() {
  EEPROM.begin(EE_SIZE);
  blInv = EEPROM.read(4) == 1;
  char b[5] = {0};
  for (int i = 0; i < 4; i++) b[i] = (char)EEPROM.read(i);
  String m(b);
  if (m != "154" && m != "169" && m != "114" && m != "200" && m != "225") {
    // 처음 켠 보드: 빌드 때 정한 기본(DEFAULT_DISP, 없으면 1.54")을 EEPROM 에 남긴다. USB 없이 OTA 로 화면 종류를
    // 정할 때 `PLATFORMIO_BUILD_FLAGS='-DDEFAULT_DISP=\"169\"' pio run` 으로 한 번 올리면 그 뒤 판에서도 유지된다.
#ifndef DEFAULT_DISP
#define DEFAULT_DISP "154"
#endif
    m = DEFAULT_DISP;
    for (int i = 0; i < 4; i++) EEPROM.write(i, i < (int)m.length() ? m[i] : 0);
    // 설정 자리도 비운다. 다른 펌웨어가 쓰던 칩이면 그 값이 남아 SSID·OTA 비밀번호로 읽힌다
    // (SmallTV 원래 펌웨어의 도시 이름 조각이 SSID 로 읽힌 적이 있다)
    for (int i = EE_SSID; i < EE_SIZE; i++) EEPROM.write(i, 0);
    EEPROM.commit();
  }
#ifdef BOARD_SMALLTV
  m = "154";
  blInv = true;
#endif
  return m;
}

static void saveModel(const String &m) {
  for (int i = 0; i < 4; i++) EEPROM.write(i, i < (int)m.length() ? m[i] : 0);
  EEPROM.commit();
}

static void setupScreen() {
  int w = 240, h = 240;
  if (model == "169") h = 280;
  else if (model == "114") { w = 135; h = 240; }
  else if (model == "200") h = 320;
  else if (model == "225") { w = 76; h = 284; }
  compact = model == "225";
  blActiveLow = model == "225" || blInv;   // 2.25" 막대와 일부 1.54" 모듈은 BLK 가 LOW 일 때 켜진다
  pinMode(PIN_BL, OUTPUT);
  backlight(false);
  tft.init(w, h, PIN_CS < 0 ? SPI_MODE3 : SPI_MODE0);   // CS 가 없는 화면은 모드 3
  pinMode(PIN_BL, OUTPUT);   // D6(GPIO12)는 하드웨어 SPI 의 MISO 라 SPI 시작 때 빼앗긴다 — 화면은 받기만 하니 다시 출력으로
  tft.setSPISpeed(20000000);   // 점퍼선이면 40MHz 에서 화면 위쪽이 깨졌다
#ifdef BOARD_SMALLTV
  tft.setRotation(2);   // 케이스에 화면이 거꾸로 붙어 있다(실물)
#else
  tft.setRotation(compact ? 1 : 0);
#endif
  if (compact) tft.invertDisplay(false);   // 2.25" 는 색 반전을 꺼야 바탕이 검다
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  textSize = (w >= 200) ? 2 : 1;
  lineH = compact ? 15 : textSize == 2 ? (h >= 280 ? 29 : 26) : 18;
  topY = compact ? 2 : textSize == 2 ? 10 : 4;
  if (model == "169") { padX = 12; cornerX = 26; }   // 둥근 모서리
  else { padX = 4; cornerX = 4; }
  for (auto &s : shown) s = "";
}

static void line(int i, const String &s, uint16_t color) {
  if (shown[i] == s) return;
  shown[i] = s;
  int y = topY + i * lineH;
  tft.fillRect(0, y, tft.width(), textSize * 8 + 2, ST77XX_BLACK);
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setCursor(!compact && (i == 0 || i == LINES - 1) ? cornerX : padX, y);
  tft.print(s);
}

// --- 색깔 있게 그리기 -----------------------------------------------------------
// 0번 = 서버 이름(색 바탕 제목줄), CPU·RAM·DISK = 글자 + 사용률 막대(초록 < 60 ≤ 노랑 < 85 ≤ 빨강),
// CPU 온도(초록 < 60°C ≤ 노랑 < 75°C ≤ 빨강), VM(모두 실행이면 초록), IP 하늘색, 켜진 시간 회색.
static const uint16_t C_GREEN = 0x07E8, C_YELLOW = 0xFFE0, C_RED = 0xF8A2, C_GRAY = 0x8410,
                      C_DIM = 0x2104, C_SKY = 0x5D9F, C_HEAD = 0x1A6F, C_ORANGE = 0xFC60;

static uint16_t levelColor(int pct) { return pct < 60 ? C_GREEN : pct < 85 ? C_YELLOW : C_RED; }

static int pctOf(const String &s) {   // "CPU 7% 42C" → 7, "DISK 23% /" → 23, "RAM 19.8/126G" → 16
  if (s.startsWith("RAM ")) {
    int sl = s.indexOf('/');
    float u = s.substring(4, sl).toFloat(), t = s.substring(sl + 1).toFloat();
    return t > 0 ? (int)(100 * u / t + 0.5) : -1;
  }
  int sp = s.indexOf(' '), pc = s.indexOf('%');
  return (sp > 0 && pc > sp) ? s.substring(sp + 1, pc).toInt() : -1;
}

static void text(int x, int y, const String &s, uint16_t color) {
  tft.setTextColor(color);
  tft.setCursor(x, y);
  tft.print(s);
}

static void fancy(int i, const String &s, bool stale) {
  if (shown[i] == s + (stale ? "~" : "")) return;
  shown[i] = s + (stale ? "~" : "");
  int y = topY + i * lineH, x = padX, w = tft.width();
  tft.fillRect(0, y - 3, w, lineH, ST77XX_BLACK);
  tft.setTextSize(textSize);
  int cw = 6 * textSize;   // 글자 폭
  if (!s.length()) return;
  if (i == 0) {   // 제목줄('\t' 뒤는 오른쪽 요약)
    tft.fillRoundRect(cornerX - 6, y - 3, w - 2 * (cornerX - 6), textSize * 8 + 6, 6, stale ? C_DIM : C_HEAD);
    int tab = s.indexOf('\t');
    text(cornerX, y, tab < 0 ? s : s.substring(0, tab), stale ? C_GRAY : ST77XX_WHITE);
    if (tab >= 0) {
      String r = s.substring(tab + 1);
      text(w - cornerX - cw * r.length(), y, r, stale ? C_GRAY : r.startsWith("!") ? C_RED : C_GREEN);
    }
    return;
  }
  if (stale) { text(x, y, s, C_GRAY); return; }
  bool cpu = s.startsWith("CPU ") || s.startsWith("GPU "), ram = s.startsWith("RAM "), disk = s.startsWith("DISK ");
  if (cpu || ram || disk) {
    int pct = pctOf(s);
    int sp = s.indexOf(' ');
    String label = s.substring(0, sp), rest = s.substring(sp), cnt;
    text(x, y, label, C_SKY);
    if (s.startsWith("CPU ") && rest.endsWith("t")) {   // 끝의 코어·스레드 "20c 40t" 는 따로(회색)
      int b = rest.lastIndexOf(' '), a = rest.lastIndexOf(' ', b - 1);
      if (a > 0 && rest.charAt(b - 1) == 'c') { cnt = rest.substring(a); rest = rest.substring(0, a); }
    }
    if (cnt.length()) text(x + (label.length() + rest.length()) * cw, y, cnt, C_GRAY);
    if (cpu && rest.indexOf("C") > 0) {   // 온도(와 GPU 전력)는 따로 색: "CPU 7% 42C", "GPU 35% 58C 92W"
      int t = rest.indexOf(' ', rest.indexOf('%'));
      String load = rest.substring(0, t), tc = rest.substring(t);
      int deg = tc.toInt();
      text(x + label.length() * cw, y, load, levelColor(pct));
      text(x + (label.length() + load.length()) * cw, y, tc, deg < 60 ? C_GREEN : deg < 75 ? C_YELLOW : C_RED);
    } else {
      text(x + label.length() * cw, y, rest, ST77XX_WHITE);
    }
    if (pct >= 0) {   // 글자 아래 막대
      int by = y + textSize * 8 + 2, bh = textSize == 2 ? 5 : 3, bw = w - 2 * x;
      tft.fillRect(x, by, bw, bh, C_DIM);
      tft.fillRect(x, by, bw * min(pct, 100) / 100, bh, levelColor(pct));
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
  if (i == 1) { text(x, y, s, C_ORANGE); return; }   // IP
  text(x, y, s, ST77XX_WHITE);
}

static bool ntpOk() { return time(nullptr) > 1700000000; }

// 보일 시계: 컴퓨터가 보낸 값, 없거나 끊기면 NTP(Wi-Fi) 시각
static String nowClock(bool stale) {
  if (!stale && clockStr.length()) return clockStr;
  if (!ntpOk()) return "";
  time_t t = time(nullptr);
  char b[24];
  strftime(b, sizeof b, "%Y-%m-%d %a %H:%M", localtime(&t));
  return b;
}

static String ago(uint32_t s) {
  return s < 60 ? String(s) + "s" : s < 3600 ? String(s / 60) + "m" : String(s / 3600) + "h";
}

// 아래 빈 공간: 큰 시:분 + 날짜·요일. 끊기면 날짜 자리에 빨간 'no data N' (NTP 시각이 없으면 시계 없이).
static void drawClock(int top, bool stale) {
  String clk = nowClock(stale);
  String gone = lastHost ? ago((millis() - lastHost) / 1000) : String("");
  String key = clk + (stale ? "~" + gone : "");
  if (key == clockShown) return;
  clockShown = key;
  int w = tft.width(), h = tft.height(), free = h - top - 6;
  tft.fillRect(0, top, w, h - top, ST77XX_BLACK);
  if (free < 20) return;
  tft.drawFastHLine(cornerX, top + 2, w - 2 * cornerX, C_DIM);
  String nodata = "no data " + gone;
  if (!clk.length()) {
    if (stale) {
      tft.setTextSize(2);
      text((w - 12 * nodata.length()) / 2, top + free / 2 - 4, nodata, C_RED);
    }
    return;
  }
  int sp = clk.lastIndexOf(' ');
  String hm = clk.substring(sp + 1), date = clk.substring(0, sp);   // "01:23", "2026-10-20 Tue"
  int big = free >= 70 ? 5 : free >= 50 ? 4 : 3, small = free >= 50 ? 2 : 1;
  int bh = 8 * big, sh = 8 * small, gap = 6;
  int y0 = top + 4 + (free - bh - gap - sh) / 2;
  tft.setTextSize(big);
  text((w - 6 * big * hm.length()) / 2, y0, hm, C_YELLOW);
  tft.setTextSize(small);
  if (stale && lastHost) date = nodata;
  text((w - 6 * small * date.length()) / 2, y0 + bh + gap, date, stale && lastHost ? C_RED : C_SKY);
}

// --- 날씨 ----------------------------------------------------------------------
struct Wx { float t = 0, tmin = 0, tmax = 0; int rh = -1, code = -1; bool day = true; uint32_t at = 0; };
static Wx wx;
static String wxLat, wxLon, wxCity, wxShown, hostGeo;   // hostGeo = 서버가 알려 준 위치(CFG geo 가 비었을 때)
static uint32_t wxNext = 0;
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

// "key":12.3 또는 "key":[12.3] 의 숫자(from 뒤 첫 번째)
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

static void weatherTick() {
  if (cfgGeo == "off" || WiFi.status() != WL_CONNECTED) return;
  uint32_t &next = wxNext;
  if (next && (int32_t)(millis() - next) < 0) return;
  next = millis() + WX_RETRY;   // 실패하면 1분 뒤
  if (!wxLat.length()) {
    String geo = cfgGeo.length() ? cfgGeo : hostGeo;
    if (geo.length()) {   // "37.55,127.04[,이름]"
      int a = geo.indexOf(','), b = geo.indexOf(',', a + 1);
      if (a < 0) return;
      wxLat = geo.substring(0, a);
      wxLon = b < 0 ? geo.substring(a + 1) : geo.substring(a + 1, b);
      wxCity = b < 0 ? String("") : geo.substring(b + 1);
    } else {   // IP 로 대략(구 단위)
      String r = httpGet("http://ip-api.com/json/?fields=status,lat,lon,city");
      float la, lo;
      if (r.indexOf("success") < 0 || !jnum(r, 0, "lat", la) || !jnum(r, 0, "lon", lo)) return;
      wxLat = String(la, 3);
      wxLon = String(lo, 3);
      int c = r.indexOf("\"city\":\"");
      if (c >= 0) { c += 8; wxCity = r.substring(c, r.indexOf('"', c)); }
    }
    wxLat.trim(); wxLon.trim(); wxCity.trim();
  }
  String r = httpGet("http://api.open-meteo.com/v1/forecast?latitude=" + wxLat + "&longitude=" + wxLon +
                     "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
                     "&daily=temperature_2m_max,temperature_2m_min&timezone=Asia%2FSeoul&forecast_days=1");
  int cur = r.indexOf("\"current\":{"), day = r.indexOf("\"daily\":{");
  float t, rh, code, isday, mx, mn;
  if (cur < 0 || day < 0 || !jnum(r, cur, "temperature_2m", t) || !jnum(r, cur, "relative_humidity_2m", rh) ||
      !jnum(r, cur, "weather_code", code) || !jnum(r, cur, "is_day", isday) ||
      !jnum(r, day, "temperature_2m_max", mx) || !jnum(r, day, "temperature_2m_min", mn)) {
    Serial.println("WX failed");
    return;
  }
  wx.t = t; wx.rh = (int)rh; wx.code = (int)code; wx.day = isday > 0.5; wx.tmax = mx; wx.tmin = mn;
  wx.at = millis();
  next = millis() + WX_EVERY;
}

// WMO 날씨 코드 → 0 맑음 1 구름 조금 2 흐림 3 안개 4 비 5 눈 6 뇌우
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
      tft.drawLine(cx + c * (r + 4) + d * s, cy + s * (r + 4) - d * c, cx + c * (r + 9) + d * s, cy + s * (r + 9) - d * c, C_YELLOW);
  }
  tft.fillCircle(cx, cy, r, C_YELLOW);
}

static void moon(int cx, int cy, int r) {
  tft.fillCircle(cx, cy, r, C_YELLOW);
  tft.fillCircle(cx + r / 2, cy - r / 3, r - 1, ST77XX_BLACK);
}

static void cloud(int x, int y, uint16_t c) {   // 56x32
  tft.fillCircle(x + 16, y + 18, 12, c);
  tft.fillCircle(x + 32, y + 13, 13, c);
  tft.fillCircle(x + 45, y + 21, 10, c);
  tft.fillRoundRect(x + 4, y + 18, 52, 14, 7, c);
}

static void wxIcon(int x, int y, int k, bool day) {   // 64x64
  const uint16_t LIGHT = 0xC618;
  switch (k) {
    case 0: if (day) sun(x + 32, y + 32, 14); else moon(x + 32, y + 32, 17); break;
    case 1:
      if (day) sun(x + 42, y + 20, 10); else moon(x + 44, y + 18, 12);
      cloud(x + 2, y + 26, ST77XX_WHITE);
      break;
    case 2: cloud(x + 12, y + 8, C_GRAY); cloud(x + 2, y + 22, LIGHT); break;
    case 3:
      for (int i = 0; i < 5; i++) tft.fillRoundRect(x + 4 + (i % 2) * 8, y + 10 + i * 10, 48, 5, 2, LIGHT);
      break;
    case 4:
      cloud(x + 4, y + 6, LIGHT);
      for (int i = 0; i < 4; i++)
        for (int d = 0; d < 2; d++) tft.drawLine(x + 16 + i * 11 + d, y + 44, x + 11 + i * 11 + d, y + 58, C_SKY);
      break;
    case 5:
      cloud(x + 4, y + 6, LIGHT);
      for (int i = 0; i < 4; i++) {
        tft.fillCircle(x + 14 + i * 11, y + 47 + (i % 2) * 7, 3, ST77XX_WHITE);
      }
      break;
    default:
      cloud(x + 4, y + 6, C_GRAY);
      tft.fillTriangle(x + 34, y + 36, x + 22, y + 52, x + 32, y + 52, C_YELLOW);
      tft.fillTriangle(x + 30, y + 50, x + 40, y + 50, x + 26, y + 64, C_YELLOW);
      break;
  }
}

// 날씨 화면: 위치 · 큰 시:분 · 날짜, 아래 아이콘 + 기온·습도·오늘 최저/최고. 날씨가 없으면 시계·날짜만 가운데 크게.
static void drawWeather(bool stale) {
  String clk = nowClock(stale);
  bool ok = wxOk();
  String key = clk + "|" + String(ok ? wx.at : 0);
  if (key == wxShown) return;
  wxShown = key;
  int w = tft.width(), oy = (tft.height() - 240) / 2;
  tft.fillScreen(ST77XX_BLACK);
  if (!ok) {
    if (!clk.length()) return;
    int sp = clk.lastIndexOf(' ');
    String hm = clk.substring(sp + 1), date = clk.substring(0, sp);
    tft.setTextSize(7);
    text((w - 42 * hm.length()) / 2, oy + 72, hm, C_YELLOW);
    tft.setTextSize(2);
    text((w - 12 * date.length()) / 2, oy + 146, date, ST77XX_WHITE);
    return;
  }
  tft.setTextSize(2);
  String city = wxCity.length() ? wxCity : String("Weather");
  if (city.length() > 19) city = city.substring(0, 19);
  text((w - 12 * city.length()) / 2, oy + 8, city, C_SKY);
  if (clk.length()) {
    int sp = clk.lastIndexOf(' ');
    String hm = clk.substring(sp + 1), date = clk.substring(0, sp);
    tft.setTextSize(6);
    text((w - 36 * hm.length()) / 2, oy + 34, hm, C_YELLOW);
    tft.setTextSize(2);
    text((w - 12 * date.length()) / 2, oy + 90, date, ST77XX_WHITE);
  }
  tft.drawFastHLine(8, oy + 116, w - 16, C_DIM);
  int k = wxKind(wx.code);
  wxIcon(8, oy + 126, k, wx.day);
  tft.setTextSize(2);
  int nx = 8 + (64 - 12 * (int)strlen(WX_NAME[k])) / 2;
  text(nx < 2 ? 2 : nx, oy + 198, WX_NAME[k], ST77XX_WHITE);
  String t = String(wx.t, 1);
  if (t.length() > 4) t = String(lround(wx.t));   // -10.5 처럼 길면 정수로
  uint16_t tc = wx.t < 0 ? C_SKY : wx.t < 25 ? ST77XX_WHITE : wx.t < 30 ? C_ORANGE : C_RED;
  int rx = 92;
  tft.setTextSize(4);
  text(rx, oy + 128, t, tc);
  int ex = rx + 24 * t.length();
  tft.drawCircle(ex + 5, oy + 132, 4, tc);
  tft.drawCircle(ex + 5, oy + 132, 3, tc);
  text(ex + 12, oy + 128, "C", tc);
  tft.setTextSize(2);
  text(rx, oy + 172, "RH", C_SKY);
  text(rx + 36, oy + 172, String(wx.rh) + "%", ST77XX_WHITE);
  text(rx, oy + 198, String(lround(wx.tmin)) + "~" + String(lround(wx.tmax)) + "C", C_GRAY);
}

// 목록 쪽: 제목줄 + 줄마다 상태 점 · 이름 · 값(CPU% 등) · 아래 가는 막대(메모리)
static String listShown;
static void drawList(int slot, int pg) {
  Host &h = hosts[slot];
  const String &p = h.page[pg];
  String key = String(slot) + ":" + String(pg) + ":" + p;
  if (key == listShown) return;
  listShown = key;
  int w = tft.width(), H = tft.height();
  tft.fillScreen(ST77XX_BLACK);
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
  tft.setTextSize(2);
  fancy(0, title + (part[1].length() ? "\t" + part[1] : String("")), false);
  int y = topY + 30;
  while (at < (int)p.length() && y + 22 <= H) {
    int b = p.indexOf('|', at);
    String row = b < 0 ? p.substring(at) : p.substring(at, b);
    at = b < 0 ? p.length() : b + 1;
    if (!row.length()) continue;
    if (row[0] == '#') {   // 소제목(compose 묶음)
      String g = row.substring(1);
      tft.setTextSize(1);
      text(8, y + 4, g, C_GRAY);
      tft.drawFastHLine(8 + 6 * g.length() + 4, y + 7, w - 6 - (8 + 6 * g.length() + 4), C_DIM);
      tft.setTextSize(2);
      y += 14;
      continue;
    }
    char st = row[0];
    int t1 = row.indexOf('~'), t2 = row.indexOf('~', t1 + 1);
    String name = row.substring(1, t1 < 0 ? row.length() : t1);
    String val = t1 < 0 ? String("") : row.substring(t1 + 1, t2 < 0 ? row.length() : t2);
    int mem = t2 < 0 || t2 + 1 >= (int)row.length() ? -1 : row.substring(t2 + 1).toInt();
    uint16_t col = st == 'g' ? C_GREEN : st == 'y' ? C_YELLOW : st == 'r' ? C_RED : C_GRAY;
    tft.fillCircle(10, y + 7, 4, col);
    int room = (w - 6 - 12 * (int)val.length() - 6 - 20) / 12;
    if ((int)name.length() > room) name = name.substring(0, room);
    text(20, y, name, st == 'o' ? C_GRAY : ST77XX_WHITE);
    text(w - 6 - 12 * val.length(), y, val,
         st == 'r' ? C_RED : val.endsWith("%") ? levelColor(val.toInt()) : C_GRAY);
    if (mem >= 0) {
      int bw = w - 26;
      tft.fillRect(20, y + 18, bw, 3, C_DIM);
      tft.fillRect(20, y + 18, bw * min(mem, 100) / 100, 3, C_SKY);
    }
    y += 26;
  }
}

static void draw(bool stale) {
  uint16_t main = stale ? 0x8410 : ST77XX_WHITE;   // 끊기면 회색
  if (compact) {
    for (int i = 0; i < 5; i++) {
      String a = field[i * 2], b = i * 2 + 1 < LINES ? field[i * 2 + 1] : String("");
      a.replace("\t", " ");
      line(i, b.length() ? a + "  " + b : a, i == 0 ? (stale ? ST77XX_RED : ST77XX_CYAN) : main);
    }
    return;
  }
  int rows = nFields ? nFields : 3;
  static int lastRows = -1;
  if (rows != lastRows) {   // 줄 수가 바뀌면(서버가 바뀌거나 처음 값) 시계 자리가 옮겨 가므로 통째로 지운다
    lastRows = rows;
    tft.fillScreen(ST77XX_BLACK);
    for (auto &x : shown) x = "";
    clockShown = "";
  }
  for (int i = 0; i < rows; i++) fancy(i, field[i], stale);
  drawClock(topY + rows * lineH, stale);
}

// --- 받은 값 보관 ------------------------------------------------------------------
static String hostName(const String &s) {   // "HOST nas|..." / "LIST nas|..." → "nas"
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
  if (slot < 0) {   // 가득 차면 가장 오래된 자리
    for (int i = 1; i < NET_MAX; i++) if (hosts[i].at < hosts[old].at) old = i;
    slot = old;
    hosts[slot].pages = 0;
  }
  return slot;
}

static void storeHost(int slot, const String &s) {
  Host &h = hosts[slot];
  String name = hostName(s);
  if (h.name != name) h.pages = 0;   // 다른 서버가 자리를 이어받으면 목록은 버린다
  h.name = name;
  h.line = s;
  h.at = millis();
  h.seq++;
  lastHost = millis();
  if (!lit) backlight(true);
}

// "LIST nas|1/2|..." → 그 서버의 쪽 1 = "..."
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

// 서버 정보 줄을 field[] 로(제목줄 오른쪽에 목록 요약)
static void parseHost(const Host &h) {
  String rest = h.line.substring(5);
  int n = 0;
  clockStr = "";
  while (rest.length()) {
    int bar = rest.indexOf('|');
    String f = bar < 0 ? rest : rest.substring(0, bar);
    if (f.startsWith("@")) clockStr = f.substring(1);   // 시계는 따로(아래 큰 글씨)
    else if (n < LINES) field[n++] = f;
    if (bar < 0) break;
    rest = rest.substring(bar + 1);
  }
  nFields = n;
  for (int i = n; i < LINES; i++) field[i] = "";
  if (h.pages && h.page[0].length() && millis() - h.listAt < LIST_TTL) {   // 쪽 = 제목|오른쪽|요약|...
    const String &p = h.page[0];
    int b1 = p.indexOf('|'), b2 = p.indexOf('|', b1 + 1), b3 = p.indexOf('|', b2 + 1);
    String sum = (b2 >= 0) ? p.substring(b2 + 1, b3 < 0 ? p.length() : b3) : String("");
    if (sum.length() && n) field[0] += "\t" + sum;
  }
}

// 서버가 알려 준 위치: CFG geo 가 비었을 때만 쓰고, 바뀌면 날씨를 다시 받는다
static void setHostGeo(String g) {
  g.trim();
  if (g.length() > 40 || g.indexOf(',') < 0 || g == hostGeo) return;
  hostGeo = g;
  if (!cfgGeo.length()) { wxLat = ""; wx.at = 0; wxNext = 0; }
}

static void handle(String s) {
  s.trim();
  if (s.startsWith("HOST ")) {
    storeHost(USB_SLOT, s);
  } else if (s.startsWith("LIST ")) {
    storeList(USB_SLOT, s);
  } else if (s.startsWith("GEO ")) {
    setHostGeo(s.substring(4));
  } else if (s.startsWith("DISP ")) {
    String m = s.substring(5);
    m.trim();
    saveModel(m);
    Serial.println("OK disp " + m + " (restart)");
    delay(100);
    ESP.restart();
  } else if (s.startsWith("BLINV ")) {   // 백라이트 방향 저장(1 = LOW 일 때 켜짐) 후 다시 켬
    EEPROM.write(4, s.endsWith("1") ? 1 : 0);
    EEPROM.commit();
    Serial.println(String("OK blinv ") + (s.endsWith("1") ? "1" : "0") + " (restart)");
    delay(100);
    ESP.restart();
  } else if (s.startsWith("BL ")) {    // 백라이트 시험: BL 0 / BL 1 (D6 을 직접 LOW/HIGH)
    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, s.endsWith("1") ? HIGH : LOW);
    Serial.println(String("OK bl d6=") + (s.endsWith("1") ? "1" : "0"));
  } else if (s == "PING") {
    Serial.println("PONG hostinfo-display " + model + (blInv ? " blinv" : "") + " " + devId +
                   (WiFi.status() == WL_CONNECTED ? " wifi " + WiFi.SSID() + " " + WiFi.localIP().toString() +
                    " " + String(WiFi.RSSI()) + "dBm" : String(wifiOn ? " wifi connecting" : " wifi off")) +
                   (ntpOk() ? " ntp" : "") + (otaOn ? " ota" : "") + " show " + cfgShow +
                   (wxOk() ? " wx " + String(wx.t, 1) + "C " + String(wx.rh) + "% code " + String(wx.code) + " " + wxLat + "," + wxLon
                           : String(" wx -")));
  } else if (s == "NETS") {   // Wi-Fi 로 받은 서버 목록
    for (int i = 0; i < SLOTS; i++)
      if (hosts[i].at)
        Serial.println(String(i == USB_SLOT ? "USB " : "NET ") + hosts[i].name + " " + String((millis() - hosts[i].at) / 1000) +
                       "s pages " + String(hosts[i].pages));
    Serial.println("OK nets");
  } else if (s == "CFG") {
    Serial.println("CFG id " + devId + " ssid " + (cfgSsid.length() ? cfgSsid : String("-")) +
                   " pass " + (cfgPass.length() ? "set(" + String(cfgPass.length()) + ")" : String("-")) +
                   " ota " + (cfgOta.length() ? String("set") : String("-")) + " show " + cfgShow + " servers " + cfgServers +
                   " geo " + (cfgGeo.length() ? cfgGeo : hostGeo.length() ? "host:" + hostGeo : String("auto")));
  } else if (s == "CFG RESTART") {
    Serial.println("OK restart");
    delay(100);
    ESP.restart();
  } else if (s.startsWith("CFG ")) {   // CFG ssid a,b,c / CFG pass x / CFG ota x ('-' = 지움). 다시 켜면 적용
    String r = s.substring(4);
    int sp = r.indexOf(' ');
    String k = sp < 0 ? r : r.substring(0, sp), v = sp < 0 ? String("") : r.substring(sp + 1);
    if (v == "-") v = "";
    if (k == "ssid" && v.length() < 80) { eeSet(EE_SSID, 80, v); cfgSsid = v; }
    else if (k == "pass" && v.length() < 64) { eeSet(EE_PASS, 64, v); cfgPass = v; }
    else if (k == "ota" && v.length() < 32) { eeSet(EE_OTA, 32, v); cfgOta = v; }
    else if (k == "servers" && v.length() < 128) {   // 바로 적용, '-' = 기본 목록
      eeSet(EE_SRV, 128, v);
      cfgServers = v.length() ? v : String(DEFAULT_SERVERS);
      Serial.println("OK cfg servers " + cfgServers);
      return;
    }
    else if (k == "geo" && v.length() < 32) {   // 바로 적용: '위도,경도[,이름]' | '-' = IP 로 찾기 | off
      eeSet(EE_GEO, 32, v);
      cfgGeo = v;
      wxLat = "";
      wx.at = 0;
      wxNext = 0;
      Serial.println("OK cfg geo " + (v.length() ? v : String("auto")));
      return;
    }
    else if (k == "show" && v.length() < 32) {   // 바로 적용
      if (v == "all") v = "";
      eeSet(EE_SHOW, 32, v);
      cfgShow = v.length() ? v : String("all");
      Serial.println("OK cfg show " + cfgShow);
      return;
    }
    else { Serial.println("ERR cfg"); return; }
    Serial.println("OK cfg " + k + " (CFG RESTART to apply)");
  }
}

static void startWifi() {
  cfgSsid = eeStr(EE_SSID, 80);
  cfgPass = eeStr(EE_PASS, 64);
  cfgOta = eeStr(EE_OTA, 32);
  cfgShow = eeStr(EE_SHOW, 32);
  if (!cfgShow.length()) cfgShow = "all";
  cfgServers = eeStr(EE_SRV, 128);
  if (!cfgServers.length()) cfgServers = DEFAULT_SERVERS;
  cfgGeo = eeStr(EE_GEO, 32);
  if (!cfgSsid.length()) {
#ifdef BOARD_SMALLTV
    return;   // 설정 AP 를 띄운다(portalTick)
#endif
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    return;
  }
  WiFi.persistent(false);   // SDK 가 따로 기억하는 옛 SSID 를 쓰지 않게(ESP32 때 겪음)
  WiFi.mode(WIFI_STA);
  WiFi.hostname(devId);
  String l = cfgSsid;   // 같은 비밀번호로 여러 SSID, 신호가 센 곳에 붙는다
  while (l.length()) {
    int c = l.indexOf(',');
    String one = c < 0 ? l : l.substring(0, c);
    one.trim();
    if (one.length()) wifiMulti.addAP(one.c_str(), cfgPass.c_str());
    if (c < 0) break;
    l = l.substring(c + 1);
  }
  configTime("KST-9", "time.bora.net", "kr.pool.ntp.org", "time.google.com");
  wifiOn = true;
}

static void startOta() {
  if (otaOn || !cfgOta.length()) return;
  ArduinoOTA.setHostname(devId.c_str());
  ArduinoOTA.setPassword(cfgOta.c_str());
  ArduinoOTA.onStart([]() {
    backlight(true);
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextSize(2);
    text(padX, topY, "OTA update...", C_YELLOW);
  });
  ArduinoOTA.onProgress([](unsigned int d, unsigned int t) {
    int w = tft.width() - 2 * padX;
    tft.fillRect(padX, topY + 30, w * d / (t ? t : 1), 8, C_GREEN);
  });
  // 비밀번호가 틀리면 아무것도 쓰지 않았으니 그대로 둔다(예전엔 다시 켜져 SmallTV 가 Wi-Fi 를 잃었다)
  ArduinoOTA.onError([](ota_error_t e) { if (e != OTA_AUTH_ERROR) ESP.restart(); });
  ArduinoOTA.begin(false);   // mDNS 없이(주소로 올림)
  otaOn = true;
}

static void netReceive() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!udpOn) udpOn = udp.begin(UDP_PORT);
  static uint32_t lastAnn = 0;
  if (!lastAnn || millis() - lastAnn > 5000) {   // 서버들에게 '여기 있다'
    lastAnn = millis();
    // 무선 컨트롤러가 Wi-Fi 쪽 브로드캐스트도 넘기지 않아, 서버 주소(고정 할당)로 직접 알린다
    String l = cfgServers + ",255.255.255.255";
    while (l.length()) {
      int c = l.indexOf(',');
      String one = c < 0 ? l : l.substring(0, c);
      IPAddress ip;
      if (ip.fromString(one)) {
        udp.beginPacket(ip, 4211);
        udp.print("DISPLAY " + devId + "\n");
        udp.endPacket();
      }
      if (c < 0) break;
      l = l.substring(c + 1);
    }
  }
  static char b[700];
  for (int k = 0; k < 6; k++) {   // 한 번에 몇 개까지
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

// --- 화면 순서 ---------------------------------------------------------------------
// 서버마다 서버 정보(8초) → 목록 쪽(6초씩) → 다음 서버 …, 한 바퀴 돌면 날씨(5초). 서버가 없으면 날씨·시계만.
enum { V_NONE, V_HOST, V_LIST, V_WX };
static int vKind = V_NONE, vSlot = -1, vPage = 0;
static uint32_t vSince = 0, vSeq = 0xFFFFFFFF;

static bool alive(int i) { return hosts[i].at && millis() - hosts[i].at < STALE_MS; }

static bool candidate(int i) {
  if (!alive(i)) return false;
  if (i == USB_SLOT) return true;
  if (alive(USB_SLOT)) return false;   // USB 값이 오는 동안은 USB 만
  return cfgShow == "all" || hosts[i].name == cfgShow;
}

static int listPages(int i) {
  return (!compact && hosts[i].pages && millis() - hosts[i].listAt < LIST_TTL) ? hosts[i].pages : 0;
}

static void setView(int k, int slot, int page) {
  vSince = millis();
  if (k == vKind && slot == vSlot && page == vPage) return;
  vKind = k; vSlot = slot; vPage = page;
  vSeq = 0xFFFFFFFF;
  tft.fillScreen(ST77XX_BLACK);
  for (auto &x : shown) x = "";
  clockShown = wxShown = listShown = "";
}

static bool anyCandidate() {
  for (int i = 0; i < SLOTS; i++) if (candidate(i)) return true;
  return false;
}

static void schedule() {
  uint32_t now = millis();
  if (!anyCandidate()) {
    if (!compact && nowClock(true).length()) { if (vKind != V_WX) setView(V_WX, -1, 0); }
    else if (vSlot >= 0 && vKind != V_HOST) setView(V_HOST, vSlot, 0);   // 시계도 없으면 마지막 서버를 회색으로
    return;
  }
  bool valid = (vKind == V_HOST && candidate(vSlot)) || (vKind == V_LIST && candidate(vSlot) && vPage < listPages(vSlot)) ||
               (vKind == V_WX && wxOk());
  uint32_t dur = vKind == V_HOST ? HOST_MS : vKind == V_LIST ? LIST_MS : WX_MS;
  if (valid && now - vSince < dur) return;
  if (valid && vKind == V_HOST && listPages(vSlot)) { setView(V_LIST, vSlot, 0); return; }
  if (valid && vKind == V_LIST && vPage + 1 < listPages(vSlot)) { setView(V_LIST, vSlot, vPage + 1); return; }
  int from = (vKind == V_HOST || vKind == V_LIST) ? vSlot : SLOTS;
  for (int i = from + 1; i < SLOTS; i++)
    if (candidate(i)) { setView(V_HOST, i, 0); return; }
  if (vKind != V_WX && !compact && wxOk() && nowClock(false).length()) { setView(V_WX, -1, 0); return; }
  for (int i = 0; i < SLOTS; i++)
    if (candidate(i)) { setView(V_HOST, i, 0); return; }
}

static void render() {
  bool stale = !anyCandidate();
  if (vKind == V_WX) { drawWeather(stale); return; }
  if (vKind == V_LIST && vSlot >= 0 && vPage < hosts[vSlot].pages) { drawList(vSlot, vPage); return; }
  if (vKind == V_HOST && vSlot >= 0) {
    if (hosts[vSlot].seq != vSeq) { vSeq = hosts[vSlot].seq; parseHost(hosts[vSlot]); }
    draw(stale);
  }
}

// --- 설정 양식(SmallTV: 시리얼이 없다) -------------------------------------------
#ifdef BOARD_SMALLTV
static ESP8266WebServer web(80);
static DNSServer dns;
static bool apOn = false;

static String esc(const String &v) {
  String r;
  for (char c : v) {
    if (c == '&') r += "&amp;"; else if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;"; else r += c;
  }
  return r;
}

static void webRoot() {
  String h = F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
               "<title>hostinfo</title><style>body{font:16px sans-serif;margin:16px;max-width:420px}"
               "input{width:100%;padding:6px;margin:2px 0 10px;box-sizing:border-box}</style>");
  h += "<h3>" + devId + "</h3><p>";
  h += WiFi.status() == WL_CONNECTED ? "Wi-Fi " + esc(WiFi.SSID()) + " " + WiFi.localIP().toString() : String("Wi-Fi -");
  h += wxOk() ? "<br>weather " + String(wx.t, 1) + "C " + String(wx.rh) + "% " + esc(wxCity) : String("<br>weather -");
  h += F("</p><form method=post action=/save autocomplete=off>SSID (쉼표로 여러 개)<input name=ssid value='");
  h += esc(cfgSsid) + F("'>Wi-Fi 비밀번호 (비우면 그대로)<input name=pass type=password autocomplete=new-password>"
                        "OTA 비밀번호 (비우면 그대로, - 는 지움)<input name=ota type=password autocomplete=new-password>"
                        "날씨 위치: 위도,경도[,이름] · 비우면 IP 로 · off<input name=geo value='");
  h += esc(cfgGeo) + F("'>보일 서버 (all 또는 호스트 이름)<input name=show value='");
  h += esc(cfgShow) + F("'>알림 보낼 서버 주소 (비우면 기본)<input name=servers value='");
  h += esc(cfgServers == DEFAULT_SERVERS ? String("") : cfgServers) + "'>";
  if (cfgOta.length()) h += F("지금 OTA 비밀번호 (저장하려면 필요)<input name=auth type=password>");
  h += F("<button>저장하고 다시 켜기</button></form>");
  web.send(200, "text/html; charset=utf-8", h);
}

static void webSave() {
  if (cfgOta.length() && web.arg("auth") != cfgOta) { web.send(403, "text/plain", "wrong OTA password"); return; }
  String ssid = web.arg("ssid"), pass = web.arg("pass"), ota = web.arg("ota"), geo = web.arg("geo"),
         show = web.arg("show"), srv = web.arg("servers");
  ssid.trim(); geo.trim(); show.trim(); srv.trim();
  if (ssid.length() > 79 || pass.length() > 63 || ota.length() > 31 || geo.length() > 31 || show.length() > 31 ||
      srv.length() > 127) { web.send(400, "text/plain", "too long"); return; }
  eeSet(EE_SSID, 80, ssid);
  if (pass.length()) eeSet(EE_PASS, 64, pass);
  if (ota.length()) eeSet(EE_OTA, 32, ota == "-" ? String("") : ota);
  eeSet(EE_GEO, 32, geo);
  eeSet(EE_SHOW, 32, show == "all" ? String("") : show);
  eeSet(EE_SRV, 128, srv);
  web.send(200, "text/plain; charset=utf-8", "saved, restarting");
  delay(500);
  ESP.restart();
}

static void showSetup() {
  field[0] = "Wi-Fi setup";
  field[1] = "join " + devId;
  field[2] = "open 192.168.4.1";
  for (int i = 3; i < LINES; i++) field[i] = "";
  nFields = 3;
  if (!lit) backlight(true);
  draw(false);
}

static void portalTick() {
  static bool webOn = false;
  static uint32_t lostSince = 0;
  if (!webOn) {
    web.on("/", HTTP_GET, webRoot);
    web.on("/save", HTTP_POST, webSave);
    web.onNotFound([]() { web.sendHeader("Location", "http://192.168.4.1/"); web.send(302, "text/plain", ""); });
    web.begin();
    webOn = true;
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (up) lostSince = 0;
  else if (!lostSince) lostSince = millis() | 1;
  if (!apOn && !up && (!cfgSsid.length() || millis() - lostSince > 180000)) {   // 설정이 없거나 3분 넘게 못 붙음
    WiFi.mode(cfgSsid.length() ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(devId.c_str());
    dns.start(53, "*", WiFi.softAPIP());
    apOn = true;
    Serial.println("AP " + devId);
    if (vKind == V_NONE) showSetup();
  } else if (apOn && up) {   // 다시 붙으면 AP 를 닫는다
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apOn = false;
  }
  if (apOn) dns.processNextRequest();
  web.handleClient();
}
#endif

void setup() {
  Serial.begin(115200);
  buf.reserve(700);
  char id[20];
  snprintf(id, sizeof id, "hostinfo-%06x", ESP.getChipId());
  devId = id;
  model = loadModel();
  startWifi();
  setupScreen();
  backlight(true);
  field[0] = "hostinfo";
  field[1] = "waiting for host";
  field[2] = "disp " + model;
  draw(false);
  Serial.println("READY hostinfo-display " + model + " " + devId);
}

void loop() {
  if (wifiOn) {
    static uint32_t lastTry = 0;
    if (WiFi.status() == WL_CONNECTED) startOta();
    else if (!lastTry || millis() - lastTry > 15000) {   // 끊겼을 때만 15초마다(붙어 있으면 아무것도 안 함)
      lastTry = millis();
      wifiMulti.run(3000);
    }
    if (otaOn) ArduinoOTA.handle();
    netReceive();
    weatherTick();
    static bool wasUp = false;
    bool up = WiFi.status() == WL_CONNECTED;
    if (up && !wasUp && vKind == V_NONE) {   // 붙었는데 아직 서버 값이 없으면 주소라도
      field[0] = devId;
      field[1] = WiFi.localIP().toString();
      field[2] = "waiting for host";
      for (int i = 3; i < LINES; i++) field[i] = "";
      nFields = 3;
      draw(false);
    }
    wasUp = up;
  }
#ifdef BOARD_SMALLTV
  portalTick();
#endif
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { handle(buf); buf = ""; }
    else if (buf.length() < 690) buf += c;
  }
  uint32_t now = millis();
  static uint32_t lastTick = 0;
  if (now - lastTick > 200) {
    lastTick = now;
    schedule();
    render();
    bool clk = !compact && nowClock(true).length();   // 시계를 보일 수 있으면 시계로 남는다
    if (lit && lastHost && now - lastHost > DARK_MS && !clk) backlight(false);
    if (!lit && (clk || anyCandidate())) backlight(true);
  }
  delay(5);
}
