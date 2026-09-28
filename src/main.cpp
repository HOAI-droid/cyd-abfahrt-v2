// ============================================================
//  Abfahrtsmonitor fuer das CYD (ESP32-2432S028R, 320x240)
//  Design "Stein Anthrazit" – Daten live vom RMV
//  Version 2: Nachtmodus, Wetterseite, Tagesblatt (Muell, Feiertage, Ferien)
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
#include "streets_data.h"

// ---------- Farben (Design "Stein Anthrazit", Tag + Nacht) ------
static constexpr uint16_t rgb(uint32_t c) {
  return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}
struct Palette {
  uint16_t bg, fg, sub, panel, pfg, psub, btn, red, orange, green, rain, prain, warnfg, otext;
  uint16_t bin[4];  // Restmuell, Bio, Papier, Gelbe Tonne
};
static const Palette PAL_DAY = {
  rgb(0xCFCBBF), rgb(0x111111), rgb(0x4A473F), rgb(0x3A3833), rgb(0xF2F1EC), rgb(0xB8B4A9),
  rgb(0x4F4C45), rgb(0xC62D1F), rgb(0xF29D0A), rgb(0x1F9D55), rgb(0x2F6690), rgb(0x86ADC9),
  rgb(0xFFFFFF), rgb(0xA85A00), {rgb(0x3C3C3A), rgb(0x7A4E24), rgb(0x2F5FA8), rgb(0xE0B000)}};
// Nacht: Helligkeit invertiert (warmer Farbton bleibt), abgedunkelt; Tafel bleibt dunkel
static const Palette PAL_NIGHT = {
  rgb(0x26241D), rgb(0x8F8F8F), rgb(0x73716D), rgb(0x34322D), rgb(0x8F8F8F), rgb(0x73716D),
  rgb(0x46443E), rgb(0x9E2419), rgb(0xC27E08), rgb(0x197E44), rgb(0x6B8AA1), rgb(0x6B8AA1),
  rgb(0xE8E6E0), rgb(0xC27E08), {rgb(0x6E6C66), rgb(0x7B552D), rgb(0x3B62A0), rgb(0xB38D00)}};
static Palette P = PAL_DAY;
static bool nightMode = false;

#define COL_BG        (P.bg)
#define COL_FG        (P.fg)
#define COL_SUB       (P.sub)
#define COL_PANEL     (P.panel)
#define COL_PANEL_FG  (P.pfg)
#define COL_PANEL_SUB (P.psub)
#define COL_BTN       (P.btn)
#define COL_RED       (P.red)
#define COL_ORANGE    (P.orange)
#define COL_GREEN     (P.green)
#define COL_RAIN      (P.rain)
#define COL_PRAIN     (P.prain)
static const uint16_t COL_WHITE = rgb(0xFFFFFF);
static const uint16_t COL_DARK  = rgb(0x111111);

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
  String stopId, stopName, dirId, dirName, place;
  String lines;          // z. B. "1, 6" – leer = alle
  uint8_t walk = DEFAULT_WALK_MIN;
  uint8_t light = 3;     // 1..3
  float lat = 0, lon = 0;
  String street, streetVal, hnr;   // Muellkalender
  uint8_t bins = 15;     // Bit 0 Rest, 1 Bio, 2 Papier, 3 Gelb
  uint8_t rest = 2;      // Restmuell-Leerung: 1 = woechentlich, 2 = alle 2, 4 = alle 4 Wochen
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
  C.place = prefs.getString("place", "");
  C.lines = prefs.getString("lines", "");
  C.walk = prefs.getUChar("walk", DEFAULT_WALK_MIN);
  C.light = prefs.getUChar("light", 3);
  C.lat = prefs.getFloat("lat", 0);
  C.lon = prefs.getFloat("lon", 0);
  C.street = prefs.getString("street", "");
  C.streetVal = prefs.getString("streetVal", "");
  C.hnr = prefs.getString("hnr", "");
  C.bins = prefs.getUChar("bins", 15);
  C.rest = prefs.getUChar("rest", 2);
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

// Stoerungsmeldung des RMV zu deinen Linien
String msgHead, msgText, msgLines, msgUntil;

enum Screen { MAIN, SETTINGS, MESSAGE };
Screen screen = MAIN;
enum Page { PG_DEP = 0, PG_WX = 1, PG_DAY = 2 };
int page = PG_DEP;
uint32_t pageSince = 0;
String shownHeader, shownHero, shownPanel, shownDots;

// ---------- Hilfen -------------------------------------------
static time_t eff(const Dep& d) { return d.rt ? d.rt : d.plan; }
static bool timeValid() { return time(nullptr) > 1700000000; }

static void fmtHM(time_t t, char* out) {
  struct tm lt; localtime_r(&t, &lt);
  sprintf(out, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

static struct tm nowTm() {
  time_t now = time(nullptr); struct tm lt; localtime_r(&now, &lt); return lt;
}

static bool inActiveHours() {
  struct tm lt = nowTm();
  return lt.tm_hour >= ACTIVE_FROM_H && lt.tm_hour < ACTIVE_TO_H;
}

static bool isNightTime() {
  if (!timeValid()) return false;
  struct tm lt = nowTm();
  int m = lt.tm_hour * 60 + lt.tm_min;
  return m >= NIGHT_FROM_MIN || m < NIGHT_TO_MIN;
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

// ISO-Zeit "2026-09-28T16:00:00+02:00" als Ortszeit (Offset wird ignoriert)
static time_t parseIso(const char* s) {
  if (!s || strlen(s) < 16) return 0;
  char d[11], t[9];
  memcpy(d, s, 10); d[10] = 0;
  memcpy(t, s + 11, 8); t[8] = 0;
  return parseLocal(d, t);
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

// UTF-8 -> ASCII fuer das Display (eingebaute Schriften ohne Umlaute)
static String toAscii(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < 0x80) { o += (char)c; continue; }
    if (c == 0xC3 && i + 1 < s.length()) {
      uint8_t d = s[++i];
      switch (d) {
        case 0x84: o += "Ae"; break; case 0x96: o += "Oe"; break; case 0x9C: o += "Ue"; break;
        case 0xA4: o += "ae"; break; case 0xB6: o += "oe"; break; case 0xBC: o += "ue"; break;
        case 0x9F: o += "ss"; break;
        case 0xA9: case 0xA8: o += 'e'; break;
        default: break;
      }
      continue;
    }
    if (c == 0xE2 && i + 2 < s.length() && (uint8_t)s[i + 1] == 0x80 &&
        ((uint8_t)s[i + 2] == 0x93 || (uint8_t)s[i + 2] == 0x94)) { o += '-'; i += 2; continue; }
    // sonstige Mehrbyte-Zeichen ueberspringen
    if (c >= 0xF0) i += 3; else if (c >= 0xE0) i += 2; else if (c >= 0xC0) i += 1;
  }
  return o;
}

// "STARKES GEWITTER" -> "Starkes Gewitter"
static String titleCase(const String& s) {
  String o = s; bool start = true;
  for (size_t i = 0; i < o.length(); i++) {
    char c = o[i];
    if (isalpha((unsigned char)c)) { o[i] = start ? toupper(c) : tolower(c); start = false; }
    else start = (c == ' ' || c == '-' || c == '/');
  }
  return o;
}

static String stripTags(const String& s) {
  String o; bool tag = false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') { tag = true; o += ' '; continue; }
    if (c == '>') { tag = false; continue; }
    if (!tag) o += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
  }
  o.replace("&amp;", "&"); o.replace("&nbsp;", " "); o.replace("&quot;", "\"");
  while (o.indexOf("  ") >= 0) o.replace("  ", " ");
  o.trim();
  return o;
}

// Text kuerzen, bis er in maxW Pixel passt (mit der gerade gesetzten Schrift)
static String fitText(TFT_eSPI& g, String s, int maxW) {
  if (g.textWidth(s) <= maxW) return s;
  while (s.length() > 1 && g.textWidth(s + "...") > maxW) s.remove(s.length() - 1);
  return s + "...";
}

// ---------- Kalender-Hilfen (Tage seit 1970) -------------------
static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
static long ymdToDay(uint32_t ymd) { return daysFromCivil(ymd / 10000, (ymd / 100) % 100, ymd % 100); }
static long todayNum() { struct tm lt = nowTm(); return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday); }
static uint32_t dayToYmd(long z) {
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  long d = doy - (153 * mp + 2) / 5 + 1;
  long m = mp + (mp < 10 ? 3 : -9);
  return (uint32_t)((y + (m <= 2)) * 10000 + m * 100 + d);
}
static int weekdayOf(long dn) { return (int)((dn % 7 + 11) % 7); }  // 0 = Sonntag
static int isoWeek(long dn) {
  int wdMon = (weekdayOf(dn) + 6) % 7;             // 0 = Montag
  long thu = dn - wdMon + 3;
  uint32_t ymd = dayToYmd(thu);
  long jan1 = daysFromCivil(ymd / 10000, 1, 1);
  return (int)((thu - jan1) / 7 + 1);
}
static const char* WD_LONG[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
static const char* WD_SHORT[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
static const char* MONTHS[] = {"Januar", "Februar", "Maerz", "April", "Mai", "Juni", "Juli",
                               "August", "September", "Oktober", "November", "Dezember"};
static String fmtDM(uint32_t ymd) {
  char b[12]; snprintf(b, sizeof(b), "%02u.%02u.", (unsigned)(ymd % 100), (unsigned)((ymd / 100) % 100)); return b;
}
static uint32_t parseYmd(const char* s) {   // "2026-10-03" oder "20261003"
  if (!s) return 0;
  int y, m, d;
  if (sscanf(s, "%d-%d-%d", &y, &m, &d) == 3) return y * 10000 + m * 100 + d;
  if (strlen(s) >= 8 && sscanf(s, "%4d%2d%2d", &y, &m, &d) == 3) return y * 10000 + m * 100 + d;
  return 0;
}

// ---------- HTTPS-Hilfe ---------------------------------------
static int httpGetJson(const String& url, JsonDocument& doc, JsonDocument* filter, int timeout = 12000) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client;
  client.setInsecure();  // oeffentliche Daten; kein Zertifikat hinterlegt
  HTTPClient http;
  http.useHTTP10(true);  // kein chunked encoding -> direkt aus dem Stream parsen
  http.setTimeout(timeout);
  if (!http.begin(client, url)) return -2;
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code != 200) { http.end(); return code; }
  DeserializationError err = filter
    ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
    : deserializeJson(doc, http.getStream());
  http.end();
  if (err) { Serial.printf("[HTTP] JSON %s: %s\n", url.c_str(), err.c_str()); return -3; }
  return 200;
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

// Koordinaten + Ortsname aus der HAFAS-Kennung "A=1@O=...@X=8651234@Y=49912345@..."
static void takeStopId(const char* sid) {
  if (!sid) return;
  String s = sid;
  int xi = s.indexOf("@X="), yi = s.indexOf("@Y=");
  if (xi < 0 || yi < 0) return;
  float lon = s.substring(xi + 3).toInt() / 1e6f, lat = s.substring(yi + 3).toInt() / 1e6f;
  if (lat < 40 || lat > 60 || lon < 0 || lon > 20) return;
  bool changed = fabsf(lat - C.lat) > 0.001f || fabsf(lon - C.lon) > 0.001f;
  String place;
  if (!C.place.length()) {
    int oi = s.indexOf("O=");
    if (oi >= 0) {
      String o = toAscii(s.substring(oi + 2, s.indexOf('@', oi)));
      if (C.stopName.length() && o.endsWith(C.stopName)) o = o.substring(0, o.length() - C.stopName.length());
      o.trim();
      if (o.length()) place = o;
    }
  }
  if (!changed && !place.length()) return;
  prefs.begin("abfahrt", false);
  if (changed) { C.lat = lat; C.lon = lon; prefs.putFloat("lat", lat); prefs.putFloat("lon", lon); }
  if (place.length()) { C.place = place; prefs.putString("place", place); }
  prefs.end();
  Serial.printf("[Ort] %.4f %.4f %s\n", C.lat, C.lon, C.place.c_str());
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
  http.useHTTP10(true);
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
  f["stopid"] = true;
  f["ProductAtStop"]["line"] = true;
  f["ProductAtStop"]["displayNumber"] = true;
  f["Product"] = true;
  JsonObject fm = f["Messages"]["Message"].add<JsonObject>();
  fm["head"] = true;
  fm["text"] = true;
  fm["act"] = true;
  fm["eDate"] = true;

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
  String mHead, mText, mLines, mUntil;
  bool coordsTaken = false;
  for (JsonObject d : doc["Departure"].as<JsonArray>()) {
    if (!coordsTaken) { takeStopId(d["stopid"].as<const char*>()); coordsTaken = true; }
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

    // Meldungen (Bauarbeiten, Umleitung ...) zu den gewuenschten Linien
    if (lineWanted(x.line)) {
      for (JsonObject m : d["Messages"]["Message"].as<JsonArray>()) {
        JsonVariant act = m["act"];
        if (act.is<bool>() && !act.as<bool>()) continue;
        String head = toAscii(stripTags(String(m["head"] | "")));
        if (!head.length()) continue;
        if (!mHead.length()) {
          mHead = head;
          mText = toAscii(stripTags(String(m["text"] | "")));
          uint32_t e = parseYmd(m["eDate"] | "");
          mUntil = e ? fmtDM(e) : String("");
        }
        if (head == mHead) {
          String l = x.line;
          if ((", " + mLines + ",").indexOf(", " + l + ",") < 0) mLines += (mLines.length() ? ", " : "") + l;
        }
        break;
      }
    }
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
  msgHead = mHead; msgText = mText; msgLines = mLines; msgUntil = mUntil;
  if (msgHead.length()) Serial.printf("[RMV] Meldung (%s): %s\n", msgLines.c_str(), msgHead.c_str());
  lastErr = "";
  return true;
}

// Fallback, falls die Abfahrtstafel keine Koordinaten liefert
static void fetchStopCoords() {
  if (C.lat != 0 || !C.key.length()) return;
  String url = "https://www.rmv.de/hapi/location.name?format=json&type=S&maxNo=1&accessId=" +
               urlEncode(C.key) + "&input=" + urlEncode(C.stopId);
  JsonDocument filter;
  JsonObject f = filter["stopLocationOrCoordLocation"].add<JsonObject>();
  f["StopLocation"]["lat"] = true;
  f["StopLocation"]["lon"] = true;
  JsonDocument doc;
  if (httpGetJson(url, doc, &filter) != 200) return;
  JsonObject s = doc["stopLocationOrCoordLocation"][0]["StopLocation"];
  float lat = s["lat"] | 0.0f, lon = s["lon"] | 0.0f;
  if (lat < 40 || lat > 60) return;
  C.lat = lat; C.lon = lon;
  prefs.begin("abfahrt", false); prefs.putFloat("lat", lat); prefs.putFloat("lon", lon); prefs.end();
  Serial.printf("[Ort] ueber location.name: %.4f %.4f\n", lat, lon);
}

// ---------- Wetter (Open-Meteo) + Warnungen (DWD ueber Bright Sky)
struct Weather {
  bool ok = false;
  time_t at = 0;
  float tNow = 0, tMax = 0, tMin = 0;
  int codeNow = 0;
  bool isDay = true;
  float hT[24];
  uint8_t hP[24];
  uint8_t hC[24];
} wx;

struct Alert {
  uint8_t level = 0;     // 0 keine, 2 markant (orange), 3 Unwetter (rot), 4 extrem (rot)
  String event;          // "Starkes Gewitter"
  time_t until = 0;
} alertNow;

uint32_t nextWxMs = 0;

static bool fetchWeather() {
  if (C.lat == 0) return false;
  char url[360];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,is_day"
           "&hourly=temperature_2m,precipitation_probability,weather_code"
           "&daily=temperature_2m_max,temperature_2m_min&timezone=Europe%%2FBerlin&forecast_days=1",
           C.lat, C.lon);
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["current"]["is_day"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["hourly"]["weather_code"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;
  JsonDocument doc;
  int code = httpGetJson(String(url), doc, &filter);
  if (code != 200) { Serial.printf("[Wetter] Fehler %d\n", code); return false; }
  JsonArray ht = doc["hourly"]["temperature_2m"], hp = doc["hourly"]["precipitation_probability"], hc = doc["hourly"]["weather_code"];
  if (ht.size() < 24) return false;
  for (int i = 0; i < 24; i++) {
    wx.hT[i] = ht[i] | 0.0f;
    wx.hP[i] = hp[i] | 0;
    wx.hC[i] = hc[i] | 0;
  }
  wx.tNow = doc["current"]["temperature_2m"] | 0.0f;
  wx.codeNow = doc["current"]["weather_code"] | 0;
  wx.isDay = (doc["current"]["is_day"] | 1) == 1;
  wx.tMax = doc["daily"]["temperature_2m_max"][0] | 0.0f;
  wx.tMin = doc["daily"]["temperature_2m_min"][0] | 0.0f;
  wx.ok = true;
  wx.at = time(nullptr);
  Serial.printf("[Wetter] %.1f Grad, Code %d\n", wx.tNow, wx.codeNow);
  return true;
}

static bool fetchAlerts() {
  if (C.lat == 0) return false;
  char url[160];
  snprintf(url, sizeof(url), "https://api.brightsky.dev/alerts?lat=%.4f&lon=%.4f&tz=Europe%%2FBerlin", C.lat, C.lon);
  JsonDocument filter;
  JsonObject f = filter["alerts"].add<JsonObject>();
  f["severity"] = true;
  f["event_de"] = true;
  f["onset"] = true;
  f["expires"] = true;
  JsonDocument doc;
  int code = httpGetJson(String(url), doc, &filter);
  if (code != 200) { Serial.printf("[Warnung] Fehler %d\n", code); return false; }
  Alert best;
  time_t now = time(nullptr);
  for (JsonObject a : doc["alerts"].as<JsonArray>()) {
    String sev = a["severity"] | "";
    uint8_t lv = sev == "extreme" ? 4 : sev == "severe" ? 3 : sev == "moderate" ? 2 : 0;
    if (lv < 2) continue;                          // gelbe Wetterwarnungen (z. B. Frost) nicht zeigen
    time_t on = parseIso(a["onset"] | ""), ex = parseIso(a["expires"] | "");
    if (ex && ex < now) continue;
    if (on && on > now + 12 * 3600) continue;
    if (lv > best.level || (lv == best.level && ex > best.until)) {
      best.level = lv;
      best.event = titleCase(toAscii(String(a["event_de"] | "Warnung")));
      best.until = ex;
    }
  }
  alertNow = best;
  if (best.level) Serial.printf("[Warnung] Stufe %d: %s\n", best.level, best.event.c_str());
  return true;
}

// Wettercode (WMO) -> Symbol + Text
enum WxIcon { WI_SUN, WI_PART, WI_CLOUD, WI_RAIN, WI_SNOW, WI_FOG, WI_THUNDER, WI_MOON, WI_PART_NIGHT };
static int wxIcon(int code, bool night) {
  if (code == 0) return night ? WI_MOON : WI_SUN;
  if (code <= 2) return night ? WI_PART_NIGHT : WI_PART;
  if (code == 3) return WI_CLOUD;
  if (code == 45 || code == 48) return WI_FOG;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WI_SNOW;
  if (code >= 95) return WI_THUNDER;
  return WI_RAIN;
}
static const char* wxText(int code, bool day) {
  switch (code) {
    case 0: return day ? "sonnig" : "klar";
    case 1: return day ? "meist sonnig" : "meist klar";
    case 2: return "leicht bewoelkt";
    case 3: return "bedeckt";
    case 45: case 48: return "Nebel";
    case 51: case 53: case 55: return "Nieselregen";
    case 56: case 57: return "gefr. Nieselregen";
    case 61: return "leichter Regen";
    case 63: return "Regen";
    case 65: return "starker Regen";
    case 66: case 67: return "gefrierender Regen";
    case 71: return "leichter Schnee";
    case 73: return "Schnee";
    case 75: return "starker Schnee";
    case 77: return "Schneegriesel";
    case 80: case 81: return "Regenschauer";
    case 82: return "heftige Schauer";
    case 85: case 86: return "Schneeschauer";
    case 95: return "Gewitter";
    case 96: case 99: return "Gewitter mit Hagel";
    default: return "";
  }
}
static bool isWet(int code) {
  return (code >= 51 && code <= 67) || (code >= 80 && code <= 82) || code >= 95;
}

// ---------- Feiertage + Schulferien (OpenHolidays) --------------
struct Holiday { uint32_t start = 0, end = 0; String name; };
Holiday nextHol, nextFerien;
long holFetchedDay = -1;
uint32_t holRetryMs = 0;

static bool fetchHolidayList(const char* kind, Holiday& out, long today) {
  uint32_t from = dayToYmd(today), to = dayToYmd(today + 400);
  char url[260];
  snprintf(url, sizeof(url),
           "https://openholidaysapi.org/%s?countryIsoCode=DE&subdivisionCode=DE-HE&languageIsoCode=DE"
           "&validFrom=%04u-%02u-%02u&validTo=%04u-%02u-%02u",
           kind, (unsigned)(from / 10000), (unsigned)((from / 100) % 100), (unsigned)(from % 100),
           (unsigned)(to / 10000), (unsigned)((to / 100) % 100), (unsigned)(to % 100));
  JsonDocument filter;
  JsonObject f0 = filter.add<JsonObject>();
  f0["startDate"] = true;
  f0["endDate"] = true;
  f0["name"].add<JsonObject>()["text"] = true;
  JsonDocument doc;
  int code = httpGetJson(String(url), doc, &filter);
  if (code != 200) { Serial.printf("[%s] Fehler %d\n", kind, code); return false; }
  Holiday best;
  for (JsonObject h : doc.as<JsonArray>()) {
    uint32_t s = parseYmd(h["startDate"] | ""), e = parseYmd(h["endDate"] | "");
    if (!s || !e || ymdToDay(e) < today) continue;
    if (!best.start || s < best.start) {
      best.start = s; best.end = e;
      best.name = toAscii(String(h["name"][0]["text"] | ""));
    }
  }
  best.name.replace("Deutschen", "Dt.");
  out = best;
  Serial.printf("[%s] %s %u-%u\n", kind, best.name.c_str(), (unsigned)best.start, (unsigned)best.end);
  return true;
}

static void updateHolidays() {
  if (!timeValid()) return;
  long today = todayNum();
  if (holFetchedDay == today) return;
  if (holRetryMs && (int32_t)(millis() - holRetryMs) < 0) return;
  bool a = fetchHolidayList("PublicHolidays", nextHol, today);
  bool b = fetchHolidayList("SchoolHolidays", nextFerien, today);
  if (a && b) { holFetchedDay = today; holRetryMs = 0; }
  else holRetryMs = millis() + 3600UL * 1000UL;
}

// ---------- Muellkalender (EAD ueber Muellmax) -----------------
struct Pickup { uint32_t ymd; uint8_t type; uint8_t rh; };   // type 0 Rest, 1 Bio, 2 Papier, 3 Gelb; rh = Rhythmus Rest (0 = unbekannt)
static const int MAX_PICKUPS = 160;
Pickup pickups[MAX_PICKUPS];
int pickupCount = 0;
time_t muellAt = 0, muellTry = 0;
String muellErr;
static const char* BIN_NAMES[] = {"Restmuell", "Bio", "Papier", "Gelbe Tonne"};

static void loadMuell() {
  prefs.begin("muell", true);
  size_t len = prefs.getBytesLength("d");
  pickupCount = 0;
  if (len && len % sizeof(Pickup) == 0 && len <= sizeof(pickups)) {
    prefs.getBytes("d", pickups, len);
    pickupCount = len / sizeof(Pickup);
  }
  muellAt = (time_t)prefs.getULong64("at", 0);
  muellTry = (time_t)prefs.getULong64("try", 0);
  muellErr = prefs.getString("err", "");
  prefs.end();
}

static void clearMuell() {
  prefs.begin("muell", false); prefs.clear(); prefs.end();
  pickupCount = 0; muellAt = 0; muellTry = 0; muellErr = "";
}

static String mmCookie;
static bool mmRequest(const String* body, String& out, String& err) {
  static const char* MM_URL = "https://www.muellmax.de/abfallkalender/ead/res/EadStart.php";
  WiFiClientSecure cl; cl.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(cl, MM_URL)) { err = "Muellmax nicht erreichbar"; return false; }
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36");
  const char* keys[] = {"Set-Cookie"};
  http.collectHeaders(keys, 1);
  if (mmCookie.length()) http.addHeader("Cookie", mmCookie);
  int code;
  if (body) {
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    code = http.POST(*body);
  } else {
    code = http.GET();
  }
  if (http.hasHeader("Set-Cookie")) {
    String c = http.header("Set-Cookie");
    int s = c.indexOf(';');
    mmCookie = s > 0 ? c.substring(0, s) : c;
  }
  if (code != 200) { http.end(); err = String("Muellmax: Fehler ") + code; return false; }
  out = http.getString();
  http.end();
  if (out.indexOf("Abfragelimit") >= 0) { err = "Muellmax: Abfragelimit, morgen erneut"; return false; }
  return true;
}

// Attribut aus dem Tag holen, der 'marker' enthaelt (z. B. name="mm_ses")
static String tagAttr(const String& html, const String& marker, const char* attr, int from = 0, int* tagEnd = nullptr) {
  int i = html.indexOf(marker, from);
  if (i < 0) return String();
  int s = html.lastIndexOf('<', i), e = html.indexOf('>', i);
  if (s < 0 || e < 0) return String();
  if (tagEnd) *tagEnd = e;
  String tag = html.substring(s, e);
  String key = String(attr) + "=\"";
  int a = tag.indexOf(key);
  if (a < 0) return String();
  a += key.length();
  int b = tag.indexOf('"', a);
  return b > a ? tag.substring(a, b) : String();
}

static String sesOf(const String& html) { return tagAttr(html, "name=\"mm_ses\"", "value"); }

static bool mmFetch(String& err) {
  String html, ses, body;
  mmCookie = "";
  if (!mmRequest(nullptr, html, err)) return false;
  ses = sesOf(html);
  if (!ses.length()) { err = "Muellmax: Seite unbekannt"; return false; }

  body = "mm_ses=" + urlEncode(ses) + "&mm_aus_ort.x=0&mm_aus_ort.y=0";
  if (!mmRequest(&body, html, err)) return false;
  ses = sesOf(html);

  String street = C.streetVal.length() ? C.streetVal : C.street;
  body = "mm_ses=" + urlEncode(ses) + "&xxx=1&mm_frm_str_name=" + urlEncode(street) + "&mm_aus_str_txt_submit=suchen";
  if (!mmRequest(&body, html, err)) return false;
  ses = sesOf(html);

  if (html.indexOf("name=\"mm_frm_str_sel\"") >= 0) {
    body = "mm_ses=" + urlEncode(ses) + "&xxx=1&mm_frm_str_sel=" + urlEncode(street) + "&mm_aus_str_sel_submit=weiter";
    if (!mmRequest(&body, html, err)) return false;
    ses = sesOf(html);
  }

  int hs = html.indexOf("name=\"mm_frm_hnr_sel\"");
  if (hs >= 0) {
    // Hausnummer waehlen: Werte wie "64291;Arheilgen;12;" oder Bereiche "1 - 29"
    int he = html.indexOf("</select>", hs);
    int want = C.hnr.toInt();
    String first, pick;
    int pos = hs;
    while (true) {
      int o = html.indexOf("<option", pos);
      if (o < 0 || (he > 0 && o > he)) break;
      int te = o;
      String val = tagAttr(html, "<option", "value", o, &te);
      int ce = html.indexOf("</option>", te);
      String text = html.substring(te + 1, ce > te ? ce : te + 1);
      text.trim();
      pos = te + 1;
      if (!val.length()) continue;
      if (!first.length()) first = val;
      if (want <= 0) continue;
      int p1 = val.indexOf(';'), p2 = p1 >= 0 ? val.indexOf(';', p1 + 1) : -1;
      String num = val;
      if (p2 >= 0) {
        int p3 = val.indexOf(';', p2 + 1);
        num = p3 > p2 ? val.substring(p2 + 1, p3) : val.substring(p2 + 1);
      }
      if (num.toInt() == want || text.toInt() == want) { pick = val; break; }
      int dash = text.indexOf('-');
      if (dash > 0) {
        int a = text.substring(0, dash).toInt(), b = text.substring(dash + 1).toInt();
        if (a > 0 && b >= a && want >= a && want <= b) { pick = val; break; }
      }
    }
    if (!pick.length()) pick = first;
    body = "mm_ses=" + urlEncode(ses) + "&xxx=1&mm_frm_hnr_sel=" + urlEncode(pick) + "&mm_aus_hnr_sel_submit=weiter";
    if (!mmRequest(&body, html, err)) return false;
    ses = sesOf(html);
  }

  body = "mm_ses=" + urlEncode(ses) + "&xxx=1&mm_ica_auswahl=" + urlEncode("iCalendar-Datei");
  if (!mmRequest(&body, html, err)) return false;
  ses = sesOf(html);

  // alle angebotenen Abfallarten ankreuzen, gefiltert wird spaeter auf dem Geraet
  String fra;
  int pos = 0;
  while (true) {
    int i = html.indexOf("name=\"mm_frm_fra", pos);
    if (i < 0) break;
    int te = i;
    String name = tagAttr(html, "name=\"mm_frm_fra", "name", i, &te);
    String val = tagAttr(html, "name=\"mm_frm_fra", "value", i);
    pos = te > i ? te : i + 10;
    if (name.length()) fra += "&" + urlEncode(name) + "=" + urlEncode(val);
  }
  if (!fra.length()) { err = "Strasse nicht gefunden"; return false; }

  body = "mm_ses=" + urlEncode(ses) + "&xxx=1&mm_frm_type=termine" + fra + "&mm_ica_gen=" + urlEncode("iCalendar-Datei laden");
  if (!mmRequest(&body, html, err)) return false;
  if (html.indexOf("BEGIN:VCALENDAR") < 0) { err = "Muellmax: kein Kalender"; return false; }

  // iCal lesen: DTSTART + SUMMARY je Termin
  Pickup tmp[MAX_PICKUPS]; int n = 0;
  long today = todayNum();
  uint32_t ymd = 0; int type = -1; uint8_t rh = 0;
  int ls = 0;
  while (ls < (int)html.length()) {
    int le = html.indexOf('\n', ls);
    if (le < 0) le = html.length();
    String l = html.substring(ls, le); l.trim();
    ls = le + 1;
    if (l.startsWith("BEGIN:VEVENT")) { ymd = 0; type = -1; rh = 0; }
    else if (l.startsWith("DTSTART")) {
      int c = l.indexOf(':');
      if (c > 0) { String v = l.substring(c + 1); ymd = parseYmd(v.c_str()); }
    }
    else if (l.startsWith("SUMMARY")) {
      String s = l.substring(l.indexOf(':') + 1); s.toLowerCase();
      if (s.indexOf("rest") >= 0) {
        type = 0;
        rh = s.indexOf("4-w") >= 0 ? 4 : s.indexOf("2-w") >= 0 ? 2 : s.indexOf("chentl") >= 0 ? 1 : 0;
      }
      else if (s.indexOf("bio") >= 0) type = 1;
      else if (s.indexOf("papier") >= 0 || s.indexOf("pappe") >= 0 || s.indexOf("ppk") >= 0) type = 2;
      else if (s.indexOf("gelb") >= 0 || s.indexOf("wertstoff") >= 0 || s.indexOf("verpackung") >= 0 || s.indexOf("lvp") >= 0) type = 3;
    }
    else if (l.startsWith("END:VEVENT")) {
      if (ymd && type >= 0 && ymdToDay(ymd) >= today - 1 && n < MAX_PICKUPS) { tmp[n].ymd = ymd; tmp[n].type = (uint8_t)type; tmp[n].rh = rh; n++; }
    }
  }
  if (!n) { err = "keine Termine gefunden"; return false; }
  for (int i = 1; i < n; i++) {
    Pickup k = tmp[i]; int j = i - 1;
    while (j >= 0 && tmp[j].ymd > k.ymd) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  memcpy(pickups, tmp, sizeof(Pickup) * n);
  pickupCount = n;
  return true;
}

// laedt den Kalender und speichert ihn; Rueckgabe: Erfolg
static bool updateMuell() {
  if (!C.street.length()) { muellErr = "keine Strasse eingerichtet"; return false; }
  if (WiFi.status() != WL_CONNECTED) { muellErr = "kein WLAN"; return false; }
  String err;
  bool ok = mmFetch(err);
  time_t now = time(nullptr);
  muellTry = now;
  prefs.begin("muell", false);
  prefs.putULong64("try", (uint64_t)now);
  if (ok) {
    muellAt = now; muellErr = "";
    prefs.putBytes("d", pickups, sizeof(Pickup) * pickupCount);
    prefs.putULong64("at", (uint64_t)now);
    prefs.putString("err", "");
  } else {
    muellErr = err;
    prefs.putString("err", err);
  }
  prefs.end();
  Serial.printf("[Muell] %s (%d Termine)\n", ok ? "geladen" : err.c_str(), pickupCount);
  return ok;
}

static bool muellDue() {
  if (!C.street.length() || !timeValid()) return false;
  time_t now = time(nullptr);
  if (muellTry && now - muellTry < 24 * 3600) return false;     // hoechstens einmal pro Tag versuchen
  if (!pickupCount || !muellAt) return true;
  if (now - muellAt > (time_t)MUELL_REFRESH_DAYS * 24 * 3600) return true;
  long last = ymdToDay(pickups[pickupCount - 1].ymd);
  return last - todayNum() < MUELL_MIN_AHEAD_DAYS;
}

// die naechsten Abholungen (nur ausgewaehlte Tonnen) ab heute
static int nextPickups(Pickup* out, int maxN) {
  long today = todayNum(); int n = 0;
  for (int i = 0; i < pickupCount && n < maxN; i++) {
    if (!(C.bins & (1 << pickups[i].type))) continue;
    if (pickups[i].type == 0 && pickups[i].rh && pickups[i].rh != C.rest) continue;
    if (n && out[n - 1].ymd == pickups[i].ymd && out[n - 1].type == pickups[i].type) continue;
    if (ymdToDay(pickups[i].ymd) < today) continue;
    out[n++] = pickups[i];
  }
  return n;
}

// Tonne auf Seite 1: Vorabend ab 17:00 bis Abholtag 09:00
static int reminderBin() {
  if (!timeValid()) return -1;
  struct tm lt = nowTm();
  long today = todayNum();
  Pickup nx[1];
  if (!nextPickups(nx, 1)) return -1;
  long d = ymdToDay(nx[0].ymd) - today;
  if (d == 1 && lt.tm_hour >= MUELL_REMIND_FROM_H) return nx[0].type;
  if (d == 0 && lt.tm_hour < MUELL_REMIND_TO_H) return nx[0].type;
  return -1;
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

// Text mit Gradzeichen: '`' im Text wird als kleiner Kreis gezeichnet.
// datum: TL_DATUM oder TR_DATUM (Oberkante bei y)
static void drawDegText(TFT_eSPI& g, const String& s, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg) {
  int fh = g.fontHeight();
  int r = max(2, fh / 9);
  int degW = r * 2 + 3;
  int total = 0;
  int start = 0;
  while (start <= (int)s.length()) {
    int i = s.indexOf('`', start);
    String part = s.substring(start, i < 0 ? s.length() : i);
    total += g.textWidth(part);
    if (i < 0) break;
    total += degW; start = i + 1;
  }
  int cx = datum == TR_DATUM ? x - total : x;
  g.setTextDatum(TL_DATUM);
  g.setTextColor(fg, bg);
  start = 0;
  while (start <= (int)s.length()) {
    int i = s.indexOf('`', start);
    String part = s.substring(start, i < 0 ? s.length() : i);
    if (part.length()) cx += g.drawString(part, cx, y);
    if (i < 0) break;
    g.drawCircle(cx + r + 1, y + r + 2, r, fg);
    if (r > 2) g.drawCircle(cx + r + 1, y + r + 2, r - 1, fg);
    cx += degW; start = i + 1;
  }
}

// Warndreieck mit Ausrufezeichen
static void drawWarnTri(TFT_eSPI& g, int x, int y, int s, uint16_t col, uint16_t mark) {
  g.fillTriangle(x, y - s, x + s + s / 6, y + s * 4 / 5, x - s - s / 6, y + s * 4 / 5, col);
  g.fillRect(x - 1, y - s / 2 + 1, 2, s * 3 / 5, mark);
  g.fillRect(x - 1, y + s * 3 / 10 + 1, 2, 2, mark);
}

// Muelltonne (Farbe je Art)
static void drawBin(TFT_eSPI& g, int x, int y, int type) {
  uint16_t col = P.bin[type & 3];
  g.fillRoundRect(x - 9, y - 10, 19, 4, 2, col);
  g.fillTriangle(x - 8, y - 5, x + 9, y - 5, x + 7, y + 11, col);
  g.fillTriangle(x - 8, y - 5, x + 7, y + 11, x - 6, y + 11, col);
  g.fillCircle(x - 4, y + 12, 2, COL_FG);
  g.fillCircle(x + 5, y + 12, 2, COL_FG);
}

// Wettersymbole aus einfachen Formen
static void drawCloud(TFT_eSPI& g, int x, int y, int s, uint16_t col) {
  g.fillCircle(x - s * 35 / 100, y + s * 8 / 100, s * 30 / 100, col);
  g.fillCircle(x, y - s * 12 / 100, s * 40 / 100, col);
  g.fillCircle(x + s * 40 / 100, y + s * 10 / 100, s * 28 / 100, col);
  g.fillRoundRect(x - s * 65 / 100, y + s * 8 / 100, s * 133 / 100, s * 30 / 100, s * 15 / 100, col);
}
static void drawSun(TFT_eSPI& g, int x, int y, int s, uint16_t bg) {
  g.fillCircle(x, y, s * 42 / 100, COL_ORANGE);
  float w = s * 0.12f; if (w < 1.5f) w = 1.5f;
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4, r1 = s * 0.58f, r2 = s * 0.8f;
    g.drawWideLine(x + cosf(a) * r1, y + sinf(a) * r1, x + cosf(a) * r2, y + sinf(a) * r2, w, COL_ORANGE, bg);
  }
}
static void drawMoon(TFT_eSPI& g, int x, int y, int s, uint16_t col, uint16_t bg) {
  g.fillCircle(x, y, s * 42 / 100, col);
  g.fillCircle(x + s * 20 / 100, y - s * 12 / 100, s * 36 / 100, bg);
}
static void drawDrops(TFT_eSPI& g, int x, int y, int s, uint16_t col, uint16_t bg) {
  float w = s * 0.11f; if (w < 1.5f) w = 1.5f;
  for (int i = -1; i <= 1; i++)
    g.drawWideLine(x + i * s * 0.35f, y, x + i * s * 0.35f - s * 0.12f, y + s * 0.32f, w, col, bg);
}
static void drawWxIcon(TFT_eSPI& g, int kind, int x, int y, int s, bool onPanel) {
  uint16_t cl = onPanel ? COL_PANEL_FG : COL_FG;
  uint16_t bg = onPanel ? COL_PANEL : COL_BG;
  uint16_t rn = onPanel ? COL_PRAIN : COL_RAIN;
  switch (kind) {
    case WI_SUN: drawSun(g, x, y, s, bg); break;
    case WI_MOON: drawMoon(g, x, y, s, cl, bg); break;
    case WI_PART:
      drawSun(g, x + s * 28 / 100, y - s * 25 / 100, s * 70 / 100, bg);
      drawCloud(g, x - s * 5 / 100, y + s * 10 / 100, s * 80 / 100, cl);
      break;
    case WI_PART_NIGHT:
      drawMoon(g, x + s * 28 / 100, y - s * 25 / 100, s * 70 / 100, cl, bg);
      drawCloud(g, x - s * 5 / 100, y + s * 10 / 100, s * 80 / 100, cl);
      break;
    case WI_CLOUD: drawCloud(g, x, y, s, cl); break;
    case WI_RAIN:
      drawCloud(g, x, y - s * 15 / 100, s * 85 / 100, cl);
      drawDrops(g, x, y + s * 30 / 100, s, rn, bg);
      break;
    case WI_SNOW:
      drawCloud(g, x, y - s * 15 / 100, s * 85 / 100, cl);
      for (int i = -1; i <= 1; i++) g.fillCircle(x + i * s * 35 / 100, y + s * 42 / 100, max(1, s / 14), rn);
      break;
    case WI_FOG:
      for (int i = 0; i < 3; i++)
        g.fillRoundRect(x - s * 55 / 100 + (i % 2) * s / 10, y - s * 25 / 100 + i * s * 25 / 100, s, max(2, s / 10), 2, cl);
      break;
    case WI_THUNDER:
      drawCloud(g, x, y - s * 15 / 100, s * 85 / 100, cl);
      g.fillTriangle(x + s / 10, y + s / 10, x - s / 5, y + s * 45 / 100, x + s / 20, y + s * 40 / 100, COL_ORANGE);
      g.fillTriangle(x + s / 20, y + s * 30 / 100, x - s / 10, y + s * 70 / 100, x + s / 5, y + s * 30 / 100, COL_ORANGE);
      break;
  }
}

// Seitenpunkte unten (nur Seite 2 und 3; Seite 1 bleibt unveraendert)
static void drawDots() {
  String sig = String(page) + nightMode;
  if (sig == shownDots) return;
  shownDots = sig;
  tft.fillRect(130, 228, 60, 12, COL_BG);
  for (int i = 0; i < 3; i++) {
    int x = 148 + i * 12;
    if (i == page) tft.fillCircle(x, 234, 3, COL_FG);
    else { tft.drawCircle(x, 234, 2, COL_SUB); tft.drawCircle(x, 234, 3, COL_SUB); }
  }
}

// Kopfzeile: Titel, Unterzeile, Uhr, optional Muelltonne neben der Uhr
static void drawHeader(const String& title, const String& sub, int bin = -1) {
  char clock[6] = "--:--";
  if (timeValid()) fmtHM(time(nullptr), clock);
  String sig = title + "|" + sub + "|" + clock + "|" + bin + "|" + nightMode;
  if (shownHeader == sig) return;
  shownHeader = sig;
  drawZone(0, 0, 320, 46, [&](TFT_eSPI& g, int ox, int oy) {
    g.fillRect(ox, oy, 320, 46, COL_BG);
    g.setTextColor(COL_FG, COL_BG);
    g.setFreeFont(&FreeSansBold9pt7b);
    g.setTextDatum(TL_DATUM);
    g.drawString(fitText(g, title, 180), ox + 14, oy + 9);
    g.setTextFont(2);
    g.setTextColor(COL_SUB, COL_BG);
    g.drawString(fitText(g, sub, 190), ox + 14, oy + 24);
    g.setTextColor(COL_FG, COL_BG);
    g.setFreeFont(&FreeSansBold18pt7b);
    g.setTextDatum(MR_DATUM);
    g.drawString(clock, ox + 306, oy + 22);
    if (bin >= 0) {
      int cw = g.textWidth(clock);
      drawBin(g, ox + 306 - cw - 22, oy + 20, bin);
    }
    g.fillRect(ox + 14, oy + 42, 292, 2, COL_FG);
  });
}

// ---------- Seite 1: Abfahrt ----------------------------------
static String msgLabel() {
  String w = msgHead;
  int sp = w.indexOf(' ');
  if (sp > 0) w = w.substring(0, sp);
  while (w.length() && !isalnum((unsigned char)w[w.length() - 1])) w.remove(w.length() - 1);
  if (!w.length()) w = "Meldung";
  String l = msgLines;
  int c = l.indexOf(',');
  if (c > 0) l = l.substring(0, c);
  return l.length() ? w + " L" + l : w;
}

static void drawHero(const View& v) {
  time_t now = time(nullptr);
  bool stale = lastOk == 0 || (now - lastOk) > 180;
  int mins = v.hero < 0 ? -1 : (v.heroSecs <= 0 ? 0 : (int)(v.heroSecs / 60));

  String sig = String(mins) + "|" + v.hero + "|" + v.skipped + "|" + stale + "|" + depCount + "|" + lastErr + "|" + msgHead + "|" + nightMode;
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

    // rechts: Kapsel mit Linie + Abfahrtszeit
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

    // Statuszeile: Ausfall / Fehler / offline / Stoerung / keine Bahn
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
    } else if (msgHead.length()) {
      String s = fitText(g, msgLabel(), 120);
      int w = g.textWidth(s);
      g.setTextColor(P.otext, COL_BG);
      g.drawString(s, ox + 296, oy + 72);
      drawWarnTri(g, ox + 296 - w - 11, oy + 79, 6, COL_ORANGE, COL_BG);
    } else if (v.hero < 0) {
      g.setTextColor(COL_SUB, COL_BG);
      g.drawString("keine Abfahrt", ox + 296, oy + 72);
    }
  });
}

static void drawPanel(const View& v) {
  time_t now = time(nullptr);
  long walk = (long)C.walk * 60;
  String sig = String(nightMode) + "#";
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

static void resetShown() { shownHeader = ""; shownHero = ""; shownPanel = ""; shownDots = ""; }

static void drawMain(bool force = false) {
  if (force) { resetShown(); tft.fillScreen(COL_BG); }
  View v = computeView();
  String sub = C.dirId.length() ? "Richtung " + C.dirName : String("alle Richtungen");
  drawHeader(C.stopName, sub, reminderBin());
  drawHero(v);
  drawPanel(v);
  int mins = v.hero < 0 ? -1 : (int)(v.heroSecs / 60);
  setLed(mins >= 0 && mins <= RED_AT_MIN);
}

// ---------- Meldungstext (nach Tippen auf den Hinweis) ------------
static void drawMessage() {
  tft.fillScreen(COL_BG);
  resetShown();
  String title = msgLines.length() ? "Meldung Linie " + msgLines : String("Meldung");
  String sub = msgUntil.length() ? "RMV, gilt bis " + msgUntil : String("RMV");
  drawHeader(title, sub);
  shownHeader = "";   // beim Zurueckgehen neu zeichnen
  drawWarnTri(tft, 24, 66, 7, COL_ORANGE, COL_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(COL_FG, COL_BG);
  tft.drawString(fitText(tft, msgHead, 262), 38, 67);

  tft.fillRoundRect(10, 82, 300, 124, 16, COL_PANEL);
  tft.setTextFont(2);
  tft.setTextColor(COL_PANEL_FG, COL_PANEL);
  tft.setTextDatum(TL_DATUM);
  // Zeilenumbruch nach Woertern
  String text = msgText.length() ? msgText : msgHead;
  int y = 92, maxW = 276;
  String line;
  int i = 0;
  while (i <= (int)text.length() && y < 196) {
    int sp = text.indexOf(' ', i);
    if (sp < 0) sp = text.length();
    String word = text.substring(i, sp);
    String test = line.length() ? line + " " + word : word;
    if (tft.textWidth(test) > maxW && line.length()) {
      tft.drawString(line, 22, y); y += 18; line = word;
    } else line = test;
    i = sp + 1;
  }
  if (line.length() && y < 196) tft.drawString(fitText(tft, line, maxW), 22, y);
  tft.setTextColor(COL_SUB, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Tippen = zurueck", 160, 222);
}

// ---------- Seite 2: Wetter -------------------------------------
static String fmtTemp(float t) { return String((int)lroundf(t)); }

static String rainHint(int h0) {
  if (!wx.ok) return "";
  if (isWet(wx.codeNow)) return "Regen jetzt";
  for (int h = h0; h < 24; h++)
    if (wx.hP[h] >= 50 || isWet(wx.hC[h])) return String("Regen ab ") + h + " Uhr";
  return "";
}

static void drawWeather(bool force) {
  if (force) { resetShown(); tft.fillScreen(COL_BG); }
  drawHeader(C.place.length() ? C.place : String("Wetter"), "Wetter heute");
  struct tm lt = nowTm();
  String hint = rainHint(lt.tm_hour);
  String sig = String((long)wx.at) + "|" + nightMode + "|" + alertNow.level + alertNow.event + (long)alertNow.until + "|" + hint + "|" + (C.lat != 0);
  if (sig != shownHero) {
    shownHero = sig;
    drawZone(10, 46, 300, 102, [&](TFT_eSPI& g, int ox, int oy) {
      g.fillRect(ox, oy, 300, 102, COL_BG);
      if (!wx.ok) {
        g.setTextFont(2); g.setTextDatum(MC_DATUM); g.setTextColor(COL_SUB, COL_BG);
        g.drawString(C.lat == 0 ? "Ort der Haltestelle noch unbekannt" : "Wetterdaten werden geladen ...", ox + 150, oy + 50);
        return;
      }
      bool warn = alertNow.level >= 2;
      if (warn) {
        bool red = alertNow.level >= 3;
        uint16_t bc = red ? COL_RED : COL_ORANGE, tc = red ? P.warnfg : COL_DARK;
        g.fillRoundRect(ox, oy + 3, 300, 22, 8, bc);
        drawWarnTri(g, ox + 14, oy + 14, 6, tc, bc);
        g.setFreeFont(&FreeSansBold9pt7b);
        g.setTextDatum(ML_DATUM);
        g.setTextColor(tc, bc);
        const char* lab = red ? "UNWETTER" : "WARNUNG";
        int lw = g.drawString(lab, ox + 26, oy + 14);
        g.setTextFont(2);
        String until;
        if (alertNow.until) { char hm[6]; fmtHM(alertNow.until, hm); until = String("bis ") + hm; }
        int uw = until.length() ? g.textWidth(until) : 0;
        g.drawString(fitText(g, alertNow.event, 300 - 26 - lw - 8 - uw - 14), ox + 26 + lw + 8, oy + 15);
        if (until.length()) { g.setTextDatum(MR_DATUM); g.drawString(until, ox + 292, oy + 15); }
      } else {
        g.fillCircle(ox + 8, oy + 12, 4, COL_RAIN);
        g.drawCircle(ox + 8, oy + 12, 5, COL_FG);
        g.setTextFont(2); g.setTextDatum(ML_DATUM); g.setTextColor(COL_FG, COL_BG);
        g.drawString("JETZT", ox + 18, oy + 12);
        if (hint.length()) {
          g.setFreeFont(&FreeSansBold9pt7b);
          g.setTextColor(COL_RAIN, COL_BG);
          g.drawString(hint, ox + 86, oy + 12);
        }
      }
      // grosse Temperatur
      int ty = warn ? 28 : 22;
      String t = fmtTemp(wx.tNow);
      g.setTextColor(COL_FG, COL_BG);
      g.setTextDatum(TL_DATUM);
      int tw = g.drawString(t, ox + 2, oy + ty, 8);
      g.drawCircle(ox + 2 + tw + 9, oy + ty + 10, 6, COL_FG);
      g.drawCircle(ox + 2 + tw + 9, oy + ty + 10, 5, COL_FG);
      // Symbol + Text rechts
      bool night = !wx.isDay;
      int kind = wxIcon(wx.codeNow, night);
      if (warn) drawWxIcon(g, kind, ox + 252, oy + 50, 32, false);
      else drawWxIcon(g, kind, ox + 252, oy + 40, 46, false);
      g.setFreeFont(&FreeSansBold9pt7b);
      g.setTextDatum(TR_DATUM);
      g.setTextColor(COL_FG, COL_BG);
      g.drawString(fitText(g, wxText(wx.codeNow, !night), 190), ox + 296, oy + 68);
      g.setTextFont(2);
      drawDegText(g, "max " + fmtTemp(wx.tMax) + "`  min " + fmtTemp(wx.tMin) + "`", ox + 296, oy + 86, TR_DATUM, COL_SUB, COL_BG);
    });
  }

  String psig = String((long)wx.at) + "|" + nightMode;
  if (psig != shownPanel) {
    shownPanel = psig;
    drawZone(10, 148, 300, 80, [&](TFT_eSPI& g, int ox, int oy) {
      g.fillRect(ox, oy, 300, 80, COL_BG);
      g.fillRoundRect(ox, oy + 2, 300, 76, 16, COL_PANEL);
      if (!wx.ok) return;
      static const int HRS[6] = {7, 10, 13, 16, 19, 22};
      for (int i = 0; i < 6; i++) {
        int h = HRS[i], cx = ox + 25 + i * 50;
        char lab[8]; sprintf(lab, "%02d h", h);
        g.setTextFont(2); g.setTextDatum(MC_DATUM);
        g.setTextColor(COL_PANEL_SUB, COL_PANEL);
        g.drawString(lab, cx, oy + 15);
        bool night = h >= 21 || h < 6;
        drawWxIcon(g, wxIcon(wx.hC[h], night), cx, oy + 34, 17, true);
        g.setFreeFont(&FreeSansBold9pt7b);
        String t = fmtTemp(wx.hT[h]);
        int w = g.textWidth(t) + 6;
        drawDegText(g, t + "`", cx - w / 2, oy + 47, TL_DATUM, COL_PANEL_FG, COL_PANEL);
        g.setTextFont(2); g.setTextDatum(MC_DATUM);
        bool wet = wx.hP[h] >= 50;
        g.setTextColor(wet ? COL_PRAIN : COL_PANEL_SUB, COL_PANEL);
        g.drawString(String(wx.hP[h]) + "%", cx, oy + 70);
      }
    });
  }
  drawDots();
}

// ---------- Seite 3: Tagesblatt ---------------------------------
static void drawDay(bool force) {
  if (force) { resetShown(); tft.fillScreen(COL_BG); }
  long today = todayNum();
  uint32_t ymd = dayToYmd(today);
  struct tm lt = nowTm();
  String title = timeValid() ? String(WD_LONG[weekdayOf(today)]) : String("Tagesblatt");
  String sub;
  if (timeValid())
    sub = String((int)(ymd % 100)) + ". " + MONTHS[((ymd / 100) % 100) - 1] + " " + (int)(ymd / 10000) + ", KW " + isoWeek(today);
  drawHeader(title, sub);

  Pickup nx[2];
  int n = nextPickups(nx, 2);
  int hr = lt.tm_hour;
  String sig = String(today) + "|" + hr + "|" + n + "|" + nightMode + "|" + muellErr + "|" + (int)C.street.length();
  for (int i = 0; i < n; i++) sig += String("|") + nx[i].ymd + "/" + nx[i].type;
  if (sig != shownHero) {
    shownHero = sig;
    drawZone(10, 46, 300, 100, [&](TFT_eSPI& g, int ox, int oy) {
      g.fillRect(ox, oy, 300, 100, COL_BG);
      g.setTextFont(2); g.setTextDatum(TL_DATUM); g.setTextColor(COL_FG, COL_BG);
      g.drawString("MUELLABFUHR", ox + 4, oy + 6);
      g.setTextDatum(TR_DATUM); g.setTextColor(COL_SUB, COL_BG);
      g.drawString("naechste Termine", ox + 296, oy + 6);
      if (!C.street.length() || n == 0) {
        g.setTextDatum(MC_DATUM);
        String s = !C.street.length() ? String("Strasse im Einrichtungs-WLAN waehlen")
                 : muellErr.length() ? "Muell: " + muellErr : String("Termine werden geladen ...");
        g.drawString(fitText(g, s, 290), ox + 150, oy + 56);
        return;
      }
      for (int i = 0; i < n; i++) {
        int y = oy + 36 + i * 38;           // Mitte der Zeile
        drawBin(g, ox + 16, y - 2, nx[i].type);
        g.setFreeFont(&FreeSansBold9pt7b);
        g.setTextDatum(TL_DATUM); g.setTextColor(COL_FG, COL_BG);
        g.drawString(BIN_NAMES[nx[i].type], ox + 36, y - 13);
        long dn = ymdToDay(nx[i].ymd);
        g.setTextFont(2); g.setTextColor(COL_SUB, COL_BG);
        g.drawString(String(WD_SHORT[weekdayOf(dn)]) + " " + fmtDM(nx[i].ymd), ox + 36, y + 3);
        long d = dn - today;
        String pill; uint16_t pc = COL_FG;
        if (d == 1 && hr >= MUELL_REMIND_FROM_H) { pill = "heute rausstellen"; pc = COL_ORANGE; }
        else if (d == 0 && hr < MUELL_REMIND_TO_H) { pill = "heute Abholung"; pc = COL_ORANGE; }
        else if (d == 0) pill = "heute";
        else if (d == 1) pill = "morgen";
        if (pill.length()) {
          g.setFreeFont(&FreeSansBold9pt7b);
          int pw = g.textWidth(pill) + 24;
          g.fillRoundRect(ox + 296 - pw, y - 14, pw, 28, 12, pc);
          g.setTextDatum(MC_DATUM);
          g.setTextColor(pc == COL_FG ? COL_BG : COL_DARK, pc);
          g.drawString(pill, ox + 296 - pw / 2, y);
        } else {
          g.setTextFont(2); g.setTextDatum(MR_DATUM); g.setTextColor(COL_SUB, COL_BG);
          g.drawString(String("in ") + d + " Tagen", ox + 296, y);
        }
        if (i == 0 && n > 1) g.drawFastHLine(ox + 4, oy + 55, 292, COL_SUB);
      }
    });
  }

  String psig = String(today) + "|" + nightMode + "|" + nextHol.start + nextHol.name + "|" + nextFerien.start + "/" + nextFerien.end;
  if (psig != shownPanel) {
    shownPanel = psig;
    drawZone(10, 146, 300, 82, [&](TFT_eSPI& g, int ox, int oy) {
      g.fillRect(ox, oy, 300, 82, COL_BG);
      g.fillRoundRect(ox, oy + 2, 300, 78, 16, COL_PANEL);
      auto rel = [](long d) -> String {
        if (d == 0) return String("heute");
        if (d == 1) return String("morgen");
        return String("in ") + d + " Tagen";
      };
      // Feiertag
      g.setTextFont(2); g.setTextDatum(TL_DATUM); g.setTextColor(COL_PANEL_SUB, COL_PANEL);
      g.drawString("FEIERTAG", ox + 12, oy + 8);
      if (nextHol.start) {
        long d = ymdToDay(nextHol.start) - today;
        g.setTextDatum(TR_DATUM);
        g.drawString(rel(d), ox + 288, oy + 8);
        g.setFreeFont(&FreeSansBold9pt7b); g.setTextDatum(TL_DATUM); g.setTextColor(COL_PANEL_FG, COL_PANEL);
        String s = String(WD_SHORT[weekdayOf(ymdToDay(nextHol.start))]) + " " + fmtDM(nextHol.start) + "  " + nextHol.name;
        g.drawString(fitText(g, s, 276), ox + 12, oy + 23);
      } else {
        g.setFreeFont(&FreeSansBold9pt7b); g.setTextColor(COL_PANEL_FG, COL_PANEL);
        g.drawString("wird geladen ...", ox + 12, oy + 23);
      }
      g.drawFastHLine(ox + 12, oy + 42, 276, COL_BTN);
      // Schulferien
      g.setTextFont(2); g.setTextDatum(TL_DATUM); g.setTextColor(COL_PANEL_SUB, COL_PANEL);
      g.drawString("FERIEN HESSEN", ox + 12, oy + 47);
      if (nextFerien.start) {
        long ds = ymdToDay(nextFerien.start) - today, de = ymdToDay(nextFerien.end) - today;
        String r = ds <= 0 ? (de == 0 ? String("letzter Tag") : String("noch ") + (de + 1) + " Tage") : rel(ds);
        g.setTextDatum(TR_DATUM);
        g.drawString(r, ox + 288, oy + 47);
        g.setFreeFont(&FreeSansBold9pt7b); g.setTextDatum(TL_DATUM); g.setTextColor(COL_PANEL_FG, COL_PANEL);
        String s = nextFerien.name + " " + fmtDM(nextFerien.start) + " - " + fmtDM(nextFerien.end);
        g.drawString(fitText(g, s, 276), ox + 12, oy + 62);
      } else {
        g.setFreeFont(&FreeSansBold9pt7b); g.setTextColor(COL_PANEL_FG, COL_PANEL);
        g.drawString("wird geladen ...", ox + 12, oy + 62);
      }
    });
  }
  drawDots();
}

static void drawCurrent(bool force = false) {
  if (page == PG_WX) drawWeather(force);
  else if (page == PG_DAY) drawDay(force);
  else drawMain(force);
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

// ---------- Helligkeit + Nachtmodus ----------------------------
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
// true, wenn der Modus gewechselt hat
static bool applyTheme() {
  bool n = isNightTime();
  if (n == nightMode) return false;
  nightMode = n;
  P = n ? PAL_NIGHT : PAL_DAY;
  return true;
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
static void drawOutlineButton(const Btn& b, const char* label) {
  drawButton(b, label, COL_BG, COL_FG);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 12, COL_FG);
  tft.drawRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 11, COL_FG);
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
        tft.fillRect(qrX + (x + 2) * scale, qrY + (y + 2) * scale, scale, scale, COL_DARK);
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
  P = PAL_DAY; nightMode = false;   // Einrichtung immer im Tag-Design
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
  web->on("/streets.csv", HTTP_GET, []() {
    if (STREETS_GZ_LEN == 0) { web->send(200, "text/plain; charset=utf-8", ""); return; }
    web->sendHeader("Content-Encoding", "gzip");
    web->sendHeader("Cache-Control", "max-age=86400");
    web->send_P(200, "text/plain; charset=utf-8", (const char*)STREETS_GZ, STREETS_GZ_LEN);
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
    j += "\"street\":\"" + jsonEsc(C.street) + "\",";
    j += "\"streetVal\":\"" + jsonEsc(C.streetVal) + "\",";
    j += "\"hnr\":\"" + jsonEsc(C.hnr) + "\",";
    j += "\"bins\":" + String(C.bins) + ",";
    j += "\"rest\":" + String(C.rest) + ",";
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
    String street = web->arg("street"), streetVal = web->arg("streetVal"), hnr = web->arg("hnr");
    bool muellChanged = street != C.street || streetVal != C.streetVal || hnr != C.hnr;
    prefs.begin("abfahrt", false);
    // Passwort leer lassen = altes behalten (nur beim gleichen WLAN)
    if (pass.length() || ssid != C.ssid) prefs.putString("pass", pass);
    prefs.putString("ssid", ssid);
    if (key.length()) prefs.putString("key", key);
    if (stop != C.stopId) { prefs.putFloat("lat", 0); prefs.putFloat("lon", 0); }
    prefs.putString("stop", stop);
    prefs.putString("stopName", web->arg("stopName"));
    prefs.putString("place", web->arg("place"));
    prefs.putString("dir", web->arg("dir"));
    prefs.putString("dirName", web->arg("dirName"));
    prefs.putString("lines", web->arg("lines"));
    prefs.putString("street", street);
    prefs.putString("streetVal", streetVal);
    prefs.putString("hnr", hnr);
    int b = web->arg("bins").toInt();
    prefs.putUChar("bins", (uint8_t)(b >= 1 && b <= 15 ? b : 15));
    int rr = web->arg("rest").toInt();
    prefs.putUChar("rest", (uint8_t)(rr == 1 || rr == 4 ? rr : 2));
    int w = web->arg("walk").toInt();
    if (w >= 1 && w <= 45) prefs.putUChar("walk", (uint8_t)w);
    prefs.end();
    if (muellChanged) clearMuell();
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
//  Einstellungs-Menue (lange auf die Kopfzeile druecken)
// ============================================================
static const Btn B_DONE  = {226, 8, 84, 36};
static const Btn B_MINUS = {22, 84, 64, 52};
static const Btn B_PLUS  = {234, 84, 64, 52};
static const Btn B_LIGHT = {10, 160, 90, 48};
static const Btn B_MUELL = {106, 160, 96, 48};
static const Btn B_SETUP = {208, 160, 102, 48};

static String muellStatus() {
  if (!C.street.length()) return "Muellkalender: keine Strasse eingerichtet";
  if (muellErr.length() && (!muellAt || muellTry > muellAt)) return "Muell: " + muellErr;
  if (!muellAt || !pickupCount) return "Muellkalender: noch nicht geladen";
  struct tm lt; time_t a = muellAt; localtime_r(&a, &lt);
  uint32_t at = (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
  return "Muellkalender: Stand " + fmtDM(at) + ", Termine bis " + fmtDM(pickups[pickupCount - 1].ymd);
}

static void drawMuellFooter(uint16_t col) {
  tft.fillRect(0, 214, 320, 22, COL_BG);
  tft.setTextFont(2);
  tft.setTextColor(col, COL_BG); tft.setTextDatum(MC_DATUM);
  tft.drawString(fitText(tft, muellStatus(), 310), 160, 224);
}

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
  drawOutlineButton(B_MUELL, "Muell laden");
  drawOutlineButton(B_SETUP, "Neu einrichten");
  drawMuellFooter(COL_SUB);
}

static uint32_t touchStart = 0;
static bool wasTouched = false;
static uint32_t settingsSince = 0;
static int tStartX = 0, tStartY = 0, tLastX = 0, tLastY = 0;
static bool longDone = false;

static void goPage(int p) {
  page = constrain(p, 0, 2);
  pageSince = millis();
  drawCurrent(true);
}

static void handleTouch() {
  int x = 0, y = 0;
  bool t = readTouch(x, y);
  uint32_t ms = millis();

  if (screen == MAIN) {
    if (t && !wasTouched) {
      touchStart = ms; tStartX = tLastX = x; tStartY = tLastY = y; longDone = false;
      pageSince = ms;
      Serial.printf("[Touch] %d,%d\n", x, y);
    }
    if (t) { tLastX = x; tLastY = y; }
    // langer Druck auf die Kopfzeile -> Menue
    if (t && !longDone && tStartY < 50 && abs(tLastX - tStartX) < 25 && ms - touchStart > 1200) {
      longDone = true;
      screen = SETTINGS; settingsSince = ms; drawSettings();
      while (ts.touched()) delay(10);
      wasTouched = false;
      return;
    }
    if (!t && wasTouched && !longDone) {
      int dx = tLastX - tStartX, dy = tLastY - tStartY;
      if (abs(dx) > 50 && abs(dx) > abs(dy)) {         // Wischen
        if (dx < 0 && page < 2) goPage(page + 1);
        else if (dx > 0 && page > 0) goPage(page - 1);
      } else if (ms - touchStart < 800 && page == PG_DEP && msgHead.length() &&
                 tStartX >= 130 && tStartY >= 100 && tStartY <= 146) {   // Tippen auf den Hinweis
        screen = MESSAGE; settingsSince = ms; drawMessage();
      }
    }
  } else if (screen == MESSAGE) {
    if (t && !wasTouched) {
      screen = MAIN; drawCurrent(true);
      while (ts.touched()) delay(10);
      t = false;
    } else if (ms - settingsSince > PAGE_TIMEOUT_S * 1000UL) {
      screen = MAIN; drawCurrent(true);
    }
  } else {
    if (t && !wasTouched) {
      settingsSince = ms;
      bool redraw = true;
      if (hit(B_MINUS, x, y)) C.walk = max(1, C.walk - 1);
      else if (hit(B_PLUS, x, y)) C.walk = min(45, C.walk + 1);
      else if (hit(B_LIGHT, x, y)) { C.light = C.light % 3 + 1; applyBrightness(); }
      else if (hit(B_MUELL, x, y)) {
        redraw = false;
        if (!C.street.length()) { drawMuellFooter(COL_SUB); }
        else {
          drawButton(B_MUELL, "laedt ...", COL_ORANGE, COL_DARK);
          bool ok = updateMuell();
          if (ok) drawButton(B_MUELL, "geladen", COL_GREEN, COL_WHITE);
          else drawButton(B_MUELL, "Fehler", COL_RED, COL_WHITE);
          drawMuellFooter(ok ? COL_GREEN : COL_RED);
          settingsSince = millis();
        }
      }
      else if (hit(B_SETUP, x, y)) { saveMenuSettings(); runPortal(true); }
      else if (hit(B_DONE, x, y)) { saveMenuSettings(); screen = MAIN; drawCurrent(true); redraw = false; }
      else redraw = false;
      if (redraw) drawSettings();
    }
    if (screen == SETTINGS && millis() - settingsSince > 30000) {  // Zeitueberschreitung
      saveMenuSettings(); screen = MAIN; drawCurrent(true);
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
  loadMuell();
  if (!configComplete()) runPortal(false);     // erster Start: Einrichtung

  showMessage("Verbinde WLAN", C.ssid);
  waitForWifi();
  showMessage("Hole Uhrzeit", "Zeitserver");
  startTime();
  applyTheme();
  showMessage("Hole Abfahrten", C.stopName);
  bool ok = fetchDepartures();
  if (ok) lastOk = time(nullptr);
  nextFetchMs = millis() + (ok ? INTERVAL_NORMAL_S : errBackoff) * 1000UL;
  fetchStopCoords();
  showMessage("Hole Wetter", C.place);
  fetchWeather();
  fetchAlerts();
  nextWxMs = millis() + WX_INTERVAL_S * 1000UL;
  showMessage("Hole Feiertage", "Hessen");
  updateHolidays();
  if (muellDue()) { showMessage("Hole Muellkalender", C.street); updateMuell(); }
  applyBrightness();
  pageSince = millis();
  drawCurrent(true);
}

void loop() {
  handleTouch();
  if (screen != MAIN) { delay(15); return; }

  uint32_t ms = millis();
  bool changed = false;
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
    if (page == PG_DEP) { shownHero = ""; shownPanel = ""; }
    applyBrightness();
  }

  if ((int32_t)(ms - nextWxMs) >= 0) {
    fetchStopCoords();
    bool ok = fetchWeather();
    fetchAlerts();
    nextWxMs = millis() + (ok ? WX_INTERVAL_S : 120) * 1000UL;
  }

  static uint32_t lastSlow = 0;
  if (ms - lastSlow >= 60000) {       // einmal pro Minute: Feiertage + Muell pruefen
    lastSlow = ms;
    updateHolidays();
    if (muellDue()) updateMuell();
  }

  // Wetter/Tagesblatt nach einer Weile zurueck zur Abfahrt
  if (page != PG_DEP && millis() - pageSince > PAGE_TIMEOUT_S * 1000UL && !ts.touched()) {
    page = PG_DEP;
    changed = true;
  }

  if (applyTheme()) { changed = true; applyBrightness(); }

  static uint32_t lastTick = 0;
  if (changed) { drawCurrent(true); lastTick = millis(); }
  else if (millis() - lastTick >= 1000) {   // Countdown laeuft lokal jede Sekunde weiter
    lastTick = millis();
    drawCurrent();
  }
  delay(15);
}
