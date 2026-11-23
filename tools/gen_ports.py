#!/usr/bin/env python3
"""gen_ports.py - the world's ports for the web page (and the calculator's /NAV47/PORTS.CSV): NGA's World Port Index
(Pub. 150, US government work, public domain) into web/ports.js and PORTS.CSV.

    python3 tools/gen_ports.py UpdatedPub150.csv web/ports.js build/ports/PORTS.CSV   (msi.nga.mil: Publications, WPI)
    python3 tools/gen_ports.py wpi.json ...      (the same index as JSON: INDEX_NO PORT_NAME COUNTRY LATITUDE LONGITUDE)
Each port's other names, searched and shown: tools/port_aliases.csv (the common English names, NAV47's own list) and,
from NGA's CSV, its Alternate Port Name and UN/LOCODE (Dubai: "Dubayy", also "AEDXB").
    python3 tools/gen_ports.py web/ports.js OUT.js OUT.csv --locode DIR    (the PRIVATE page only: make web-private
        LOCODE=DIR) the UN/LOCODE release's csv/ (UNECE: its terms forbid redistribution, so never into web/): each
        port the 4 nearest UN/LOCODE ports of its country within 10 km, their names and codes added (searched, not shown).
"""
import csv, io, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))

args = [a for a in sys.argv[1:] if not a.startswith("--")]
src, out_js, out_csv = args[0], args[1], args[2]
locode = sys.argv[sys.argv.index("--locode") + 1] if "--locode" in sys.argv else None
rows = []
if src.lower().endswith(".js"):                         # the page's own ports.js: its ports, their names kept
    t = open(src, encoding="utf-8").read()
    for l in json.loads(t[t.index("const PORTS = ") + 14:t.rindex(";")]).split("\n"):
        f = l.split("|")
        if len(f) >= 5: rows.append((f[4], f[0], f[1], f[2], f[3], (f[5] if len(f) > 5 else "") + "|" + (f[6] if len(f) > 6 else ""), ""))
elif src.lower().endswith(".json"):
    for r in json.load(open(src)):
        rows.append((r.get("INDEX_NO"), r.get("PORT_NAME"), r.get("COUNTRY"), r.get("LATITUDE"), r.get("LONGITUDE"), "", ""))
else:
    t = open(src, encoding="utf-8-sig", errors="replace").read()
    rd = csv.DictReader(io.StringIO(t))
    def col(d, *names):
        for n in names:
            for k in d:
                if k and k.strip().lower() == n: return d[k]
    for d in rd:
        rows.append((col(d, "world port index number", "index_no"), col(d, "main port name", "port_name"),
                     col(d, "country code", "country"), col(d, "latitude"), col(d, "longitude"),
                     col(d, "alternate port name") or "", col(d, "un/locode", "unlocode") or ""))
alias = {}                                         # (WPI name, country) -> the common names
for d in csv.DictReader(l for l in open(os.path.join(HERE, "port_aliases.csv")) if not l.startswith("#")):
    alias.setdefault((d["wpi"].strip().lower(), d["country"].strip().upper()), []).append(d["common"].strip())
ports = []
for no, name, cc, la, lo, alt, loc in rows:
    try: la, lo = float(la), float(lo)
    except (TypeError, ValueError): continue
    name = re.sub(r"\s+", " ", (name or "").strip()).title()
    if not name or abs(la) > 90 or abs(lo) > 180: continue
    cc = (cc or "").strip()[:20]
    aka = alias.get((name.lower(), cc.upper()), [])[:]     # the common names: the first one is shown
    disp = aka[0] if aka else ""
    if src.lower().endswith(".js"):                     # already its names: "shown|searched"
        disp, al = (alt or "|").split("|", 1); aka = [a for a in al.split(";") if a]; alt = ""
    for a in re.split(r"[;,/]", alt or ""):             # NGA's alternate names
        a = re.sub(r"\s+", " ", a.strip()).title()
        if a and a.lower() != name.lower() and a not in aka: aka.append(a)
    loc = re.sub(r"[^A-Z]", "", (loc or "").upper())
    if len(loc) == 5: aka.append(loc)                   # UN/LOCODE
    ports.append((name, cc, round(la, 5), round(lo, 5), int(no) if str(no or "").isdigit() else 0, disp, ";".join(aka)))
if locode:                                             # the private page: UN/LOCODE's names and codes
    import math, glob
    lc = {}
    for fn in sorted(glob.glob(os.path.join(locode, "*CodeListPart*.csv"))):
        for r in csv.reader(open(fn, encoding="utf-8", errors="replace")):
            if len(r) < 11 or not r[6] or r[6][0] != "1": continue
            m = re.match(r"(\d{2})(\d{2})([NS])\s+(\d{3})(\d{2})([EW])", r[10].strip())
            if not m: continue
            la = (int(m[1]) + int(m[2]) / 60) * (1 if m[3] == "N" else -1); lo = (int(m[4]) + int(m[5]) / 60) * (1 if m[6] == "E" else -1)
            lc.setdefault(r[1], []).append((la, lo, r[3].strip(), r[4].strip(), r[1] + r[2]))
    n = 0
    for k, (name, cc, la, lo, no, disp, aka) in enumerate(ports):
        near = sorted((6371 * math.hypot(math.radians(q[0] - la), math.radians(q[1] - lo) * math.cos(math.radians(la))), q)
                      for q in lc.get(cc.upper(), []))
        near = [q for d, q in near if d < 10.0][:4]      # the 4 nearest within 10 km: the city and its terminals
        if not near: continue
        al = [a for a in aka.split(";") if a]
        for q in near:
            for a in (q[2], q[3], q[4]):
                if a and a.lower() != name.lower() and a not in al: al.append(a)
        ports[k] = (name, cc, la, lo, no, disp, ";".join(al)); n += 1
    print("UN/LOCODE: %d ports in %d countries; %d of the index's ports matched (10 km)" % (sum(map(len, lc.values())), len(lc), n))
ports.sort(key=lambda p: (p[0], p[1]))
with open(out_js, "w") as o:
    o.write("/* ports.js - GENERATED by tools/gen_ports.py from %s: do not edit.\n" % os.path.basename(src))
    o.write(" * NGA's World Port Index (Pub. 150; US government work, public domain): %d ports. */\n" % len(ports))
    o.write("const PORTS = %s;\n" % json.dumps("\n".join("%s|%s|%s|%s|%d|%s|%s" % p for p in ports), ensure_ascii=False))   # name|cc|lat|lon|no|shown|searched
os.makedirs(os.path.dirname(out_csv) or ".", exist_ok=True)
with open(out_csv, "w", newline="") as o:
    w = csv.writer(o, lineterminator="\n"); w.writerow(["name", "lat", "lon", "country"])
    for p in ports:                                      # the common name first (the calculator's 20 characters)
        w.writerow([(p[5] or p[0])[:20], "%.5f" % p[2], "%.5f" % p[3], p[1]])
print("%s: %d ports, %d with a common name, %d with other names -> %s, %s" % (src, len(ports), sum(1 for p in ports if p[5]), sum(1 for p in ports if p[6]), out_js, out_csv))
