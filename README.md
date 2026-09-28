# Abfahrtsmonitor für das CYD

Zeigt, in wie vielen Minuten du losgehen musst, um die nächste Bahn zu erreichen. Das Design ist „Stein Anthrazit“, die Daten kommen live vom RMV (inklusive Verspätungen und Ausfällen). Das Gerät funktioniert für jede Haltestelle im RMV-Gebiet.

## Erste Inbetriebnahme

1. **RMV-Schlüssel beantragen:** unter https://opendata.rmv.de/site/anmeldeseite.html. Der Schlüssel kommt nach einigen Tagen per Mail.
2. **Flashen:** VS Code mit der PlatformIO-Erweiterung öffnen und diesen Ordner laden. CYD per USB anschließen und „Upload“ klicken (oder `pio run -t upload`). In `config.h` musst du nichts eintragen.
3. **Einrichten:** Das CYD öffnet das WLAN **„Abfahrtsmonitor-Setup“** und zeigt einen QR-Code.
   - Mit dem Handy den QR-Code scannen oder das WLAN von Hand wählen. Die Einrichtungsseite öffnet sich von selbst. Falls nicht, im Browser `192.168.4.1` aufrufen.
   - Eintragen: Heim-WLAN mit Passwort, RMV-Schlüssel, **Stadt → Haltestelle** für die Abfahrt, **Stadt → Haltestelle** für die Fahrtrichtung, dazu optional Linien und die Gehzeit.
   - Auf „Speichern und starten“ tippen. Das Display startet neu und zeigt die Abfahrten.

## Später ändern

- **Menü:** lange (gut 1 Sekunde) auf die Kopfzeile mit der Uhr drücken.
  - **Gehzeit:** mit − und + einstellen.
  - **Licht:** Helligkeit in 3 Stufen.
  - **Neu einrichten:** öffnet wieder das Einrichtungs-WLAN. Alle bisherigen Werte sind vorausgefüllt, Passwort und Schlüssel bleiben erhalten, wenn du die Felder leer lässt. Mit „Abbrechen“ auf dem Display geht es ohne Änderung zurück.
- **WLAN nicht erreichbar:** Das Display bietet „Nochmal“ und „Neu einrichten“ an. Nach 60 Sekunden versucht es sich von selbst erneut zu verbinden.

## Anzeige

- **Minutenzahl:** Sie zählt herunter, bis du losgehen musst. Ab 3 Minuten wird sie rot.
- **Ausfall:** Fällt eine Bahn aus, springt der Countdown auf die nächste. Oben rechts erscheint „Ausfall 6 07:47“, in der Liste wird die Fahrt durchgestrichen.
- **Statuszeile:** Bei Problemen steht dort „offline, Stand hh:mm“ oder ein RMV-Hinweis wie „RMV: Schluessel pruefen“.

## Technik

| Was | Wie |
|---|---|
| Uhrzeit | Zeitserver der PTB und pool.ntp.org, stündlich neu abgeglichen. Sommer- und Winterzeit stellen sich von selbst um. |
| Abfahrten | RMV-Abfahrtstafel (`/hapi/departureBoard`) |
| Abfrage | 30 s, wenn die Bahn in weniger als 15 min fällig ist; sonst 60 s; nachts 10 min. Bei Fehlern schrittweise länger, bis 5 min. |
| Haltestellenliste | 11.117 gültige RMV-Haltestellen, komprimiert (120 KB) in die Firmware eingebaut |
| Speicher | Einstellungen im internen Flash; bleiben auch ohne Strom und über Neustarts erhalten |

Weitere Grundwerte (Intervalle, Rot-Grenze, Nachtzeit) stehen in `include/config.h`.

## Haltestellenliste aktualisieren (bei neuer Tarifperiode)

```
pip install openpyxl
python tools/make_stops.py RMV_Haltestellen_....xlsx
```
Danach neu flashen. Die Einstellungen bleiben erhalten.

## Falls etwas nicht passt

- **Farben invertiert oder vertauscht:** Manche CYD-Varianten brauchen das. In `config.h` `INVERT_DISPLAY 1` setzen. Stimmen Rot und Blau trotzdem nicht, in `platformio.ini` `-DTFT_RGB_ORDER=TFT_BGR` ergänzen.
- **Board mit zwei USB-Buchsen (ST7789-Display):** In `platformio.ini` `-DILI9341_2_DRIVER=1` durch `-DST7789_DRIVER=1` ersetzen.
- **Touch trifft daneben:** Beim Tippen zeigt der serielle Monitor (115200 Baud) `[Touch] x,y`. Die `TOUCH_*`-Werte in `config.h` anpassen.
- **RMV-Antwort prüfen:** `python tools/rmv_test.py SCHLUESSEL 3024329 3024747` zeigt die Rohdaten.
