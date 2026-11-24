#!/bin/bash
# screens.sh - the manuals' pictures, made again from the program itself: the calculator in the PC simulator
# (make sim) and the web page in headless Chrome (make web). Made-up data only: a route, platforms and a GPS log
# invented for the pictures, in the open Gulf; no private data.
#   bash docs/manual/screens.sh            (from the repository's top; needs python3 Pillow geographiclib, Chrome)
set -e
cd "$(dirname "$0")/../.."
TOP=$PWD; IMG=${IMG:-$TOP/docs/manual/img}; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
mkdir -p "$IMG" "$W/simdisk/NAV47"
make -s sim >/dev/null
python3 test/nmea/make_log.py "$W/log.nmea" --minutes 60 --legs 045:30,090:30 --lost 1500:90 --rot 30
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
shot() {   # shot NAME KEYS...: the simulator from its start, the keys, then the screen as a PNG (2x)
  local n=$1; shift
  (cd "$W" && rm -f *.pbm *.txt && NAV47_CLOCK="2026-11-22 09:00" NAV47_NMEA="$W/log.nmea" "$TOP/build/sim" "$@" SNAP:$n >/dev/null 2>&1 || true)
  python3 test/sim/render.py "$W/$n" "$IMG/$n.png"
}
w() { for i in $(seq "$1"); do printf "WAIT:1000 "; done; }
G="F6 F6 F6"                                    # the GPS's menu row
shot calc_almanac   WAIT:300
shot calc_split     F2
shot calc_sky       F3 WAIT:300
shot calc_allsky    F4
shot calc_moon      F5
shot calc_tf        F6 F2
shot calc_tide      F6 F3
shot calc_route     F6 F4
shot calc_plan      F6 F4 RUN
shot calc_chart     F6 F4 0 9 9 9
shot calc_voyage    F6 F4 RUN F5 RUN WAIT:3000 RUN
shot calc_eta       F6 F4 RUN F5 RUN F4
shot calc_zones     F6 F5
shot calc_set       F6 F6 F1
shot calc_files     F6 F6 F2
shot calc_wpts      F6 F6 F2 F4
shot calc_dp        F6 F6 F4 WAIT:500
shot calc_abs       F6 F6 F4 WAIT:500 DOT WAIT:300
shot gps_fix        $G F1 $(w 25)
shot gps_sats       $G F1 $(w 20) F2 0
shot gps_nav        $G F3 $(w 1200) ENTER WAIT:300
shot gps_bow        $G F3 $(w 1720) ENTER F4 WAIT:250
shot gps_chart      $G F3 $(w 1735) ENTER 0 WAIT:250 7
shot gps_dr         $G F1 $(w 1525) ENTER WAIT:300
shot gps_set        $G F5
shot gps_mob        $G F3 $(w 900) RUN $(w 3)
M="$G F3 $(w 900) RUN $(w 3) ENTER"            # a MOB marked, the alarm acknowledged
shot gps_mob_fix    $M F1 $(w 2)
shot gps_mob_bow    $M F4 $(w 2)
[ -n "$CALC_ONLY" ] && exit 0
# the web page
CH=$(command -v google-chrome-stable || command -v chromium || command -v google-chrome)
web() {   # web NAME WIDTHxHEIGHT SECONDS HASH
  rm -rf "$W/prof"; timeout 150 "$CH" --headless=new --disable-gpu --no-sandbox --allow-file-access-from-files --user-data-dir="$W/prof" \
    --window-size=$2 --virtual-time-budget=$(( $3 * 1000 )) --screenshot="$IMG/$1.png" "file://$TOP/web/index.html#$4" >/dev/null 2>&1 || true
}
if [ -n "$CH" ]; then
  web web_live       1400,1320 22 "tab=live&ex=field&gpsdemo&gpsx=30"
  web web_route_plan 1400,1000 8  "tab=route&ex=field"
  web web_gps        1300,1000 12 "tab=gps&gpsdemo"
  web web_almanac    1300,900  6  "tab=almanac"
  web web_sky        1300,900  6  "tab=sky"
  web web_tide       1300,900  6  "tab=tide"
  web web_zones      1300,900  6  "tab=zones"
  web web_calc       1300,800  20 "tab=calc&gpsdemo&keys=F6,F6,F6,F1,ENTER"
fi
# smaller files
python3 - "$IMG" <<'PY'
import sys, glob
from PIL import Image
for f in glob.glob(sys.argv[1] + "/*.png"):
    im = Image.open(f)
    if im.width > 1000: im = im.resize((im.width * 3 // 4, im.height * 3 // 4), Image.LANCZOS)
    im.convert("P", palette=Image.ADAPTIVE, colors=128).save(f, optimize=True)
PY
ls "$IMG" | wc -l
