#!/usr/bin/env python3
"""
RMV-Schnittstelle testen und Haltestellen-IDs finden.

  pip install requests
  python rmv_test.py DEIN_SCHLUESSEL                 -> sucht beide Haltestellen
  python rmv_test.py DEIN_SCHLUESSEL STOP_ID DIR_ID  -> zeigt Live-Abfahrten

Die gefundenen IDs (extId) traegst du in include/config.h ein.
"""
import json
import sys

import requests

BASE = "https://www.rmv.de/hapi"


def find_stop(key, text):
    r = requests.get(f"{BASE}/location.name", params={
        "accessId": key, "input": text, "format": "json", "maxNo": 5, "type": "S"}, timeout=15)
    r.raise_for_status()
    data = r.json()
    items = data.get("stopLocationOrCoordLocation", [])
    print(f"\nSuche: {text}")
    for it in items:
        s = it.get("StopLocation")
        if s:
            print(f"  extId={s.get('extId'):<10}  id={s.get('id')}\n      {s.get('name')}")
    if not items:
        print("  nichts gefunden:", json.dumps(data)[:300])


def departures(key, stop_id, dir_id):
    params = {"accessId": key, "id": stop_id, "format": "json", "maxJourneys": 20, "duration": 120}
    if dir_id:
        params["direction"] = dir_id
    r = requests.get(f"{BASE}/departureBoard", params=params, timeout=15)
    r.raise_for_status()
    data = r.json()
    deps = data.get("Departure", [])
    print(f"\n{len(deps)} Abfahrten")
    for d in deps:
        pas = d.get("ProductAtStop", {})
        line = pas.get("displayNumber") or pas.get("line") or d.get("name")
        print(f"  Linie {line:<4} plan {d.get('time')}  echt {d.get('rtTime', '-'):<8} "
              f"{'AUSFALL' if d.get('cancelled') in (True, 'true') else ''}  -> {d.get('direction')}")
    if deps:
        print("\nErste Abfahrt komplett (zum Pruefen der Feldnamen):")
        print(json.dumps(deps[0], indent=2, ensure_ascii=False)[:3000])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    key = sys.argv[1]
    if len(sys.argv) >= 3:
        departures(key, sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "")
    else:
        find_stop(key, "Darmstadt Im Fiedlersee")
        find_stop(key, "Darmstadt Willy-Brandt-Platz")
