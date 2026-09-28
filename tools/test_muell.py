#!/usr/bin/env python3
"""Prueft den Muellmax-Ablauf wie die Firmware (Strasse -> Hausnummer -> iCal) und zeigt die Abfallarten."""
import re
import sys
from collections import Counter

import requests

URL = "https://www.muellmax.de/abfallkalender/ead/res/EadStart.php"
H = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"}


def ses(html):
    m = re.search(r'<input[^>]*name="mm_ses"[^>]*>', html)
    return re.search(r'value="([^"]*)"', m.group(0)).group(1)


def run(street, hnr):
    s = requests.Session()
    r = s.get(URL, headers=H, timeout=30)
    r = s.post(URL, data={"mm_ses": ses(r.text), "mm_aus_ort.x": 0, "mm_aus_ort.y": 0}, headers=H, timeout=30)
    r = s.post(URL, data={"mm_ses": ses(r.text), "xxx": 1, "mm_frm_str_name": street, "mm_aus_str_txt_submit": "suchen"}, headers=H, timeout=30)
    if 'name="mm_frm_str_sel"' in r.text:
        print("  Strassenauswahl erscheint noch einmal")
        r = s.post(URL, data={"mm_ses": ses(r.text), "xxx": 1, "mm_frm_str_sel": street, "mm_aus_str_sel_submit": "weiter"}, headers=H, timeout=30)
    if 'name="mm_frm_hnr_sel"' in r.text:
        sel = re.search(r'name="mm_frm_hnr_sel".*?</select>', r.text, re.S).group(0)
        opts = re.findall(r'<option[^>]*value="([^"]*)"[^>]*>(.*?)</option>', sel, re.S)
        print("  Hausnummern:", opts[:8], "...", len(opts))
        pick = next((v for v, t in opts if v and (t.strip() == str(hnr) or v.split(";")[2:3] == [str(hnr)])), next(v for v, t in opts if v))
        r = s.post(URL, data={"mm_ses": ses(r.text), "xxx": 1, "mm_frm_hnr_sel": pick, "mm_aus_hnr_sel_submit": "weiter"}, headers=H, timeout=30)
    r = s.post(URL, data={"mm_ses": ses(r.text), "xxx": 1, "mm_ica_auswahl": "iCalendar-Datei"}, headers=H, timeout=30)
    fra = dict(re.findall(r'<input[^>]*name="(mm_frm_fra[^"]*)"[^>]*value="([^"]*)"', r.text))
    fra.update({n: v for v, n in re.findall(r'<input[^>]*value="([^"]*)"[^>]*name="(mm_frm_fra[^"]*)"', r.text)})
    print("  Abfallarten-Felder:", fra)
    data = {"mm_ses": ses(r.text), "xxx": 1, "mm_frm_type": "termine", "mm_ica_gen": "iCalendar-Datei laden"}
    data.update(fra)
    r = s.post(URL, data=data, headers=H, timeout=30)
    ics = r.text
    print("  iCal:", "ja" if "BEGIN:VCALENDAR" in ics else "NEIN", len(ics), "Zeichen")
    sums = Counter(l.split(":", 1)[1].strip() for l in ics.splitlines() if l.startswith("SUMMARY"))
    dates = [l.split(":", 1)[1].strip() for l in ics.splitlines() if l.startswith("DTSTART")]
    print("  SUMMARY:", dict(sums))
    print("  DTSTART Beispiele:", dates[:3], "bis", dates[-1:] if dates else "")


if __name__ == "__main__":
    for st, hn in [("Frankfurter Landstraße", 1), ("Messeler Straße", 10)]:
        print("Strasse:", st)
        try:
            run(st, hn)
        except Exception as e:
            print("  Fehler:", e)
