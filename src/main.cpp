// ============================================================
//  Abfahrtsmonitor fuer das CYD (ESP32-2432S028R, 320x240)
//  Design "Stein Anthrazit" – Daten live vom RMV
//  Einrichtung ueber eigenes WLAN (Captive Portal)
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <time.h>
#include "esp_sntp.h"
#include "qrcode.h"
#include "config.h"
#include "portal_html.h"
#include "stops_data.h"

// ---------- Farben (Design "Stein Anthrazit") ----------------
static constexpr uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}
static const uint16_t COL_BG        = rgb(0xCFCBBF);  // Stein
static const uint16_t COL_FG        = rgb(0x111111);
static const uint16_t COL_SUB       = rgb(0x4A473F);
static const uint16_t COL_PANEL     = rgb(0x3A3833);  // Anthrazit
static const uint16_t COL_PANEL_FG  = rgb(0xF2F1EC);
static const uint16_t COL_PANEL_SUB = rgb(0xB8B4A9);
static const uint16_t COL_BTN       = rgb(0x4F4C45);
static const uint16_t COL_RED       = rgb(0xC62D1F);
static const uint16_t COL_ORANGE    = rgb(0xF29D0A);
static const uint16_t COL_GREEN     = rgb(0x1F9D55);
static const uint16_t COL_WHITE     = rgb(0xFFFFFF);

// ---------- Hardware -----------------------------------------
#define XPT_IRQ  36
#define XPT_MOSI 32
#define XPT_MISO 39
#define XPT_CLK  25
#define XPT_CS   33
#define LED_R 4
#define LED_G 16
#define LED_B 17

TFT_eSPI tft;
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);
Preferences prefs;

// ---------- Einstellungen (im Flash gespeichert) --------------
struct Config {
  String ssid, pass, key;
  String stopId, stopName, dirId, dirName;
  String lines;          // z. B. "1, 6" – leer = alle
  uint8_t walk = DEFAULT_WALK_MIN;
  uint8_t light = 3;     // 1..3
} C;

static const int MAX_LINES = 8;
String lineList[MAX_LINES];
int lineCount = 0;

static void parseLines() {
  lineCount = 0;
  String cur;
  for (size_t i = 0; i <= C.lines.length(); i++) {
    char c = i < C.lines.length() ? C.lines[i] : ',';
    if (c == ',' || c == ' ' || c == ';') {
      cur.trim();
      if (cur.length() && lineCount < MAX_LINES) lineList[lineCount++] = cur;
      cur = "";
    } else cur += c;
  }
}

static void loadConfig() {
  prefs.begin("abfahrt", true);
  C.ssid = prefs.getString("ssid", "");
  C.pass = prefs.getString("pass", "");
  C.key = prefs.getString("key", "");
  C.stopId = prefs.getString("stop", "");
  C.stopName = prefs.getString("stopName", "");
  C.dirId = prefs.getString("dir", "");
  C.dirName = prefs.getString("dirName", "");
  C.lines = prefs.getString("lines", "");
  C.walk = prefs.getUChar("walk", DEFAULT_WALK_MIN);
  C.light = prefs.getUChar("light", 3);
  prefs.end();
  parseLines();
}

static void saveMenuSettings() {
  prefs.begin("abfahrt", false);
  prefs.putUChar("walk", C.walk);
  prefs.putUChar("light", C.light);
  prefs.end();
}

static bool configComplete() { return C.ssid.length() && C.key.length() && C.stopId.length(); }

// ---------- Abfahrten ----------------------------------------
static const int MAX_DEPS = 20;
struct Dep {
  char   line[6];
  time_t plan;        // Fahrplanzeit
  time_t rt;          // Echtzeit (0 = keine)
  bool   cancelled;
};
Dep deps[MAX_DEPS];
int depCount = 0;

time_t   lastOk = 0;           // letzter erfolgreicher Abruf
String   lastErr;              // Text fuer die Statuszeile
uint32_t errBackoff = 30;
uint32_t nextFetchMs = 0;

enum Screen { MAIN, SETTINGS };
Screen screen = MAIN;
String shownHeader, shownHero, shownPanel;

// ---------- Hilfen -------------------------------------------
static time_t eff(const Dep& d) { return d.rt ? d.rt : d.plan; }
static bool timeValid() { return time(nullptr) > 1700000000; }

static void fmtHM(time_t t, char* out) {
  struct tm lt; localtime_r(&t, &lt);
  sprintf(out, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

static bool inActiveHours() {
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt);
  return lt.tm_hour >= ACTIVE_FROM_H && lt.tm_hour < ACTIVE_TO_H;
}

static bool lineWanted(const char* l) {
  if (lineCount == 0) return true;
  for (int i = 0; i < lineCount; i++) if (lineList[i].equalsIgnoreCase(l)) return true;
  return false;
}

static time_t parseLocal(const char* date, const char* tim) {
  if (!date || !tim) return 0;
  int Y, M, D, h, m, s = 0;
  if (sscanf(date, "%d-%d-%d", &Y, &M, &D) != 3) return 0;
  if (sscanf(tim, "%d:%d:%d", &h, &m, &s) < 2) return 0;
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = s; t.tm_isdst = -1;
  return mktime(&t);
}

static String urlEncode(const String& s) {
  String o; const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else { o += '%'; o += hex[(c >> 4) & 15]; o += hex[c & 15]; }
  }
  return o;
}

static String jsonEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

// Text kuerzen, bis er in maxW Pixel passt (mit der gerade gesetzten Schrift)
static String fitText(TFT_eSPI& g, String s, int maxW) {
  if (g.textWidth(s) <= maxW) return s;
  while (s.length() > 1 && g.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
  return s + "...";
}

// Liniennummer aus der RMV-Antwort ziehen (Feldnamen je nach API-Version)
static String extractLine(JsonObject d) {
  const char* v = d["ProductAtStop"]["displayNumber"].as<const char*>();
  if (!v) v = d["ProductAtStop"]["line"].as<const char*>();
  JsonVariant p = d["Product"];
  if (!v && p.is<JsonArray>()) {
    v = p[0]["displayNumber"].as<const char*>();
    if (!v) v = p[0]["line"].as<const char*>();
  }
  if (!v && p.is<JsonObject>()) {
    v = p["displayNumber"].as<const char*>();
    if (!v) v = p["line"].as<const char*>();
  }
  if (v) return String(v);
  String n = d["name"] | "";
  n.trim();
  int sp = n.lastIndexOf(' ');
  return sp >= 0 ? n.substring(sp + 1) : n;
}

// ---------- RMV abfragen -------------------------------------
static bool fetchDepartures() {
  if (WiFi.status() != WL_CONNECTED) { lastErr = "kein WLAN"; return false; }

  String url = "https://www.rmv.de/hapi/departureBoard?format=json&maxJourneys=20&duration=120";
  url += "&accessId=" + urlEncode(C.key);
  url += "&id=" + urlEncode(C.stopId);
  if (C.dirId.length()) url += "&direction=" + urlEncode(C.dirId);

  WiFiClientSecure client;
  client.setInsecure();  // oeffentliche Fahrplandaten; kein Zertifikat hinterlegt
  HTTPClient http;
  http.useHTTP10(true);  // kein chunked encoding -> direkt aus dem Stream parsen
  http.setTimeout(10000);
  if (!http.begin(client, url)) { lastErr = "RMV nicht erreichbar"; return false; }
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[RMV] HTTP %d\n", code);
    lastErr = (code == 401 || code == 403) ? "RMV: Schluessel pruefen" : String("RMV: Fehler ") + code;
    http.end();
    return false;
  }

  JsonDocument filter;   // nur die benoetigten Felder behalten (spart RAM)
  filter["errorCode"] = true;
  filter["errorText"] = true;
  JsonObject f = filter["Departure"].add<JsonObject>();
  f["name"] = true;
  f["time"] = true;
  f["date"] = true;
  f["rtTime"] = true;
  f["rtDate"] = true;
  f["cancelled"] = true;
  f["ProductAtStop"]["line"] = true;
  f["ProductAtStop"]["displayNumber"] = true;
  f["Product"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { Serial.printf("[RMV] JSON: %s\n", err.c_str()); lastErr = "RMV: Antwort unlesbar"; return false; }
  if (doc["errorCode"].is<const char*>()) {
    String ec = doc["errorCode"].as<const char*>();
    Serial.printf("[RMV] Fehler %s: %s\n", ec.c_str(), doc["errorText"] | "");
    lastErr = ec.startsWith("API_AUTH") ? "RMV: Schluessel pruefen" : "RMV: " + ec;
    return false;
  }

  Dep tmp[MAX_DEPS]; int n = 0;
  for (JsonObject d : doc["Departure"].as<JsonArray>()) {
    if (n >= MAX_DEPS) break;
    String line = extractLine(d);
    Dep& x = tmp[n];
    strlcpy(x.line, line.c_str(), sizeof(x.line));
    const char* date = d["date"];
    x.plan = parseLocal(date, d["time"]);
    const char* rtT = d["rtTime"];
    const char* rtD = d["rtDate"] | date;
    x.rt = rtT ? parseLocal(rtD, rtT) : 0;
    JsonVariant c = d["cancelled"];
    x.cancelled = c.is<bool>() ? c.as<bool>() : (c.is<const char*>() && strcmp(c.as<const char*>(), "true") == 0);
    if (!x.plan) continue;
    Serial.printf("[RMV] Linie %-4s plan %s echt %s %s\n", x.line, (const char*)d["time"], rtT ? rtT : "-", x.cancelled ? "AUSFALL" : "");
    n++;
  }
  for (int i = 1; i < n; i++) {    // nach tatsaechlicher Abfahrt sortieren
    Dep k = tmp[i]; int j = i - 1;
    while (j >= 0 && eff(tmp[j]) > eff(k)) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  memcpy(deps, tmp, sizeof(Dep) * n);
  depCount = n;
  lastErr = "";
  return true;
}

// ---------- Was ist gerade relevant? --------------------------
struct View {
  int  hero = -1;        // Bahn, die du nimmst
  long heroSecs = 0;     // Sekunden bis zum Losgehen
  int  list[3];          // die 3 danach
  int  listCount = 0;
  int  skipped = -1;     // ausgefallene Bahn vor deiner
};

static View computeView() {
  View v;
  time_t now = time(nullptr);
  long walk = (long)C.walk * 60;
  for (int i = 0; i < depCount; i++) {
    const Dep& d = deps[i];
    if (!lineWanted(d.line)) continue;
    long secs = (long)(eff(d) - walk - now);
    if (secs < -30) continue;                 // nicht mehr erreichbar
    if (v.hero < 0) {
      if (d.cancelled) { if (v.skipped < 0) v.skipped = i; continue; }
      v.hero = i; v.heroSecs = secs;
    } else if (v.listCount < 3) {
      v.list[v.listCount++] = i;
    }
  }
  return v;
}

// ---------- Zeichnen -----------------------------------------
// Jede Zone wird im Zwischenspeicher gezeichnet und in einem Rutsch
// uebertragen -> kein Flackern. Reicht der Speicher nicht: direkt.
template <typename F>
static void drawZone(int x, int y, int w, int h, F draw) {
  TFT_eSprite spr(&tft);
  spr.setColorDepth(16);
  if (spr.createSprite(w, h)) {
    draw((TFT_eSPI&)spr, 0, 0);
    spr.pushSprite(x, y);
    spr.deleteSprite();
  } else {
    draw(tft, x, y);
  }
}

static void drawHeader() {
  char clock[6] = "--:--";
  if (timeValid()) fmtHM(time(nullptr), clock);
  if (shownHeader == clock) return;
  shownHeader = clock;
  drawZone(0, 0, 320, 46, [&](TFT_eSPI& g, int ox, int oy) {
    g.fillRect(ox, oy, 320, 46, COL_BG);
    g.setTextColor(COL_FG, COL_BG);
    g.setFreeFont(&FreeSansBold9pt7b);
    g.setTextDatum(TL_DATUM);
    g.drawString(fitText(g, C.stopName, 180), ox + 14, oy + 9);
    g.setTextFont(2);
    g.setTextColor(COL_SUB, COL_BG);
    String sub = C.dirId.length() ? "Richtung " + C.dirName : String("alle Richtungen");
    g.drawString(fitText(g, sub, 180), ox + 14, oy + 24);
    g.setTextColor(COL_FG, COL_BG);
    g.setFreeFont(&FreeSansBold18pt7b);
    g.setTextDatum(MR_DATUM);
    g.drawString(clock, ox + 306, oy + 22);
    g.fillRect(ox + 14, oy + 42, 292, 2, COL_FG);
  });
}

static void drawHero(const View& v) {
  time_t now = time(nullptr);
  bool stale = lastOk == 0 || (now - lastOk) > 180;
  int mins = v.hero < 0 ? -1 : (v.heroSecs <= 0 ? 0 : (int)(v.heroSecs / 60));

  String sig = String(mins) + "|" + v.hero + "|" + v.skipped + "|" + stale + "|" + depCount + "|" + lastErr;
  if (v.hero >= 0) sig += "|" + String((long)deps[v.hero].rt);
  if (sig == shownHero) return;
  shownHero = sig;

  drawZone(10, 46, 300, 98, [&](TFT_eSPI& g, int ox, int oy) {
    g.fillRect(ox, oy, 300, 98, COL_BG);

    uint16_t urg = mins < 0 ? COL_SUB : (mins <= 1 ? COL_RED : (mins <= 5 ? COL_ORANGE : COL_GREEN));
    g.fillCircle(ox + 8, oy + 12, 4, urg);
    g.drawCircle(ox + 8, oy + 12, 5, COL_FG);
    g.setTextFont(2);
    g.setTextDatum(ML_DATUM);
    g.setTextColor(COL_FG, COL_BG);
    g.drawString(mins == 0 ? "JETZT LOS" : "LOS IN", ox + 18, oy + 12);

    // grosse Minutenzahl – rot ab RED_AT_MIN
    uint16_t numCol = (mins >= 0 && mins <= RED_AT_MIN) ? COL_RED : COL_FG;
    g.setTextColor(numCol, COL_BG);
    g.setTextDatum(TL_DATUM);
    char num[8]; const char* unit = "min";
    int numW, baseY;
    if (mins < 0) {
      strcpy(num, "--");
      numW = g.drawString(num, ox + 2, oy + 22, 8); baseY = oy + 94;
    } else if (mins < 100) {
      sprintf(num, "%d", mins);
      numW = g.drawString(num, ox + 2, oy + 22, 8); baseY = oy + 94;
    } else {                                   // >= 100 min: h:mm, kleinere Schrift
      sprintf(num, "%d:%02d", mins / 60, mins % 60); unit = "h";
      numW = g.drawString(num, ox + 2, oy + 40, 6); baseY = oy + 86;
    }
    g.setTextColor(COL_FG, COL_BG);
    g.setFreeFont(&FreeSansBold12pt7b);
    g.setTextDatum(BL_DATUM);
    g.drawString(unit, ox + 2 + numW + 6, baseY - 4);

    // rechts: schwarze Kapsel mit Linie + Abfahrtszeit
    if (v.hero >= 0) {
      const Dep& d = deps[v.hero];
      char hm[6]; fmtHM(d.plan, hm);
      g.setFreeFont(&FreeSansBold12pt7b);
      int tw = g.textWidth(hm);
      g.setTextFont(2);
      int bw = max(22, (int)g.textWidth(d.line) + 10);
      int pw = 4 + bw + 6 + tw + 10, px = ox + 296 - pw, py = oy + 16;
      g.fillRoundRect(px, py, pw, 30, 12, COL_FG);
      g.fillRoundRect(px + 4, py + 4, bw, 22, 8, COL_BG);
      g.setTextDatum(MC_DATUM);
      g.setTextColor(COL_FG, COL_BG);
      g.drawString(d.line, px + 4 + bw / 2, py + 15);
      g.setFreeFont(&FreeSansBold12pt7b);
      g.setTextDatum(ML_DATUM);
      g.setTextColor(COL_BG, COL_FG);
      g.drawString(hm, px + 4 + bw + 6, py + 14);

      g.setTextFont(2);
      g.setTextDatum(TR_DATUM);
      g.setTextColor(COL_SUB, COL_BG);
      if (d.rt) {
        long delay = (long)(d.rt - d.plan) / 60;
        if (delay > 0) g.drawString(String("+") + delay + " min", ox + 296, oy + 52);
        else if (delay < 0) g.drawString(String(delay) + " min", ox + 296, oy + 52);
      } else {
        g.drawString("Fahrplan", ox + 296, oy + 52);
      }
    }

    // Statuszeile: Ausfall / Fehler / offline / keine Bahn
    g.setTextFont(2);
    g.setTextDatum(TR_DATUM);
    if (v.skipped >= 0) {
      char hm[6]; fmtHM(deps[v.skipped].plan, hm);
      g.setTextColor(COL_RED, COL_BG);
      g.drawString(String("Ausfall ") + deps[v.skipped].line + " " + hm, ox + 296, oy + 72);
    } else if (stale) {
      g.setTextColor(COL_SUB, COL_BG);
      String s;
      if (lastErr.length()) s = lastErr;
      else if (lastOk) { char hm[6]; fmtHM(lastOk, hm); s = String("offline, Stand ") + hm; }
      else s = "warte auf Daten";
      g.drawString(fitText(g, s, 140), ox + 296, oy + 72);
    } else if (v.hero < 0) {
      g.setTextColor(COL_SUB, COL_BG);
      g.drawString("keine Abfahrt", ox + 296, oy + 72);
    }
  });
}

static void drawPanel(const View& v) {
  time_t now = time(nullptr);
  long walk = (long)C.walk * 60;
  String sig;
  for (int k = 0; k < v.listCount; k++) {
    const Dep& d = deps[v.list[k]];
    long m = (long)(eff(d) - walk - now) / 60;
    sig += String(d.line) + (long)d.plan + "/" + (long)d.rt + d.cancelled + ":" + m + ";";
  }
  if (sig == shownPanel) return;
  shownPanel = sig;

  drawZone(10, 146, 300, 86, [&](TFT_eSPI& g, int ox, int oy) {
    g.fillRect(ox, oy, 300, 86, COL_BG);
    g.fillRoundRect(ox, oy, 300, 86, 16, COL_PANEL);
    if (v.listCount == 0) {
      g.setTextFont(2); g.setTextDatum(MC_DATUM);
      g.setTextColor(COL_PANEL_SUB, COL_PANEL);
      g.drawString("keine weiteren Abfahrten", ox + 150, oy + 43);
      return;
    }
    for (int k = 0; k < v.listCount; k++) {
      const Dep& d = deps[v.list[k]];
      int cy = oy + 18 + k * 25;
      uint16_t txt = d.cancelled ? COL_PANEL_SUB : COL_PANEL_FG;
      g.setTextFont(2);
      int bw = max(22, (int)g.textWidth(d.line) + 8);
      g.fillRoundRect(ox + 10, cy - 9, bw, 18, 6, txt);
      g.setTextDatum(MC_DATUM);
      g.setTextColor(COL_PANEL, txt);
      g.drawString(d.line, ox + 10 + bw / 2, cy + 1);

      char hm[6]; fmtHM(d.plan, hm);
      g.setFreeFont(&FreeSansBold9pt7b);
      g.setTextDatum(ML_DATUM);
      g.setTextColor(txt, COL_PANEL);
      int tx = ox + 18 + bw;
      int w = g.drawString(hm, tx, cy);
      if (d.cancelled) g.drawFastHLine(tx, cy, w, txt);

      g.setTextFont(2);
      if (!d.cancelled && d.rt && d.rt != d.plan) {
        long delay = (long)(d.rt - d.plan) / 60;
        g.setTextColor(COL_PANEL_SUB, COL_PANEL);
        g.drawString(String(delay > 0 ? "+" : "") + delay, tx + w + 4, cy + 1);
      }
      g.setTextDatum(MR_DATUM);
      g.setTextColor(COL_PANEL_SUB, COL_PANEL);
      if (d.cancelled) {
        g.drawString("Ausfall", ox + 290, cy + 1);
      } else {
        long m = (long)(eff(d) - walk - now) / 60;
        if (m <= 0) g.drawString("jetzt los", ox + 290, cy + 1);
        else g.drawString(String("los in ") + m + " min", ox + 290, cy + 1);
      }
    }
  });
}

static void setLed(bool warn) {
#if LED_WARNUNG
  digitalWrite(LED_R, warn ? LOW : HIGH);  // LED ist low-aktiv
#else
  (void)warn;
#endif
}

static void drawMain(bool force = false) {
  if (force) { shownHeader = ""; shownHero = ""; shownPanel = ""; tft.fillScreen(COL_BG); }
  View v = computeView();
  drawHeader();
  drawHero(v);
  drawPanel(v);
  int mins = v.hero < 0 ? -1 : (int)(v.heroSecs / 60);
  setLed(mins >= 0 && mins <= RED_AT_MIN);
}

static void showMessage(const char* l1, const String& l2 = "") {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_FG, COL_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(l1, 160, 105);
  tft.setTextFont(2);
  tft.setTextColor(COL_SUB, COL_BG);
  tft.drawString(l2, 160, 135);
}

// ---------- Helligkeit ----------------------------------------
static void setBacklight(uint8_t level) {  // 0..255
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, level);
#else
  ledcWrite(0, level);
#endif
}
static void applyBrightness() {
  static const uint8_t lv[] = {0, 40, 120, 255};
  uint8_t b = lv[constrain(C.light, 1, 3)];
  if (timeValid() && !inActiveHours()) b = 20;  // nachts gedimmt
  setBacklight(b);
}

// ---------- Touch ---------------------------------------------
static bool readTouch(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320), 0, 319);
  y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240), 0, 239);
  return true;
}

struct Btn { int x, y, w, h; };
static bool hit(const Btn& b, int x, int y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; }

static void drawButton(const Btn& b, const char* label, uint16_t bg, uint16_t fg) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 12, bg);
  tft.setTextFont(2); tft.setTextDatum(MC_DATUM); tft.setTextColor(fg, bg);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2 + 1);
}

// ============================================================
//  Einrichtungs-WLAN (Captive Portal)
// ============================================================
WebServer* web = nullptr;
DNSServer* dns = nullptr;
String scanJson = "[]";
uint32_t portalSavedAt = 0;

static void scanNetworks() {
  int n = WiFi.scanNetworks();
  String out = "[";
  int added = 0;
  for (int i = 0; i < n; i++) {            // Ergebnisse sind nach Signalstaerke sortiert
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == s) { dup = true; break; }
    if (dup) continue;
    out += (added++ ? ",\"" : "\"") + jsonEsc(s) + "\"";
  }
  out += "]";
  WiFi.scanDelete();
  scanJson = out;
}

static int qrX, qrY, qrBox;
static void qrDraw(esp_qrcode_handle_t q) {
  int size = esp_qrcode_get_size(q);
  int scale = max(1, qrBox / (size + 4));
  int total = (size + 4) * scale;
  tft.fillRoundRect(qrX, qrY, total, total, 6, COL_WHITE);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++)
      if (esp_qrcode_get_module(q, x, y))
        tft.fillRect(qrX + (x + 2) * scale, qrY + (y + 2) * scale, scale, scale, COL_FG);
}

static const Btn B_CANCEL = {14, 190, 120, 40};

static void drawPortalScreen(bool canCancel) {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_FG, COL_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Einrichtung", 14, 12);
  tft.fillRect(14, 42, 292, 2, COL_FG);

  tft.setTextFont(2);
  tft.setTextColor(COL_SUB, COL_BG);
  tft.drawString("1. Mit dem Handy ins WLAN:", 14, 54);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString("Abfahrtsmonitor-", 14, 74);
  tft.drawString("Setup", 14, 94);
  tft.setTextFont(2);
  tft.setTextColor(COL_SUB, COL_BG);
  tft.drawString("2. Die Seite oeffnet sich", 14, 122);
  tft.drawString("   von selbst, sonst im", 14, 138);
  tft.drawString("   Browser: 192.168.4.1", 14, 154);

  // QR-Code zum Verbinden mit dem Einrichtungs-WLAN
  qrX = 188; qrY = 58; qrBox = 120;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = qrDraw;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  String wifiQr = String("WIFI:T:nopass;S:") + SETUP_AP_NAME + ";;";
  esp_qrcode_generate(&cfg, wifiQr.c_str());
  tft.setTextDatum(TC_DATUM);
  tft.drawString("scannen", 248, 184);

  if (canCancel) drawButton(B_CANCEL, "Abbrechen", COL_PANEL, COL_PANEL_FG);
}

[[noreturn]] static void runPortal(bool canCancel) {
  Serial.println("[Setup] starte Einrichtungs-WLAN");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  delay(200);
  showMessage("Suche WLAN-Netze", "einen Moment");
  scanNetworks();
  WiFi.softAP(SETUP_AP_NAME);
  delay(200);
  IPAddress ip = WiFi.softAPIP();

  dns = new DNSServer();
  dns->start(53, "*", ip);
  web = new WebServer(80);

  web->on("/", HTTP_GET, []() { web->send_P(200, "text/html; charset=utf-8", PORTAL_HTML); });
  web->on("/stops.csv", HTTP_GET, []() {
    web->sendHeader("Content-Encoding", "gzip");
    web->sendHeader("Cache-Control", "max-age=86400");
    web->send_P(200, "text/plain; charset=utf-8", (const char*)STOPS_GZ, STOPS_GZ_LEN);
  });
  web->on("/scan", HTTP_GET, []() {
    if (web->hasArg("refresh")) scanNetworks();
    web->send(200, "application/json", scanJson);
  });
  web->on("/config", HTTP_GET, []() {
    String j = "{";
    j += "\"ssid\":\"" + jsonEsc(C.ssid) + "\",";
    j += "\"hasPass\":" + String(C.pass.length() ? "true" : "false") + ",";
    j += "\"keyTail\":\"" + jsonEsc(C.key.length() > 4 ? C.key.substring(C.key.length() - 4) : C.key) + "\",";
    j += "\"stop\":\"" + jsonEsc(C.stopId) + "\",";
    j += "\"dir\":\"" + jsonEsc(C.dirId) + "\",";
    j += "\"lines\":\"" + jsonEsc(C.lines) + "\",";
    j += "\"walk\":" + String(C.walk) + "}";
    web->send(200, "application/json", j);
  });
  web->on("/save", HTTP_POST, []() {
    String ssid = web->arg("ssid"), pass = web->arg("pass"), key = web->arg("key");
    String stop = web->arg("stop");
    if (!ssid.length() || !stop.length() || (!key.length() && !C.key.length())) {
      web->send(400, "text/plain", "Angaben unvollstaendig.");
      return;
    }
    prefs.begin("abfahrt", false);
    // Passwort leer lassen = altes behalten (nur beim gleichen WLAN)
    if (pass.length() || ssid != C.ssid) prefs.putString("pass", pass);
    prefs.putString("ssid", ssid);
    if (key.length()) prefs.putString("key", key);
    prefs.putString("stop", stop);
    prefs.putString("stopName", web->arg("stopName"));
    prefs.putString("dir", web->arg("dir"));
    prefs.putString("dirName", web->arg("dirName"));
    prefs.putString("lines", web->arg("lines"));
    int w = web->arg("walk").toInt();
    if (w >= 1 && w <= 45) prefs.putUChar("walk", (uint8_t)w);
    prefs.end();
    web->send(200, "text/plain", "OK");
    portalSavedAt = millis();
  });
  // Handys pruefen mit diesen Adressen, ob Internet da ist -> auf die Einrichtungsseite umleiten
  web->onNotFound([]() {
    web->sendHeader("Location", "http://192.168.4.1/", true);
    web->send(302, "text/plain", "");
  });
  web->begin();

  setBacklight(255);
  drawPortalScreen(canCancel);

  bool wasT = false;
  while (true) {
    dns->processNextRequest();
    web->handleClient();
    if (portalSavedAt) {
      static bool shown = false;
      if (!shown) { showMessage("Gespeichert", "starte neu ..."); shown = true; }
      if (millis() - portalSavedAt > 2000) ESP.restart();
    }
    int x, y;
    bool t = readTouch(x, y);
    if (canCancel && t && !wasT && hit(B_CANCEL, x, y)) ESP.restart();
    wasT = t;
    delay(2);
  }
}

// ============================================================
//  Einstellungs-Menue (lange auf die Uhr druecken)
// ============================================================
static const Btn B_DONE  = {226, 8, 84, 36};
static const Btn B_MINUS = {22, 84, 64, 52};
static const Btn B_PLUS  = {234, 84, 64, 52};
static const Btn B_LIGHT = {10, 160, 110, 48};
static const Btn B_SETUP = {130, 160, 180, 48};

static void drawSettings() {
  tft.fillScreen(COL_BG);
  tft.setTextColor(COL_FG, COL_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("Einstellungen", 14, 26);
  drawButton(B_DONE, "Fertig", COL_FG, COL_BG);

  // Gehzeit
  tft.fillRoundRect(10, 52, 300, 96, 16, COL_PANEL);
  tft.setTextFont(2);
  tft.setTextColor(COL_PANEL_SUB, COL_PANEL); tft.setTextDatum(ML_DATUM);
  tft.drawString("Gehzeit zur Haltestelle", 24, 68);
  tft.fillRoundRect(B_MINUS.x, B_MINUS.y, B_MINUS.w, B_MINUS.h, 14, COL_BTN);
  tft.fillRoundRect(B_PLUS.x, B_PLUS.y, B_PLUS.w, B_PLUS.h, 14, COL_BTN);
  tft.setTextColor(COL_PANEL_FG, COL_BTN);
  tft.setFreeFont(&FreeSansBold18pt7b); tft.setTextDatum(MC_DATUM);
  tft.drawString("-", B_MINUS.x + B_MINUS.w / 2, B_MINUS.y + B_MINUS.h / 2 - 2);
  tft.drawString("+", B_PLUS.x + B_PLUS.w / 2, B_PLUS.y + B_PLUS.h / 2);
  tft.setTextColor(COL_PANEL_FG, COL_PANEL);
  char w[4]; sprintf(w, "%d", C.walk);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(w, 172, 112, 6);
  tft.setTextFont(2); tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_PANEL_SUB, COL_PANEL);
  tft.drawString("min", 178, 120);

  drawButton(B_LIGHT, (String("Licht ") + C.light + "/3").c_str(), COL_PANEL, COL_PANEL_FG);
  drawButton(B_SETUP, "Neu einrichten", COL_BG, COL_FG);
  tft.drawRoundRect(B_SETUP.x, B_SETUP.y, B_SETUP.w, B_SETUP.h, 12, COL_FG);
  tft.drawRoundRect(B_SETUP.x + 1, B_SETUP.y + 1, B_SETUP.w - 2, B_SETUP.h - 2, 11, COL_FG);

  tft.setTextColor(COL_SUB, COL_BG); tft.setTextDatum(MC_DATUM);
  tft.drawString("WLAN, Haltestelle, Linien, Schluessel", 160, 224);
}

static uint32_t touchStart = 0;
static bool wasTouched = false;
static uint32_t settingsSince = 0;

static void handleTouch() {
  int x, y;
  bool t = readTouch(x, y);
  uint32_t ms = millis();

  if (screen == MAIN) {
    if (t && !wasTouched) { touchStart = ms; Serial.printf("[Touch] %d,%d\n", x, y); }
    if (t && y < 50 && ms - touchStart > 1200) {   // langer Druck auf die Kopfzeile
      screen = SETTINGS; settingsSince = ms; drawSettings();
      while (ts.touched()) delay(10);
      t = false;
    }
  } else {
    if (t && !wasTouched) {
      settingsSince = ms;
      bool redraw = true;
      if (hit(B_MINUS, x, y)) C.walk = max(1, C.walk - 1);
      else if (hit(B_PLUS, x, y)) C.walk = min(45, C.walk + 1);
      else if (hit(B_LIGHT, x, y)) { C.light = C.light % 3 + 1; applyBrightness(); }
      else if (hit(B_SETUP, x, y)) { saveMenuSettings(); runPortal(true); }
      else if (hit(B_DONE, x, y)) { saveMenuSettings(); screen = MAIN; drawMain(true); redraw = false; }
      else redraw = false;
      if (redraw) drawSettings();
    }
    if (screen == SETTINGS && ms - settingsSince > 30000) {  // Zeitueberschreitung
      saveMenuSettings(); screen = MAIN; drawMain(true);
    }
  }
  wasTouched = t;
}

// ---------- WLAN & Zeit ---------------------------------------
static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(C.ssid.c_str(), C.pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.printf("[WLAN] %s %s\n", ok ? "verbunden" : "FEHLER", WiFi.localIP().toString().c_str());
  return ok;
}

// WLAN nicht erreichbar: erneut versuchen oder neu einrichten
static const Btn B_RETRY = {14, 170, 140, 48};
static const Btn B_RESET = {166, 170, 140, 48};
static void waitForWifi() {
  while (!connectWifi()) {
    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_FG, COL_BG);
    tft.setFreeFont(&FreeSansBold12pt7b); tft.setTextDatum(TL_DATUM);
    tft.drawString("WLAN nicht erreichbar", 14, 20);
    tft.setTextFont(2); tft.setTextColor(COL_SUB, COL_BG);
    tft.drawString(C.ssid, 14, 60);
    tft.drawString("Neuer Versuch in 60 Sekunden.", 14, 90);
    drawButton(B_RETRY, "Nochmal", COL_PANEL, COL_PANEL_FG);
    drawButton(B_RESET, "Neu einrichten", COL_FG, COL_BG);
    uint32_t t0 = millis();
    while (millis() - t0 < 60000) {
      int x, y;
      if (readTouch(x, y)) {
        if (hit(B_RESET, x, y)) runPortal(true);
        if (hit(B_RETRY, x, y)) break;
      }
      delay(20);
    }
    showMessage("Verbinde WLAN", C.ssid);
  }
}

static void startTime() {
  sntp_set_sync_interval(NTP_RESYNC_MIN * 60UL * 1000UL);
  configTzTime(TZ_INFO, NTP_1, NTP_2, NTP_3);  // Zeitzone inkl. Sommer/Winterzeit
  uint32_t t0 = millis();
  while (!timeValid() && millis() - t0 < 15000) delay(200);
}

// ---------- Programmablauf ------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(INVERT_DISPLAY);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
#endif
  setBacklight(255);

  touchSpi.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  loadConfig();
  if (!configComplete()) runPortal(false);     // erster Start: Einrichtung

  showMessage("Verbinde WLAN", C.ssid);
  waitForWifi();
  showMessage("Hole Uhrzeit", "Zeitserver");
  startTime();
  showMessage("Hole Abfahrten", C.stopName);
  bool ok = fetchDepartures();
  if (ok) lastOk = time(nullptr);
  nextFetchMs = millis() + (ok ? INTERVAL_NORMAL_S : errBackoff) * 1000UL;
  applyBrightness();
  drawMain(true);
}

void loop() {
  handleTouch();
  if (screen != MAIN) { delay(15); return; }

  uint32_t ms = millis();
  if ((int32_t)(ms - nextFetchMs) >= 0) {
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    bool ok = fetchDepartures();
    uint32_t wait;
    if (ok) {
      lastOk = time(nullptr); errBackoff = 30;
      View v = computeView();
      if (!inActiveHours()) wait = INTERVAL_NIGHT_S;
      else if (v.hero >= 0 && v.heroSecs < 15 * 60) wait = INTERVAL_FAST_S;
      else wait = INTERVAL_NORMAL_S;
    } else {
      wait = errBackoff;
      errBackoff = min<uint32_t>(errBackoff * 2, INTERVAL_ERROR_MAX);
    }
    nextFetchMs = millis() + wait * 1000UL;
    shownHero = ""; shownPanel = "";
    applyBrightness();
  }

  static uint32_t lastTick = 0;
  if (ms - lastTick >= 1000) {       // Countdown laeuft lokal jede Sekunde weiter
    lastTick = ms;
    drawMain();
  }
  delay(15);
}
