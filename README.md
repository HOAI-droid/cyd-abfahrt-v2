# Abfahrtsmonitor für das CYD – Version 2

> Version 2, abgeleitet von [cyd-abfahrt](https://github.com/HOAI-droid/cyd-abfahrt). Die erste Version bleibt dort unverändert.

Zeigt, in wie vielen Minuten du losgehen musst, um die nächste Bahn zu erreichen. Das Design ist „Stein Anthrazit“, die Daten kommen live vom RMV (inklusive Verspätungen, Ausfällen und Störungsmeldungen). Das Gerät funktioniert für jede Haltestelle im RMV-Gebiet.

**Neu in Version 2**
- **Nachtmodus** von 21:30 bis 06:30: gleiches Layout, Farben invertiert und abgedunkelt.
- **Seite 2 – Wetter** (nach links wischen): Temperatur, Wetter, Tagesverlauf 07–22 Uhr mit Regenwahrscheinlichkeit, amtliche Unwetterwarnungen des DWD.
- **Seite 3 – Tagesblatt** (nochmal wischen): Datum, die nächsten zwei Müllabfuhr-Termine (EAD Darmstadt), nächster Feiertag und Schulferien in Hessen.
- **Tonne neben der Uhr** am Vorabend der Abholung ab 17:00 bis 09:00 am Abholtag.
- **Störungen** des RMV zu deinen Linien in der Statuszeile, Meldungstext per Tippen.

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
- **Müllabfuhr (optional, Darmstadt):** die ersten Buchstaben der Straße eintippen und aus der Liste wählen, bei Bedarf die Hausnummer, dazu die Tonnen, die angezeigt werden sollen.
- Auf „Speichern und starten“ tippen. Das Display startet neu und zeigt die Abfahrten.

### Selbst kompilieren (optional)
VS Code mit der Erweiterung **PlatformIO IDE** installieren und diesen Ordner öffnen. In PlatformIO die Umgebung `cyd` oder `cyd2usb` wählen und **Upload** klicken, oder im Terminal `pio run -e cyd -t upload`. In `config.h` musst du nichts eintragen.

Jeder Push baut die Firmware außerdem automatisch per GitHub Actions und aktualisiert das Release.

## Später ändern

- **Menü:** lange (gut 1 Sekunde) auf die Kopfzeile mit der Uhr drücken.
  - **Gehzeit:** mit − und + einstellen.
  - **Licht:** Helligkeit in 3 Stufen.
  - **Muell laden:** holt den Müllkalender sofort neu, z. B. wenn das Gerät länger aus war. Unten steht, wie aktuell der Kalender ist.
  - **Neu einrichten:** öffnet wieder das Einrichtungs-WLAN. Alle bisherigen Werte sind vorausgefüllt, Passwort und Schlüssel bleiben erhalten, wenn du die Felder leer lässt. Mit „Abbrechen“ auf dem Display geht es ohne Änderung zurück.
- **WLAN nicht erreichbar:** Das Display bietet „Nochmal“ und „Neu einrichten“ an. Nach 60 Sekunden versucht es sich von selbst erneut zu verbinden.

## Seiten

- **Wischen** nach links: Seite 1 (Abfahrt) → Seite 2 (Wetter) → Seite 3 (Tagesblatt). Nach rechts zurück. Nach 30 Sekunden springt das Gerät von selbst zur Abfahrt zurück.
- **Seite 2:** Die Tafel zeigt 07, 10, 13, 16, 19 und 22 Uhr. „Regen ab 16 Uhr“ erscheint, sobald die Regenwahrscheinlichkeit 50 % erreicht. Bei einer Warnung des DWD (ab Stufe „markant“) erscheint oben ein oranger oder roter Balken.
- **Seite 3:** Die nächsten zwei Abholungen mit farbiger Tonne (Restmüll grau, Bio braun, Papier blau, Gelbe Tonne gelb). Am Vorabend wird aus „morgen“ ein oranges „heute rausstellen“.

## Anzeige

- **Minutenzahl:** Sie zählt herunter, bis du losgehen musst. Ab 3 Minuten wird sie rot.
- **Ausfall:** Fällt eine Bahn aus, springt der Countdown auf die nächste. Oben rechts erscheint „Ausfall 6 07:47“, in der Liste wird die Fahrt durchgestrichen.
- **Statuszeile:** Bei Problemen steht dort „offline, Stand hh:mm“ oder ein RMV-Hinweis wie „RMV: Schluessel pruefen“.
- **Störung:** Meldet der RMV Bauarbeiten oder Umleitungen für deine Linien, steht dort z. B. „⚠ Bauarbeiten L1“. Antippen zeigt den ganzen Text.
- **Tonne neben der Uhr:** Am Vorabend der Abholung ab 17:00 bis 09:00 am Abholtag.

## Technik

| Was | Wie |
|---|---|
| Uhrzeit | Zeitserver der PTB und pool.ntp.org, stündlich neu abgeglichen. Sommer- und Winterzeit stellen sich von selbst um. |
| Abfahrten | RMV-Abfahrtstafel (`/hapi/departureBoard`), inklusive Meldungen |
| Wetter | Open-Meteo (ohne Schlüssel), alle 15 min; Ort = Koordinaten der Haltestelle |
| Unwetter | DWD-Warnungen über Bright Sky (`api.brightsky.dev/alerts`), alle 15 min |
| Feiertage, Ferien | OpenHolidays (`openholidaysapi.org`), Hessen, täglich |
| Müllkalender | EAD Darmstadt über Müllmax (iCal-Export), alle 4 Wochen, höchstens ein Versuch pro Tag. Straßenliste wird beim Build erzeugt (`tools/make_streets.py`). |
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
