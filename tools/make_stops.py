#!/usr/bin/env python3
"""
Erzeugt aus der RMV-Haltestellenliste (Excel) die kompakte Liste fuer das CYD.

  pip install openpyxl
  python tools/make_stops.py RMV_Haltestellen_....xlsx

Ergebnis: include/stops_data.h (gzip-komprimiert, wird mit der Firmware geflasht).
Nur gueltige Haltestellen (GUELTIG_BIS leer oder in der Zukunft).
Format je Zeile: HAFAS_ID;Gemeinde;Ortsteil;Haltestellenname
"""
import datetime
import gzip
import os
import sys

import openpyxl


def main(path):
    wb = openpyxl.load_workbook(path, read_only=True)
    ws = wb.worksheets[0]
    rows = ws.iter_rows(values_only=True)
    head = [str(h) for h in next(rows)]
    ix = {h: i for i, h in enumerate(head)}
    today = datetime.datetime.now()
    out, skipped = [], 0
    for r in rows:
        hid = r[ix["HAFAS_ID"]]
        name = r[ix["HST_NAME"]]
        if not hid or not name:
            continue
        bis = r[ix["GUELTIG_BIS"]]
        if isinstance(bis, datetime.datetime) and bis < today:
            skipped += 1
            continue
        gem = (r[ix["GEMEINDENAME"]] or "").strip()
        ot = (r[ix["ORTSTEILNAME"]] or "").strip()
        if ot == gem:
            ot = ""
        clean = lambda s: str(s).replace(";", ",").replace("\n", " ").strip()
        out.append(f"{int(hid)};{clean(gem)};{clean(ot)};{clean(name)}")
    out.sort(key=lambda s: s.split(";")[1] + s.split(";")[3])
    data = gzip.compress(("\n".join(out) + "\n").encode("utf-8"), 9)

    target = os.path.join(os.path.dirname(__file__), "..", "include", "stops_data.h")
    with open(target, "w") as f:
        f.write("// automatisch erzeugt von tools/make_stops.py – nicht von Hand aendern\n")
        f.write(f"// {len(out)} Haltestellen, Stand {today:%Y-%m-%d}\n#pragma once\n#include <Arduino.h>\n")
        f.write(f"static const size_t STOPS_GZ_LEN = {len(data)};\n")
        f.write("static const uint8_t STOPS_GZ[] PROGMEM = {\n")
        for i in range(0, len(data), 24):
            f.write(",".join(str(b) for b in data[i:i + 24]) + ",\n")
        f.write("};\n")
    print(f"{len(out)} Haltestellen ({skipped} abgelaufene entfernt), {len(data)/1024:.0f} KB komprimiert -> {target}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1])
