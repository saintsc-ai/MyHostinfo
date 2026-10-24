// Wi-Fi + OTA + 설정. 설정은 보드 안(NVS, 이름공간 "hostinfo")에만 둔다.
//
// 시리얼 한 줄 명령:
//   CFG                         → 지금 설정(비밀번호는 가림)
//   CFG <키> <값>               → 저장. 키: ssid pass id ota disp hinfo geo hsrv (값에 빈칸 가능, 줄 끝까지)
//                                ssid 는 쉼표로 여러 개(비밀번호는 하나): ssid home,office — 그중 신호가 센 곳에 붙는다
//                                disp = 화면 종류(154·169·114·200·225), hinfo off = 컴퓨터 상태 화면 끔(보드 상태만),
//                                geo 위도,경도,이름 = 날씨 위치, hsrv IP,IP… = 'DISPLAY'를 알릴 서버
//   CFG <키>                    → 그 키 지우기
//   CFG RESTART                 → 다시 켜기(설정을 바꾼 뒤)
//   WIFISCAN                    → 보이는 Wi-Fi(진단)
// ssid 가 없으면 Wi-Fi 를 켜지 않는다(USB 로 꽂은 컴퓨터만 보인다).
// OTA: ota 비밀번호를 넣으면 Wi-Fi 로 펌웨어를 올릴 수 있다(espota, 포트 3232, 호스트 이름 = id).
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <stdarg.h>
#include <esp_task_wdt.h>

#ifndef PREFS_NS
#define PREFS_NS "hostinfo"
#endif

namespace net {

static Preferences prefs;
static WiFiMulti multi;
static String cSsid, cPass, cId, cOta, cDisp, cHinfo, cGeo, cHsrv;
static uint32_t lastWifiTry = 0;
static bool otaOn = false;
static void (*otaHook)() = nullptr;   // OTA 를 시작할 때(main 이 화면에 알림)

static void outln(const String &s) { Serial.println(s); }

static void outf(const char *fmt, ...) {
  char buf[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  size_t n = strlen(buf);
  while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
  outln(String(buf));
}

static void load() {
  prefs.begin(PREFS_NS, true);
  cSsid = prefs.getString("ssid", "");
  cPass = prefs.getString("pass", "");
  cId = prefs.getString("id", "");
  cOta = prefs.getString("ota", "");
  cDisp = prefs.getString("disp", "");
  cHinfo = prefs.getString("hinfo", "on");
  cGeo = prefs.getString("geo", "");
  cHsrv = prefs.getString("hsrv", "");
  prefs.end();
  if (cId.isEmpty()) {
    uint8_t m[6];
    WiFi.macAddress(m);
    char b[20];
    snprintf(b, sizeof(b), "esp32-%02x%02x%02x", m[3], m[4], m[5]);
    cId = b;
  }
}

static String mask(const String &s) { return s.isEmpty() ? String("(none)") : String("****"); }

static void showConfig() {
  outf("CFG ssid=%s pass=%s id=%s ota=%s disp=%s hinfo=%s geo=%s hsrv=%s",
       cSsid.c_str(), mask(cPass).c_str(), cId.c_str(), mask(cOta).c_str(), cDisp.isEmpty() ? "off" : cDisp.c_str(),
       cHinfo.c_str(), cGeo.isEmpty() ? "auto" : cGeo.c_str(), cHsrv.isEmpty() ? "default" : cHsrv.c_str());
}

// "CFG ..."·"WIFISCAN" 줄을 처리했으면 true. raw 는 줄 전체(빈칸 그대로).
static bool command(const String &raw) {
  if (raw == "WIFISCAN") {
    int n = WiFi.scanNetworks();
    String line = "WIFISCAN";
    for (int i = 0; i < n && i < 20; i++) line += " " + WiFi.SSID(i) + ":" + String(WiFi.RSSI(i)) + ":ch" + String(WiFi.channel(i));
    WiFi.scanDelete();
    outln(line);
    return true;
  }
  if (!(raw == "CFG" || raw.startsWith("CFG "))) return false;
  String rest = raw.length() > 4 ? raw.substring(4) : String("");
  rest.trim();
  if (rest.isEmpty()) { showConfig(); return true; }
  if (rest == "RESTART") { outln("OK restart"); Serial.flush(); delay(200); ESP.restart(); }
  int sp = rest.indexOf(' ');
  String key = sp < 0 ? rest : rest.substring(0, sp);
  String val = sp < 0 ? String("") : rest.substring(sp + 1);
  static const char *keys[] = {"ssid", "pass", "id", "ota", "disp", "hinfo", "geo", "hsrv"};
  bool known = false;
  for (auto k : keys) known = known || key == k;
  if (!known) { outln("ERR usage: CFG [<ssid|pass|id|ota|disp|hinfo|geo|hsrv> [value]] | CFG RESTART"); return true; }
  prefs.begin(PREFS_NS, false);
  if (val.isEmpty()) prefs.remove(key.c_str());
  else prefs.putString(key.c_str(), val);
  prefs.end();
  load();
  outf("OK cfg %s (CFG RESTART to apply)", key.c_str());
  return true;
}

static void begin() {
  load();
  if (cSsid.isEmpty()) return;     // USB 전용
  WiFi.persistent(false);          // IDF 가 예전 SSID 를 따로 기억했다가 저절로 붙으려 하는 것을 막는다
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  delay(100);
  WiFi.setHostname(cId.c_str());
  WiFi.setAutoReconnect(true);
  for (String rest = cSsid; rest.length();) {
    int c = rest.indexOf(',');
    String ss = c < 0 ? rest : rest.substring(0, c);
    rest = c < 0 ? String("") : rest.substring(c + 1);
    ss.trim();
    if (ss.length()) multi.addAP(ss.c_str(), cPass.c_str());
  }
  multi.run(10000);
  lastWifiTry = millis();
}

// loop 에서 부른다: 끊겼으면 30초마다 다시 붙고, 붙어 있으면 OTA 를 받는다.
static void poll() {
  if (cSsid.isEmpty()) return;
  uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiTry > 30000) { lastWifiTry = now; esp_task_wdt_reset(); multi.run(3000); }
    return;
  }
  if (!otaOn && cOta.length()) {
    ArduinoOTA.setHostname(cId.c_str());
    ArduinoOTA.setPassword(cOta.c_str());
    ArduinoOTA.onStart([]() { Serial.println("OTA start"); if (otaHook) otaHook(); });
    ArduinoOTA.onProgress([](unsigned int, unsigned int) { esp_task_wdt_reset(); });   // 받는 동안 워치독(8초)에 걸리지 않게
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA error %d\n", (int)e); });
    ArduinoOTA.begin();
    otaOn = true;
  }
  if (otaOn) ArduinoOTA.handle();
}

}  // namespace net
