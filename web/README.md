# NAV47 on the web

One page, `web/index.html`, opened as a file (no server needed). Everything is computed by the calculator's own C
engines, compiled to WebAssembly with the DM42n program itself in one file, `web/nav47.js` (`make web`).

| Tab | |
|---|---|
| **Route** | a chart (Mercator or globe: drag, wheel, Fit) with the coastline (Natural Earth 110 m / 50 m), your **objects** and their keep-out zones, the route planned round the zones, the **check**, the **plan** (CSV, NMEA, GPX, ROUTE.TXT, *To the calculator*), the **compare** panel (great circle, rhumb line, composite, the ellipsoids) and the **voyage** (the Mermaid Asiana, or a plane from 100 kn; speed, departure, play, follow; the date and time on board, DEP ARR, gone, to go, run, DTG) |
| **Almanac** | the Sun, the Moon, the planets and the stars above 10° (GHA, Dec, Hc, Zn), the Sun's day, the Moon's phases |
| **Sky** | the dome from the DR (as the calculator's SKY2), and the brightest stars for sights |
| **Tide** | the public stations: the day's curve, HW / LW (or the streams), day by day |
| **Zones** | every zone's time now, its places, summer time |
| **Calculator** | the DM42n program itself, every page, the keys clicked or typed; *To the calculator* on the Route tab sends the plan to it |

The top: **Now** (the clock) or a fixed UT, the **DR**, the times in **UT** or **ZT** (the DR's zone).

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

## Planning round the zones

With *keep out of the zones* on:

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

## Build

```bash
make coast      # build/coast/COAST_C.BIN, COAST_L.BIN (Natural Earth, public domain)
make web        # web/nav47.js (needs emscripten): the calculator, web/api.c, the coastlines
google-chrome web/index.html
```

Never private data: `make web` makes the private tide table empty, whatever `TIDE47` says.

Links for demos: `#tab=route&ex=field`, `#tab=route&ex=cape&mode=globe&voy=0.4`, `#tab=calc&keys=F6,F4,RUN,RUN,RUN`.

Not for navigation: check against official charts, the Nautical Almanac and the tide tables.
