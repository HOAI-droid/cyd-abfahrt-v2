# Abfahrtsmonitor für das CYD – Version 2

> Version 2, abgeleitet von [cyd-abfahrt](https://github.com/HOAI-droid/cyd-abfahrt). Die erste Version bleibt dort unverändert.

Zeigt, in wie vielen Minuten du losgehen musst, um die nächste Bahn zu erreichen. Das Design ist „Stein Anthrazit“, die Daten kommen live vom RMV (inklusive Verspätungen und Ausfällen). Das Gerät funktioniert für jede Haltestelle im RMV-Gebiet.

## Erste Inbetriebnahme

### 1. RMV-Schlüssel beantragen
Unter https://opendata.rmv.de/site/anmeldeseite.html. Der Schlüssel kommt nach einigen Tagen per Mail.

### 2. Firmware herunterladen
Unter **[Releases → Aktuelle Firmware](../../releases/tag/firmware)** die passende Datei laden:
- `cyd-abfahrt-v2-cyd.bin` – CYD mit **einem** Micro-USB-Anschluss
- `cyd-abfahrt-v2-cyd2usb.bin` – CYD mit **USB-C und Micro-USB**

### 3. Im Browser flashen (Chrome, Chromium oder Edge)
1. CYD per USB anschließen (Datenkabel!).
2. <https://espressif.github.io/esptool-js/> öffnen.
3. Baudrate **921600**, auf **Connect** klicken und den Port wählen (Linux: `ttyUSB0`, Windows: `COM…`).
4. Bei **Flash Address** `0x0` eintragen, die `.bin`-Datei auswählen.
5. **Program** klicken und warten, bis „Leaving…“ erscheint. Dann die **RST**-Taste am CYD drücken.

> Linux (Ubuntu, Zorin, Mint …) einmalig vorbereiten:
> `sudo apt remove brltty && sudo usermod -aG dialout $USER`, danach neu anmelden.
>
> Findet der Browser das Board nicht: Datenkabel prüfen, unter Windows ggf. den CH340- bzw. CP2102-Treiber installieren. Bleibt es bei „Connecting…“ hängen, beim Verbinden die **BOOT**-Taste gedrückt halten.

### 4. Einrichten (per Handy)
Das CYD öffnet das WLAN **„Abfahrtsmonitor-Setup“** und zeigt einen QR-Code.
- Mit dem Handy den QR-Code scannen oder das WLAN von Hand wählen. Die Einrichtungsseite öffnet sich von selbst. Falls nicht, im Browser `192.168.4.1` aufrufen.
- Eintragen: Heim-WLAN mit Passwort (nur **2,4 GHz**), RMV-Schlüssel, **Stadt → Haltestelle** für die Abfahrt, **Stadt → Haltestelle** für die Fahrtrichtung, dazu optional Linien und die Gehzeit.
- Auf „Speichern und starten“ tippen. Das Display startet neu und zeigt die Abfahrten.

### Selbst kompilieren (optional)
VS Code mit der Erweiterung **PlatformIO IDE** installieren und diesen Ordner öffnen. In PlatformIO die Umgebung `cyd` oder `cyd2usb` wählen und **Upload** klicken, oder im Terminal `pio run -e cyd -t upload`. In `config.h` musst du nichts eintragen.

Jeder Push baut die Firmware außerdem automatisch per GitHub Actions und aktualisiert das Release.

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
- **Display weiß, gespiegelt oder falsche Farben:** die jeweils andere Datei (`cyd` bzw. `cyd2usb`) flashen.
- **Touch trifft daneben:** Beim Tippen zeigt der serielle Monitor (115200 Baud) `[Touch] x,y`. Die `TOUCH_*`-Werte in `config.h` anpassen.
- **RMV-Antwort prüfen:** `python tools/rmv_test.py SCHLUESSEL 3024329 3024747` zeigt die Rohdaten.
