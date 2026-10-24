// 화면(ST7789 SPI 모듈). SPI3(HSPI)를 쓴다(FSPI 는 비워 둔다).
//
// 배선: SCK→GPIO6, MOSI(SDA)→GPIO7, CS→15, DC→16, RST→17, BL(BLK)→18, VCC→3V3, GND.
// 설정: CFG disp <종류> 후 CFG RESTART. 종류 = 169(1.69" 240×280) · 154(1.54" 240×240) ·
//       114(1.14" 135×240) · 200(2.0" 240×320, 백라이트 핀 없음) · 225(2.25" 76×284). 비우면 화면 없음.
// 백라이트: 켤 때와 BOOT 버튼(GPIO0)을 누를 때 켜고 30초 뒤 끈다. 켜져 있는 동안 2초마다 바뀐 줄만 다시 그린다.
// 컴퓨터 상태 화면(hostview.h)이 켜져 있으면(CFG hinfo, 기본) 화면 순서·백라이트는 그쪽이 정하고, 여기서는 보드 상태 한 쪽만 그린다.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

namespace disp {

static const int PIN_SCK = 6, PIN_MOSI = 7, PIN_CS = 15, PIN_DC = 16, PIN_RST = 17, PIN_BL = 18, PIN_BTN = 0;
static const uint32_t ON_MS = 30000, EVERY_MS = 2000;
static const int LINES = 9;

struct Info {
  String id, wifi, ip, srv, wx, mem, up;
};

static SPIClass *bus = nullptr;
static Adafruit_ST7789 *tft = nullptr;
static bool hasBl = true;
static bool blActiveLow = false;    // 2.25" 막대 모듈은 BLK 가 LOW 일 때 켜진다
static uint32_t onUntil = 0, lastDraw = 0;
static bool lit = false;
static String shown[LINES];
static int textSize = 2, lineH = 30, topY = 8;
static int padX = 4, cornerX = 4;   // 왼쪽 여백, 둥근 모서리 화면(1.69")의 맨 위·맨 아래 줄 여백
static bool compact = false;        // 2.25" 막대(76px 폭): 가로로 눕혀 다섯 줄로 묶는다

static void backlight(bool on) {
  lit = on;
  if (hasBl) digitalWrite(PIN_BL, on != blActiveLow ? HIGH : LOW);
}

static void wake(uint32_t now) {
  onUntil = now + ON_MS;
  if (!lit) {
    backlight(true);
    lastDraw = 0;   // 켜자마자 바로 그린다
  }
}

// 종류 문자열로 시작. 모르는 값이나 빈 값이면 false(화면 없음).
static bool begin(const String &model) {
  int w, h;
  if (model == "169") { w = 240; h = 280; }
  else if (model == "154") { w = 240; h = 240; }
  else if (model == "114") { w = 135; h = 240; }
  else if (model == "200") { w = 240; h = 320; hasBl = false; }
  else if (model == "225") { w = 76; h = 284; }
  else return false;
  blActiveLow = model == "225";
  if (hasBl) { pinMode(PIN_BL, OUTPUT); digitalWrite(PIN_BL, blActiveLow ? HIGH : LOW); }   // 처음엔 끈 채로
  pinMode(PIN_BTN, INPUT_PULLUP);
  bus = new SPIClass(HSPI);
  bus->begin(PIN_SCK, -1, PIN_MOSI, PIN_CS);
  tft = new Adafruit_ST7789(bus, PIN_CS, PIN_DC, PIN_RST);
  tft->init(w, h);   // 240×280 은 라이브러리가 세로 offset 20 을 넣는다
  tft->setSPISpeed(40000000);
  compact = model == "225";
  tft->setRotation(compact ? 1 : 0);   // 막대는 가로로(284×76). 라이브러리가 offset 82·18 을 넣는다
  if (compact) tft->invertDisplay(false);   // 이 패널은 색 반전을 끄야 바탕이 검다(라이브러리 기본은 켬)
  tft->fillScreen(ST77XX_BLACK);
  textSize = w >= 200 ? 2 : 1;
  lineH = compact ? 15 : textSize == 2 ? (h >= 280 ? 29 : 26) : 18;
  topY = compact ? 2 : textSize == 2 ? 10 : 4;
  if (model == "169") { padX = 12; cornerX = 26; }   // 모서리 반지름이 커서 첫 줄 첫 글자가 잘렸다
  else { padX = 4; cornerX = 4; }
  tft->setTextWrap(false);
  for (auto &s : shown) s = "";
  wake(millis());
  return true;
}

// USB 로 꽂힌 컴퓨터가 보낸 상태(HOST 줄). 20초 넘게 안 오면 보드 상태 화면으로 돌아간다.
static const uint32_t HOST_STALE_MS = 20000;
static String host[LINES];
static uint32_t hostAt = 0;

static void hostSeen(uint32_t now) {
  if (!hostAt || now - hostAt > HOST_STALE_MS) {   // 새로 붙으면 화면을 켜고 지운다
    if (tft) {
      tft->fillScreen(ST77XX_BLACK);
      for (auto &s : shown) s = "";
    }
    wake(now);
  }
  hostAt = now;
}

static bool hostMode(uint32_t now) { return hostAt && now - hostAt <= HOST_STALE_MS; }

static void line(int i, const String &s, uint16_t color) {
  if (shown[i] == s) return;
  shown[i] = s;
  int y = topY + i * lineH;
  tft->fillRect(0, y, tft->width(), textSize * 8 + 2, ST77XX_BLACK);
  tft->setTextSize(textSize);
  tft->setTextColor(color);
  tft->setCursor(i == 0 || i == LINES - 1 ? cornerX : padX, y);
  tft->print(s);
}

// 보드 상태 화면(바뀐 줄만). hostview.h 도 쓴다.
static void drawBoard(const Info &d) {
  if (compact) {   // 284×76: 한 줄 47글자, 다섯 줄
    line(0, d.id + "   " + d.up, ST77XX_CYAN);
    line(1, d.wifi + "  " + d.ip, ST77XX_WHITE);
    line(2, d.srv, d.srv.startsWith("servers 0") ? 0x8410 : ST77XX_GREEN);
    line(3, d.wx, ST77XX_YELLOW);
    line(4, d.mem, ST77XX_WHITE);
    return;
  }
  line(0, d.id, ST77XX_CYAN);
  line(1, d.wifi, ST77XX_WHITE);
  line(2, d.ip, ST77XX_WHITE);
  line(3, d.srv, d.srv.startsWith("servers 0") ? 0x8410 : ST77XX_GREEN);
  line(4, d.wx, ST77XX_YELLOW);
  line(5, d.mem, ST77XX_WHITE);
  line(6, d.up, 0x8410);   // 회색
  line(7, "", ST77XX_WHITE);
  line(8, "", ST77XX_WHITE);
}

// loop 에서 매번(CFG hinfo off 이거나 2.25" 막대일 때). 버튼·백라이트를 보고, 켜져 있으면 2초마다 info() 를 받아 바뀐 줄만 그린다.
template <typename F>
static void tick(uint32_t now, F info) {
  if (!tft) return;
  if (digitalRead(PIN_BTN) == LOW) wake(now);
  if (lit && hasBl && (int32_t)(now - onUntil) > 0) {
    backlight(false);
    return;
  }
  if (!lit && hasBl) return;   // 꺼져 있는 동안은 그리지 않는다(SPI 시간을 아낀다)
  if (lastDraw && now - lastDraw < EVERY_MS) return;
  lastDraw = now;
  static bool wasHost = false;
  bool hm = hostMode(now);
  if (hm != wasHost) {   // 보드 화면 ↔ 컴퓨터 화면이 바뀌면 지우고 다시
    tft->fillScreen(ST77XX_BLACK);
    for (auto &s : shown) s = "";
    wasHost = hm;
  }
  if (hm) {
    if (compact) {   // 284×76: 두 필드씩
      for (int i = 0; i < 5; i++) {
        String a = host[i * 2], b = i * 2 + 1 < LINES ? host[i * 2 + 1] : String("");
        line(i, b.length() ? a + "  " + b : a, i == 0 ? ST77XX_CYAN : ST77XX_WHITE);
      }
    } else {
      for (int i = 0; i < LINES; i++) line(i, host[i], i == 0 ? ST77XX_CYAN : ST77XX_WHITE);
    }
    return;
  }
  drawBoard(info());
}

}  // namespace disp
