#!/bin/bash
# demo_gif.sh - NAV47 at sea for a forum post: an animated GIF made in the PC simulator (make sim), the same
# made-up route, platforms and GPS log as the manual's pictures (docs/manual/screens.sh); no private data.
#   bash docs/manual/demo_gif.sh   ->  build/demo/NAV47-demo.gif   (needs python3 Pillow geographiclib)
set -e
cd "$(dirname "$0")/../.."
TOP=$PWD; IMG=$TOP/build/demo; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$IMG" "$W/simdisk/NAV47"
make -s sim >/dev/null
python3 test/nmea/make_log.py "$W/log.nmea" --minutes 60 --legs 045:30,090:30 --lost 1500:90
# the route along the log's track, platforms near it, two waypoints in the list
python3 - "$W" <<'PY'
import sys
from geographiclib.geodesic import Geodesic
w = sys.argv[1]; G = Geodesic.WGS84
secs = [[]]
for l in open(w + "/log.nmea"):
    if not l.strip(): secs.append([]); continue
    secs[-1].append(l.strip())
def pos(t):
    for l in secs[t]:
        if l.startswith("$GPRMC") and ",A," in l:
            f = l.split(","); return int(f[3][:2]) + float(f[3][2:]) / 60, int(f[5][:3]) + float(f[5][3:]) / 60
wp = [pos(0), pos(1799), pos(3599)]
wp[1] = (wp[1][0] + 0.002, wp[1][1] - 0.002)
open(w + "/simdisk/NAV47/ROUTE.TXT", "w").write("NAV47 ROUTE 1\nRL\nLIM 60.0\nSPLIT 0 0.000\nELL 0\nSPEED 10.0\nETD 0\n"
    + "%.7f %.7f START\n%.7f %.7f TURN\n%.7f %.7f FINISH\n" % (wp[0] + wp[1] + wp[2]))
at = lambda p, b, d: (lambda g: (g["lat2"], g["lon2"]))(G.Direct(p[0], p[1], b, d * 1852))
mid = at(wp[1], 225, 1.5)
objs = [("platform", "Alpha", *at(wp[1], 95, 1.6), 500, 40, 60, 40), ("platform", "Bravo", *at(mid, 135, 0.55), 500, "", "", ""),
        ("wellhead", "Well 3", *at(wp[1], 45, 0.8), 500, "", "", ""), ("spm", "SPM 2", *at(wp[2], 0, 1.2), 500, "", "", ""),
        ("buoy", "B7", *at(wp[0], 60, 2.4), 50, "", "", "")]
open(w + "/simdisk/NAV47/OBJECTS.CSV", "w").write("type,name,lat,lon,zone_m,orient,face_m,side_m\n" + "".join("%s,%s,%.6f,%.6f,%s,%s,%s,%s\n" % o for o in objs))
open(w + "/simdisk/NAV47/WAYPTS.CSV", "w").write("name,lat,lon\nSTART,%.7f,%.7f\nANCHORAGE,%.7f,%.7f\n" % (wp[0] + at(wp[0], 200, 1.0)))
open(w + "/simdisk/NAV47/NAV47.CFG", "w").write("NAV47 CFG 1\nLAT 26.3\nLON 50.3\nTZ 3\nLIVE 1\nLT 1\nGPS 1 0 1 1 10 -1 0 1\nPSG 0.250 0.200 1 80\n")
PY
cp "$W/simdisk/NAV47/OBJECTS.CSV" "$W/objects.csv"
# one run of the simulator: the keys, and a snapshot (SNAP) with how long it shows in the GIF (ms)
K=(); F=()
snap() { K+=("SNAP:f$(printf %03d ${#F[@]})"); F+=("$1"); }
keys() { K+=("$@"); }
wait_s() { keys "WAIT:$1"; }
keys F6 F6 F6 F1; for i in $(seq 25); do wait_s 1000; done      # FIX: the first fix
for i in $(seq 6); do snap 500; wait_s 1000; done
keys F2; wait_s 1000; snap 1400                                   # SATS
keys F3; for i in $(seq 1100); do wait_s 1000; done               # NAV, the vessel 18 min on
keys ENTER; wait_s 1000                                           # the alarm (a zone ahead) acknowledged
for i in $(seq 8); do snap 450; for j in $(seq 15); do wait_s 1000; done; done
keys F4                                                           # BOW: the platforms come
for i in $(seq 36); do for j in $(seq 9); do wait_s 1000; done; snap 160; done
keys F3 0; wait_s 300; keys 7                                     # the live CHART round the turn
for i in $(seq 24); do for j in $(seq 8); do wait_s 1000; done; snap 220; done
keys RUN; wait_s 1500; snap 1200                                  # MOB
keys ENTER F5; wait_s 1000; snap 1300                             # NAV: the MOB on its top row
keys F1; wait_s 1000; snap 1800
(cd "$W" && NAV47_CLOCK="2026-11-22 09:00" NAV47_NMEA="$W/log.nmea" "$TOP/build/sim" "${K[@]}" >/dev/null 2>&1 || true)
for i in "${!F[@]}"; do n=f$(printf %03d $i); python3 test/sim/render.py "$W/$n" "$W/$n.png"; done
printf "%s\n" "${F[@]}" > "$W/ms.txt"
python3 - "$W" "$IMG/NAV47-demo.gif" <<'PY'
import sys, glob
from PIL import Image
w, out = sys.argv[1], sys.argv[2]
ms = [int(x) for x in open(w + "/ms.txt").read().split()]
fr = [Image.open("%s/f%03d.png" % (w, i)).convert("P", palette=Image.ADAPTIVE, colors=4) for i in range(len(ms))]
fr[0].save(out, save_all=True, append_images=fr[1:], duration=ms, loop=0, optimize=True)
print(out, len(fr), "frames")
PY
