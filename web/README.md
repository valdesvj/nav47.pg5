# NAV47 on the web

One page, `web/index.html`, opened as a file (no server needed). Everything is computed by the calculator's own C
engines, compiled to WebAssembly with the DM42n program itself in one file, `web/nav47.js` (`make web`).

| Tab | |
|---|---|
| **Route** | a chart (Mercator or globe: drag, wheel, Fit) with the coastline (Natural Earth 110 m / 50 m), your **objects** and their keep-out zones, the route planned round the zones, the **check**, the **plan** (CSV, NMEA, GPX, ROUTE.TXT, *To the calculator*), the **compare** panel (great circle, rhumb line, composite, the ellipsoids) and the **voyage** (the Mermaid Asiana, or a plane from 100 kn; speed, departure, play, follow; the date and time on board, DEP ARR, gone, to go, run, DTG) |
| **Weather** | online when you switch it on: the forecast hour by hour (72 h: wind barbs, rain, clouds, thunderstorm risk) and the observed rain radar and infrared satellite clouds (the last 2 hours, every 10 minutes), on the coastline, with your route; Play animates |
| **Almanac** | the Sun, the Moon, the planets and the stars above 10° (GHA, Dec, Hc, Zn), the Sun's day, the Moon's phases |
| **Sky** | the dome from the DR (as the calculator's SKY2), and the brightest stars for sights |
| **Tide** | the public stations, NOAA's current stations and your own (.TID): the day's curve, HW / LW (or the streams), day by day; **Auto**: the station nearest the DR (as the calculator's AUTO) |
| **Flow** | your FLOW atlas (.FLW) as the calculator's T&F: the stream at the DR from HW−6 to HW+6 of the reference port (springs scaled, the wind added), now, the atlas round the DR; the stream stations near the DR |
| **Zones** | as ZONE47: the dot map in 24 columns of 15°, the stops (← →), each stop's places (↑ ↓) with their local time and summer time now, your zone ▲; all the zones in a list below |
| **Calculator** | the DM42n program itself, every page, the keys clicked or typed; *To the calculator* on the Route tab sends the plan to it |

The top: **Now** (the clock) or a fixed UT, the **DR**, the times in **UT** or **ZT** (the DR's zone).

## ROUTE.TXT: the web and the calculator

The Route card's **Load ROUTE.TXT** reads the calculator's route (its waypoints, RL / GC / CS, limit, split,
ellipsoid, speed, ETD); edit it on the chart; **Save ROUTE.TXT** writes it back for `/NAV47/ROUTE.TXT` on the DM42n
(the planned points, round the zones; 100 at most). The Voyage card: speed, departure, and **arrival** (typed: the
speed it needs), as the calculator's ETA page; *a new departure keeps the arrival* or the speed.

A flight (100 kn or more) passes over land: the land check is for vessels; the keep-out zones still apply.

## Objects and their zones

| Object | Zone |
|---|---|
| Platform, rig, wellhead, SPM | 500 m |
| Wreck | 100 m |
| Buoy, light / beacon | 50 m |
| Port / berth | none |

Each object's zone can be changed. Add them on the chart (＋ Object), or load a CSV (`type, name, lat, lon[, zone m]`
or `name, lat, lon`; positions as `27.2` or `27 12.0N`), GeoJSON, GPX or KML points. *Example field* loads a made-up
field (invented positions) to try it. **Your objects and routes stay in this browser (localStorage); they are never
sent anywhere and never go into this repository.**

## Tide and flow files

The page has TICON-3's tide gauges worldwide (CC BY 4.0, `make ticon`: 2343 coastal and harbour gauges of GESLA-3,
converted as Tide 47 converts its TICON-3 ports, datum LAT; named after the nearest place of Natural Earth's populated
places, e.g. "Manama BH" for Mina Salman; the nautical zone of the longitude for local time; files `build/ticon/T*.TID`
by area, ~200 stations each, embedded by `make web`; copy an area to `/NAV47/TIDES/` for the calculator) and NOAA's tidal current stations (public domain, `make noaa`: NOAA CO-OPS's harmonic current stations, their
constants of the default depth, as `.TID` files in `build/noaa/`, embedded by `make web`; copy them to `/NAV47/TIDES/`
for the calculator). **Your own stations and FLOW atlas** (Aramco, Admiralty ...) come from your Tide 47 checkout:

```bash
make tidefiles TIDE47=~/opt/tide47      # build/tidefiles/*.TID and FLOW.FLW (git-ignored: never commit them)
```

Load them on the Tide or Flow tab (*Your tide files*). They stay in this browser, never sent anywhere and never in the
repository; the calculator's own parsers read them, and the Calculator tab gets them too (its FILES, TIDE and T&F pages;
4 files at most). 〰 *Streams* on the Route chart draws the streams at the time shown: the stations (on a chart under 40° wide),
and the atlas's cells. NOAA's 874 harmonic current stations: rectilinear (flood / ebb along the major axis, as NOAA's own
predictions) except the 16 whose current truly turns (rotary).

## Weather (online, off until you switch it on)

- **Forecast:** [Open-Meteo](https://open-meteo.com) (free, no key; CC BY 4.0; free for non-commercial use: 10 000
  points a day, 600 a minute): wind (barbs, knots, by Beaufort), precipitation, cloud cover, CAPE and the weather code
  (thunderstorm 95–99), hourly for 72 h, on a grid over the view (~260 points at most, finer as you zoom in), drawn
  smoothly (bilinear) between the points.
- **Radar and satellite:** [RainViewer](https://www.rainviewer.com/api.html) (free with attribution, personal use):
  the observed rain of the last 2 hours (and its nowcast when given), every 10 minutes, and the infrared clouds.
  Radar exists over land and near the coasts. The radar is drawn on a dark map, opaque and crisp (RainViewer's free
  service gives one colour scheme, Universal Blue).
- **Waves and currents:** [Open-Meteo Marine](https://open-meteo.com/en/docs/marine-weather-api) (free, CC BY 4.0):
  significant wave height (shaded), the waves' direction, and the ocean current (speed in knots, where it sets; coarse:
  0.1 km/h, directions in 45° steps), hourly, the same grid; fetched only with the Waves or Currents layer on.
- **Lightning:** no free global feed may be used this way; the thunderstorm forecast (⚡) and high CAPE instead.
- Only the view's grid points are sent (never your route, objects or files). The Weather chart is the web maps'
  spherical Mercator, so the radar tiles fit the coast.

## Land

Every leg is checked as sailed (every ~2 km) against the Natural Earth 50 m coastline: a leg over land is an error,
in red on the chart (✕ where it crosses) and in the Check, as a leg into a keep-out zone. The coastline is simplified
to ~2 km: it finds a leg across a headland, an island or a peninsula, not close inshore; the first and last 3 km of the
route (and 3 km round a Port object) are left out, since a route starts and ends in harbour. Check on the official chart.

## Planning round the zones

**A passage between two objects:** *Passage* (Route card) from one object to another and *Plan*, or *From* / *To* in
the object list: the route becomes the shortest way between them, clear of every zone. More generally,
with *keep out of the zones* on:

- no leg enters a zone, and every zone is passed by the **clearance** (50 m by default);
- a leg **from or to an object** (＋ Waypoint on an object, or ＋WP in the list) starts or ends on a circle its zone +
  50 m out (550 m from a platform), **on the side that makes the route shortest**; when other zones cover that whole
  circle, the nearest clear wider one (and the Check says so);
- the shortest way: each leg on the azimuthal equidistant plane round its middle (from the geodesic engine, within a
  metre at 100 km), each zone a polygon just outside its circle, a visibility graph and Dijkstra from every start to
  every end, then pulled tight;
- then every leg is **checked as it is sailed** (rhumb line or great circle), every ~25 m, with exact ellipsoidal
  distances: the Check shows the closest pass, and any leg into a zone in red. If the full clearance cannot be kept,
  half, then none, and the Check says so.

`index.html#tab=route&test=avoid` (or `&dense`) plans 40 random fields and lists the results; in this repository's
runs: 79 routes, 15–80 objects each, no leg into a zone, departures and arrivals at 550 m, at most 71 ms.

## DP: the platform for the calculator's DP OPS

The **DP** tab runs the same C as the calculator's DP OPS (`src/dpops.c`).
- **The platform**: a square or a rectangle, the bearing its face 0 looks to, its height, solid or a jacket. Its
  position can be typed, the DR, or a platform object from the Route tab.
- **The working face and side**: AUTO, or a face; the vessel stern-, starboard-, port- or bow-to; the standoff; the
  heading tolerance.
- **The vessel**: a DPCAP47 `.DPV` (the example, or load yours; it stays in this browser), and the working limits.
- **The weather**: the wind; the current from the flow program (your FLOW atlas, else a stream station within
  30 NM) or typed; the sea from DNV's table or typed.
- **Four buttons** compare the set-ups at a glance: starboard side to (parallel), port side to (parallel), stern to
  and bow to, each with its best heading and result. A click takes that set-up.
- **The answer**: the picture with the lee shaded, the best heading, and each thruster's load against its limit,
  intact and after the worst failure. A ring shows every heading of the bow on that face: green holds, and after a
  failure; yellow holds; orange only beyond the limits; red no hold.
- **Sending it on**: *Send to the Calculator tab*, or *Save DPOPS.TXT* and *Save VESSEL.DPV* for `/NAV47/` on the
  DM42n.

## Build

```bash
make coast      # build/coast/COAST_C.BIN, COAST_L.BIN (Natural Earth, public domain)
make noaa       # build/noaa/N*.TID: NOAA's current stations (fetched once, cached)
make ticon      # build/ticon/T*.TID: TICON-3 worldwide (TICON_3.zip in ~/Downloads, or TICON_ZIP=...)
make web        # web/nav47.js (needs emscripten): the calculator, web/api.c, the coastlines
google-chrome web/index.html
```

Never private data: `make web` makes the private tide table empty, whatever `TIDE47` says.

Links for demos: `#tab=route&ex=field`, `#tab=route&ex=field&pass=Buoy%20N1,Echo`, `#tab=route&wps=26.6,50.4;25,52.4` (a leg over Qatar), `#tab=route&ex=cape&mode=globe&voy=0.4`, `#tab=calc&keys=F6,F4,RUN,RUN,RUN`.

Not for navigation: check against official charts, the Nautical Almanac and the tide tables.
