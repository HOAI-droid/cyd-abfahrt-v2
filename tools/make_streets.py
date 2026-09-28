#!/usr/bin/env python3
"""
Erzeugt die Strassenliste fuer den Muellkalender (EAD Darmstadt ueber Muellmax).

  pip install requests
  python tools/make_streets.py

Ergebnis: include/streets_data.h (gzip-komprimiert, wird mit der Firmware geflasht).
Format je Zeile: Anzeigename;Wert fuer Muellmax (Wert entfaellt, wenn gleich)
Bei einem Fehler bleibt die vorhandene Datei unveraendert (Exit-Code 1).
"""
import gzip
import os
import re
import sys
from html import unescape

import requests

URL = "https://www.muellmax.de/abfallkalender/ead/res/EadStart.php"
HEADERS = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"}


def ses(html):
    m = re.search(r'<input[^>]*name="mm_ses"[^>]*>', html)
    if not m:
        raise RuntimeError("mm_ses nicht gefunden")
    v = re.search(r'value="([^"]*)"', m.group(0))
    return v.group(1) if v else ""


def main():
    s = requests.Session()
    r = s.get(URL, headers=HEADERS, timeout=30)
    r.raise_for_status()
    r = s.post(URL, data={"mm_ses": ses(r.text), "mm_aus_ort.x": 0, "mm_aus_ort.y": 0}, headers=HEADERS, timeout=30)
    r = s.post(URL, data={"mm_ses": ses(r.text), "xxx": 1, "mm_frm_str_name": "", "mm_aus_str_txt_submit": "suchen"},
               headers=HEADERS, timeout=30)
    html = r.text
    if "Abfragelimit" in html:
        raise RuntimeError("Muellmax: Abfragelimit ueberschritten")
    sel = re.search(r'<select[^>]*name="mm_frm_str_sel"[^>]*>(.*?)</select>', html, re.S)
    if not sel:
        raise RuntimeError("Strassenauswahl nicht gefunden")
    out, seen = [], set()
    for val, text in re.findall(r'<option[^>]*value="([^"]*)"[^>]*>(.*?)</option>', sel.group(1), re.S):
        val = unescape(val).strip()
        text = unescape(re.sub(r"<[^>]+>", "", text)).strip()
        if not val or not text or val in seen:
            continue
        seen.add(val)
        out.append(text if text == val else f"{text};{val}")
    if len(out) < 50:
        raise RuntimeError(f"nur {len(out)} Strassen gefunden")
    data = gzip.compress(("\n".join(out) + "\n").encode("utf-8"), 9)
    path = os.path.join(os.path.dirname(__file__), "..", "include", "streets_data.h")
    with open(path, "w", encoding="utf-8") as f:
        f.write("#pragma once\n#include <Arduino.h>\n")
        f.write(f"// Strassenliste EAD Darmstadt ({len(out)} Strassen), erzeugt mit tools/make_streets.py\n")
        f.write("static const uint8_t STREETS_GZ[] PROGMEM = {\n")
        for i in range(0, len(data), 20):
            f.write("  " + ",".join(str(b) for b in data[i:i + 20]) + ",\n")
        f.write("};\n")
        f.write(f"static const size_t STREETS_GZ_LEN = {len(data)};\n")
    print(f"{len(out)} Strassen, {len(data)} Bytes komprimiert")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # Build soll trotzdem weiterlaufen
        print("Strassenliste nicht erzeugt:", e)
        sys.exit(1)
