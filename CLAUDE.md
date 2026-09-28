# cyd-abfahrt-v2 – Abfahrtsmonitor für das CYD (Version 2)

ESP32-Firmware (PlatformIO, Arduino-Framework) für das „Cheap Yellow Display“ ESP32-2432S028R (ILI9341, 320×240, XPT2046-Touch).

Version 2, abgeleitet von HOAI-droid/cyd-abfahrt (Version 1 bleibt dort unverändert). Firmware-Dateien heißen `cyd-abfahrt-v2-*.bin`.
Zeigt, in wie vielen Minuten man losgehen muss, um die nächste Bahn an einer RMV-Haltestelle zu erreichen.

## Aufbau
- `src/main.cpp`: gesamte Logik (RMV-Abfrage, Countdown, Zeichnen, Touch-Menü, Einrichtungs-WLAN)
- `include/config.h`: Grundwerte (Intervalle, Rot-Grenze, Zeitzone/NTP, Touch-Kalibrierung). Keine Zugangsdaten.
- `include/portal_html.h`: Einrichtungsseite (Captive Portal), Stadt → Haltestelle-Auswahl im Browser
- `include/stops_data.h`: gzip-komprimierte RMV-Haltestellenliste, erzeugt mit `tools/make_stops.py` – nicht von Hand ändern
- `tools/rmv_test.py`: RMV-API testen

## Wichtige Entscheidungen
- Design „Stein Anthrazit“: Hintergrund #CFCBBF, Text #111111, Tafel #3A3833; Minutenzahl rot (#C62D1F) ab ≤ 3 min
- WLAN, RMV-Schlüssel, Haltestellen-IDs (HAFAS_ID), Linien und Gehzeit werden per Einrichtungs-WLAN gesetzt und in NVS (Preferences, Namespace `abfahrt`) gespeichert
- RMV-API: `https://www.rmv.de/hapi/departureBoard` mit `accessId`, `id`, `direction`; Echtzeit `rtTime`, Ausfall `cancelled`
- Abfrageintervall adaptiv: 30 s (Bahn < 15 min), 60 s, nachts 600 s; Countdown lokal jede Sekunde
- Zeit per NTP (PTB + pool), TZ `CET-1CEST,M3.5.0,M10.5.0/3`
- Displaytexte nur ASCII (eingebaute Schriften ohne Umlaute)
- Nutzer: Haltestelle Darmstadt-Arheilgen Im Fiedlersee (3024329) → Richtung Willy-Brandt-Platz (3024747), Linien 1 und 6, Gehzeit 6 min

## Bauen
`pio run -e cyd` (bzw. `cyd2usb` für ST7789-Variante) / `pio run -e cyd -t upload` / `pio device monitor` (115200 Baud)
GitHub Actions (`.github/workflows/build.yml`) baut bei jedem Push beide Varianten als Einzel-.bin (Adresse 0x0) und aktualisiert das Release `firmware`.
