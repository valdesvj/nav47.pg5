#!/bin/bash
# check.sh - make check: the simulator built with ASan and UBSan, walked through every page with a made-up route,
# platforms, a GPS log and bad lines in OBJECTS.CSV. Any sanitizer report fails it.
#   bash test/sim/check.sh            (from the repository's top; needs gcc with libasan/libubsan, python3)
set -e
cd "$(dirname "$0")/../.."
TOP=$PWD; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
SIM=$TOP/build/sim_check
make -s -B sim SIM_CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" \
     SIM_OUT="$SIM" >/dev/null
mkdir -p "$W/simdisk/NAV47"
python3 test/nmea/make_log.py "$W/log.nmea" --minutes 40 --legs 045:20,090:20 --lost 900:90 --rot 30 >/dev/null
python3 - "$W" <<'PY'
import sys, math
w = sys.argv[1]
def pos(t):
    secs = open(w + "/log.nmea").read().split("\n\n")
    for l in secs[t].split("\n"):
        if l.startswith("$GPRMC") and ",A," in l:
            f = l.split(","); return int(f[3][:2]) + float(f[3][2:]) / 60, int(f[5][:3]) + float(f[5][3:]) / 60
a, b, c = pos(0), pos(1199), pos(2399)
open(w + "/simdisk/NAV47/ROUTE.TXT", "w").write("NAV47 ROUTE 1\nRL\nLIM 60.0\nSPLIT 0 0.000\nELL 0\nSPEED 10.0\nETD 0\n"
    + "%.7f %.7f START\n%.7f %.7f TURN\n%.7f %.7f FINISH\n" % (a + b + c))
# good objects, then lines that must be skipped safely (the strncat overflow: many long fields that are no position)
good = "platform,Alpha,%.6f,%.6f,500,40,60,40\nbuoy,B7,%.6f,%.6f,50\n" % (b[0] + 0.01, b[1] + 0.02, a[0] + 0.02, a[1])
bad = [",".join(["X" * 15] * 12), ",".join(["Y" * 600] * 30), "platform," + ",".join(["9" * 15] * 11), ",,,,,,,,,,,,,,,,", "N,26,30"]
open(w + "/simdisk/NAV47/OBJECTS.CSV", "w").write(good + "\n".join(bad) + "\n")
open(w + "/simdisk/NAV47/WAYPTS.CSV", "w").write("name,lat,lon\nSTART,%.7f,%.7f\n" % a + ",".join(["Z" * 15] * 12) + "\n")
open(w + "/simdisk/NAV47/NAV47.CFG", "w").write("NAV47 CFG 1\nLAT 26.3\nLON 50.3\nTZ 3\nLIVE 1\nLT 1\nGPS 1 0 1 1 10 -1 0 1\nPSG 0.250 0.200 1 80\n")
PY
w() { for i in $(seq "$1"); do printf "WAIT:1000 "; done; }
G="F6 F6 F6"
fail=0; n=0
run() {   # run NAME KEYS...: one walk from the start; a sanitizer report or a crash fails it
  local name=$1; shift; n=$((n + 1))
  if ! (cd "$W" && NAV47_CLOCK="2026-11-22 09:00" NAV47_NMEA="$W/log.nmea" "$SIM" "$@" SNAP:x >"$W/out" 2>&1) \
     || grep -qE "ERROR: AddressSanitizer|runtime error:|LeakSanitizer" "$W/out"; then
    echo "FAIL $name"; grep -E "ERROR|runtime error|#[0-4] " "$W/out" | head -8; fail=1
  fi
}
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1
run almanac  WAIT:300 F2 F3 WAIT:300 F4 F5 UP DOWN 4 6 8 2
run tides    F6 F1 WAIT:500 F6 F2 F6 F3 4 6 F6 F5 8 2
run route    F6 F4 RUN F1 F2 EXIT F6 F4 0 9 9 9 7 7 5 F4 F4
run voyage   F6 F4 RUN F5 RUN WAIT:3000 RUN F4 F2 EXIT
run setup    F6 F6 F1 DOWN DOWN UP F6 F6 F2 F4 EXIT F6 F6 F3 DOWN DOWN
run dp       F6 F6 F4 WAIT:500 DOT WAIT:300 4 6 8 2
run usb      F6 F6 F5 WAIT:300
run waypts   F6 F6 F2 F4 DOWN DOWN UP ENTER
run gps      $G F1 $(w 20) F2 0 4 6 F3 $(w 5) F4 $(w 3) F5 DOWN DOWN UP
run passage  $G F3 $(w 600) ENTER F4 $(w 3) 0 WAIT:250 7 EXIT F1 $(w 3)
run dr       $G F1 $(w 930) ENTER WAIT:300 F3 F4 $(w 2) F5
run mob      $G F3 $(w 300) RUN $(w 3) ENTER F1 $(w 2) F4 $(w 2) F5 F3 0 WAIT:300
echo "make check: $n walks, $([ $fail = 0 ] && echo "no sanitizer errors" || echo FAILED)"
exit $fail
