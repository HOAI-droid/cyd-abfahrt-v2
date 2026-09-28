#pragma once
// ============================================================
//  Abfahrtsmonitor – Grundeinstellungen
//  WLAN, RMV-Schluessel, Haltestellen, Linien und Gehzeit werden
//  NICHT hier eingetragen, sondern beim ersten Start ueber das
//  Einrichtungs-WLAN (siehe README).
// ============================================================

// Name des Einrichtungs-WLANs, das das CYD beim ersten Start oeffnet
#define SETUP_AP_NAME "Abfahrtsmonitor-Setup"

// Startwert fuer die Gehzeit (Minuten)
#define DEFAULT_WALK_MIN 6

// Ab wie vielen Minuten bis zum Losgehen die Zahl rot wird
#define RED_AT_MIN 3

// ---- Abfrage-Intervalle (Sekunden) --------------------------
#define INTERVAL_FAST_S    30    // wenn deine Bahn in < 15 min faellig ist
#define INTERVAL_NORMAL_S  60    // tagsueber
#define INTERVAL_NIGHT_S   600   // ausserhalb der aktiven Zeit
#define INTERVAL_ERROR_MAX 300   // Obergrenze beim Zurueckfahren nach Fehlern

// Aktive Zeit (Stunden, lokal). Ausserhalb: seltener abfragen + Display gedimmt.
#define ACTIVE_FROM_H 5
#define ACTIVE_TO_H   24

// ---- Nachtmodus (invertiert + abgedunkelt), Minuten nach Mitternacht
#define NIGHT_FROM_MIN (21 * 60 + 30)  // ab 21:30
#define NIGHT_TO_MIN   (6 * 60 + 30)   // bis 06:30

// ---- Seiten (Wischen) ----------------------------------------
#define PAGE_TIMEOUT_S 30              // Wetter/Tagesblatt: nach 30 s zurueck zur Abfahrt

// ---- Wetter + Unwetterwarnung ---------------------------------
#define WX_INTERVAL_S  900             // alle 15 min (Open-Meteo, DWD ueber Bright Sky)

// ---- Muellkalender (EAD Darmstadt ueber Muellmax) --------------
#define MUELL_REFRESH_DAYS 28          // alle 4 Wochen neu laden
#define MUELL_MIN_AHEAD_DAYS 21        // frueher laden, wenn die Termine bald enden
#define MUELL_REMIND_FROM_H 17         // Tonne auf Seite 1: Vorabend ab 17:00 ...
#define MUELL_REMIND_TO_H   9          // ... bis zum Abholtag 09:00

// ---- Uhrzeit: Zeitserver + Zeitzone Deutschland (inkl. Sommer/Winterzeit)
#define TZ_INFO  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_1    "ptbtime1.ptb.de"     // Physikalisch-Technische Bundesanstalt
#define NTP_2    "de.pool.ntp.org"
#define NTP_3    "pool.ntp.org"
#define NTP_RESYNC_MIN 60              // alle 60 min neu synchronisieren

// ---- Hardware-Feinheiten -----------------------------------
// Falls Farben invertiert aussehen (manche CYD-Varianten): auf 1 setzen
#define INVERT_DISPLAY 0
// RGB-LED hinten auf dem CYD rot leuchten lassen, wenn es knapp wird
#define LED_WARNUNG 0

// Touch-Kalibrierung (Rohwerte des XPT2046). Bei Abweichungen im
// seriellen Monitor die Rohwerte der Ecken ablesen und hier eintragen.
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800
