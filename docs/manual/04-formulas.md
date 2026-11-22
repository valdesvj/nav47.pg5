# 4. Formulas, accuracy and references

How NAV47 computes every number, how accurate it is, and where each method comes from. Each section names the source
file, so you can read the code beside the formula.

Notation: φ latitude (N +), λ longitude (E +), δ declination, ε obliquity, R distance, C course, V speed, *d*
distance in nautical miles (1 nm = 1852 m). Angles in degrees unless stated.

- [Accuracy at a glance](#accuracy-at-a-glance)
- [1. Time](#1-time)
- [2. The ephemeris: Sun, Moon, planets, stars](#2-the-ephemeris-sun-moon-planets-stars)
- [3. Sight reduction, rising and setting](#3-sight-reduction-rising-and-setting)
- [4. Tides and streams](#4-tides-and-streams)
- [5. Time zones](#5-time-zones)
- [6. The route: geodesics, rhumb lines, composite sailing](#6-the-route-geodesics-rhumb-lines-composite-sailing)
- [7. The GPS: fix, DR, current, passage, CPA](#7-the-gps-fix-dr-current-passage-cpa)
- [8. DP: DNV-ST-0111 level 1 and ABS SKP](#8-dp-dnv-st-0111-level-1-and-abs-skp)
- [9. How it is tested](#9-how-it-is-tested)
- [10. References](#10-references)

---

## Accuracy at a glance

| What | Accuracy | Against |
|---|---|---|
| Sun GHA, Dec | 0.015′ | JPL DE421 + ERFA, 2000–2050 (Almanac 47's tests) |
| Moon GHA, Dec | 0.10′ (HP 0.006′) | JPL DE421 + ERFA, 2000–2050 |
| Planets | 0.044′ | JPL DE421 + ERFA, 2000–2050 |
| Stars | 0.003′ | JPL DE421 + ERFA, 2000–2050 |
| Hc, Zn | 0.08′ | JPL DE421 + ERFA, 2000–2050 |
| NAV47's C against Almanac 47's Python | the Moon, Sun events and phase identical; the rest within 1 × 10⁻¹¹° | 302 dates × 71 results |
| Sunrise, sunset, twilight | within about 15 s of the full Sun; the minute shown changes by one in about 1 % of cases | the full Sun series |
| Tide astronomy (V₀ + u, f) | 0.07°, 0.0003 (main constituents, 2020–2040) | NOAA's tables |
| Tide engine, C against Tide 47 | 10⁻¹⁴ | 478 checks |
| Geodesic (GC) distance and course | round-off, about 15 nanometres | GeographicLib, by construction |
| Rhumb line distance / course | < 1 µm / < 10⁻⁹° | arbitrary-precision (mpmath), 8 ellipsoids |
| Passage monitor: XTE | 0.5 % of the distance off, + 2 m (rhumb legs: within 30 nm of the track) | geographiclib, nearest point of the leg |
| Passage monitor: BRG, DTG to the waypoint | exact (the geodesic inverse) | geographiclib |
| Time zones | identical at every change of 2026, 2027 and 2030 | the IANA database |
| ABS SKP | identical, bit for bit | DPCAP47's `dp_abs` |

**The limits the numbers do not show.**

- **UT1 and UTC.** NAV47 takes the clock (or the GPS) as UT1. They differ by less than 0.9 s, which moves a GHA by
  up to 0.23′.
- **ΔT is fixed at 69.2 s** (TT − UT1). That is close to its value now; years from now the Moon, the fastest body,
  will show it first.
- **Tides are the astronomical tide only.** The real height also depends on the station's constants (their source and
  the length of record), on wind, pressure and surges. The official tables of the port authority are the reference.
- **The GPS's accuracy is the receiver's.** NAV47 shows it (HDOP, the GST error) but does not improve it. DR does not
  allow for the current.
- **DP OPS and ABS SKP are static planning methods**, not the vessel's own DP capability analysis.

---

## 1. Time

`src/astro.c` (the C twin of Almanac 47's `c47astro.py`).

**Julian date** (Meeus ch. 7), for a date Y M D and UT hours h; when M ≤ 2, Y ← Y − 1 and M ← M + 12:

```
A  = INT(Y / 100)          B = 2 − A + INT(A / 4)
JD = INT(365.25 (Y + 4716)) + INT(30.6001 (M + 1)) + D + B − 1524.5 + h / 24
```

**UT** is the calculator clock minus CLOCK TZ, or the GPS's UT (GPSSET: UT FROM GPS), taken as UT1. Terrestrial Time
TT = UT1 + ΔT with ΔT = 69.2 s. Julian centuries and millennia from J2000.0:

```
T = (JD_TT − 2451545.0) / 36525        τ = T / 10
```

## 2. The ephemeris: Sun, Moon, planets, stars

### The Sun (SUNA)

1. **VSOP87D**, truncated: the Earth's heliocentric longitude L, latitude B and distance R as series
   Σ A cos(B + C τ) τᵏ (Bretagnon & Francou 1988). The full set of coefficients is compiled in (`src/almdata.c`,
   4,344 numbers).
2. Geocentric: λ = L + 180°, β = −B.
3. **FK5** (Meeus 32.3): Δλ = −0.09033″, Δβ = +0.03916″ (cos λ′ − sin λ′), with λ′ = λ − 1.397° T.
4. **Nutation**, IAU 1980: Δψ, Δε from the series of Seidelmann (1982).
5. **Aberration**: −20.4898″ / R.
6. **Obliquity**: ε = (84381.448″ − 46.815″ T + Δε) / 3600.
7. To the equator:
   ```
   δ  = asin(sin β cos ε + cos β sin ε sin λ)
   RA = atan2(sin λ cos ε − tan β sin ε, cos λ)
   ```
8. **GHA Aries** (apparent sidereal time at Greenwich), with *d* = JD_UT − 2451545.0:
   ```
   GHA♈ = 280.46061837 + 360.98564736629 d + 0.000387933 T² + Δψ cos ε
   GHA  = GHA♈ − RA            SHA = 360° − RA
   ```
9. Semi-diameter SD = 15.99383′ / R (R in au).

### The Moon (MOON)

The lunar theory of **Meeus ch. 47** (60 terms in longitude and distance, 60 in latitude), plus **55 correction
terms fitted to JPL DE421** (Almanac 47). Then nutation and the conversion to RA, Dec and GHA as for the Sun. With the
distance Δ in km:

```
HP = asin(6378.14 / Δ)        SD = 358473400″ / Δ
```

Phase (PHAS): the illuminated fraction k = (1 + cos *i*) / 2, *i* the phase angle (Meeus ch. 48), and the age from
the elongation.

### The planets (PLAN)

Venus, Mars, Jupiter and Saturn: **VSOP87D** truncated for the planet and the Earth, geocentric by subtraction,
**light time** in two passes, FK5, aberration, nutation. HP = 8.794″ / Δ (Δ in au).

### The stars (STAR)

The 57 navigation stars and Polaris from a J2000 catalogue, with proper motion, **IAU 2006 precession**, nutation and
annual aberration.

## 3. Sight reduction, rising and setting

**Hc and Zn** (CHZ), the local hour angle LHA = GHA + λ:

```
sin Hc = sin φ sin δ + cos φ cos δ cos LHA
Zn     = atan2(−cos δ sin LHA,  cos φ sin δ − sin φ cos δ cos LHA)      (0 to 360°)
```

Hc is the computed altitude: no refraction, dip or semi-diameter is applied.

**Rising, setting and twilight** (SUNRISE). The hour angle at the altitude h₀:

```
cos LHA₀ = (sin h₀ − sin φ sin δ) / (cos φ cos δ)
```

with h₀ = −0°50′ for sunrise and sunset (the upper limb: 34′ of refraction and 16′ of semi-diameter), −6° civil and
−12° nautical twilight. The time is iterated until the correction is under 0.0003 h (about 1 s). The iterations use
the low-precision Sun of the *Astronomical Almanac* (0.01°); the GHA and Dec on the screens always come from the full
Sun. Meridian passage: LHA = 0. "--:--": the Sun does not reach h₀ that day.

## 4. Tides and streams

`src/tide.c`: the C twin of Tide 47's exact engine (`python/tide47ref.py`).

The height at time *t* is the sum of 60 harmonic constituents (NOAA's standard 37, 8 more from TICON, 15 for the
Aramco tables):

```
h(t) = Z₀ + Σᵢ fᵢ Hᵢ cos(Vᵢ(t) + uᵢ − gᵢ)
```

- Hᵢ, gᵢ: the station's amplitude and phase lag (its constants).
- Vᵢ(t) = the Doodson numbers × the astronomical arguments (τ, s, h, p, N′, p′) of the day.
- fᵢ, uᵢ: the 18.6-year nodal corrections of Schureman's *Manual of Harmonic Analysis and Prediction of Tides*: the
  method behind NOAA's and the Admiralty's predictions.

**High and low waters**: h and its first three derivatives every 2 hours, a quintic (Hermite) of h′ between them, its
sign every half hour, then Newton's method on the quintic.

**Streams**: the same sum for the two components u (east) and v (north). The slacks are the roots of u (a
rectilinear stream); the maxima come from u′, or from g = u u′ + v v′ (a rotary stream).

**The FLOW atlas (T&F)**: the stream vectors of your atlas at HW−6 … HW+6 of the reference port, scaled by the day's
range against springs, with the wind's drift added (as Tide 47's FLOW47).

## 5. Time zones

`src/zones.c`: 38 zones and 311 cities. Summer time is worked out on the calculator from the rules of the **IANA time
zone database** (2026e): the transition dates (last Sunday of March, and so on) and their local times, for any year.

## 6. The route: geodesics, rhumb lines, composite sailing

`src/geo47/` (geo47) and `src/route.c`. The ellipsoids, with *a* the semi-major axis and *f* the flattening
(e² = f (2 − f)):

| | *a* (m) | 1 / *f* |
|---|---|---|
| WGS84 | 6378137.0 | 298.257223563 |
| GRS80 | 6378137.0 | 298.257222101 |
| International 1924 (Hayford, ED50) | 6378388.0 | 297.0 |
| Clarke 1866 (NAD27) | 6378206.4 | 294.978698214 |
| Bessel 1841 | 6377397.155 | 299.1528128 |
| Krassovsky | 6378245.0 | 298.3 |
| Sphere (IUGG mean radius) | 6371008.7714 | 0 |
| Custom | yours | yours |

### GC: the great circle as a geodesic

The shortest path on the ellipsoid, by **Karney's algorithms** (2013). NAV47 carries GeographicLib's own C
implementation (`src/geo47/geodesic.c`, C. F. F. Karney, MIT licence), ported to the calculator. It solves the
**inverse problem** (two points → distance s₁₂ and the courses α₁, α₂) and the **direct problem** (a point, a course,
a distance → the other point) to round-off: about 15 nanometres on the Earth. ROUTE shows the **initial** course; PLAN
splits the geodesic so each piece can be steered.

### RL: the rhumb line on the ellipsoid

A line of constant course. With the isometric latitude ψ and the meridian arc M:

```
ψ(φ) = asinh(tan φ) − e · atanh(e sin φ)
tan C = Δλ / Δψ
s     = ΔM / cos C
```

NAV47 uses the numerically stable form

```
r = ΔM / Δψ       (metres a radian of ψ)
x = Δλ · r,   y = ΔM
C = atan2(x, y),   s = hypot(x, y)
```

The meridian arc ΔM is computed **exactly**, as a geodesic along the meridian (GeographicLib). On an east–west leg
(Δψ → 0), r → ν cos φ, the radius of the parallel, with ν = a / √(1 − e² sin² φ): parallel sailing needs no special
case. The shorter way round is taken across 180°. A rhumb line into a pole is refused.

### CS: composite sailing

When the great circle would go beyond a limiting latitude φ_L: a geodesic to the point where a geodesic touches φ_L
(its vertex, course 090° or 270°), along the parallel φ_L, then a geodesic on to the destination. When the great
circle stays inside φ_L it is used as it is.

### Splitting (PLAN)

Points every *n* nm along each leg, the legs cut into *n* equal parts, or points on whole meridians (every 10°: 20° W,
30° W, …). Every point lies on the leg as sailed.

## 7. The GPS: fix, DR, current, passage, CPA

`src/nmea.c`, `src/navfix.c`, `src/passage.c`, `src/gpsui.c`; the web page uses the same C.

### The fix

- **Checksum**: the XOR of every character between `$` and `*`, as two hex digits. A wrong one is counted and
  dropped.
- **Position**: RMC (status A), GGA or GNS (quality > 0). **Quality**: GGA's 0 none, 1 GPS, 2 DGPS, 4 RTK, 5 RTK
  float, 6 estimated; RMC's mode D also means DGPS.
- **Satellites**: GSV (elevation, azimuth, signal); "used" when a GSA named it in the last 3 s; one system from the
  talker (GP GL GA GB GQ) or from the number.
- **Heading**: HDT, THS (unless invalid), HDG + variation (+ deviation). **Speed through the water**: VHW.

### DR (dead reckoning)

Every second, from the time of the last fix, with the course C and speed V (see the GPS manual for where they come
from), Δt in seconds:

```
d  = V Δt / 3600                       (nm)
Δφ = d cos C / 60                      (degrees)
Δλ = d sin C / (60 cos φ)
```

When the fix returns, the DR's error is the distance and bearing from the DR position to the fix.

### Set and drift

The ground vector less the water vector, east and north:

```
cₑ = SOG sin COG − STW sin HDG
cₙ = SOG cos COG − STW cos HDG
```

smoothed with a time constant of 30 s: c ← c + (1 − e^(−Δt/30)) (c_new − c). Set = atan2(cₑ, cₙ), drift = |c|. STW
is the log's, or the DR SPEED typed. Without a gyro NAV shows the drift angle COG − HDG instead.

### The passage monitor

For the active leg from A to B, the vessel at P.

**A geodesic leg.** The distance s₁₃ and course α₁₃ from A to P, and the leg's course α₁₂ at A, all from the geodesic
inverse; then on the sphere of the same arc (R = 6371008.8 m):

```
XTE = R asin( sin(s₁₃/R) · sin(α₁₃ − α₁₂) )
ATD = R acos( cos(s₁₃/R) / cos(XTE/R) )
```

XTE is positive when the vessel is right of the track; ATD (along-track) is negative before A.

The course to steer (CTS) is the geodesic's course at the along-track point.

**A rhumb leg**, on the Mercator plane (x = Δλ, y = ψ), where the leg is straight. P's perpendicular offset p and the
foot of the perpendicular at the fraction *t* of the leg:

```
p   = (x_P Δy − y_P Δx) / L          t = (x_P Δx + y_P Δy) / L²         L = √(Δx² + Δy²)
XTE = p · ν(φ_f) cos φ_f             (the plane's scale at the foot, latitude φ_f)
ATD = the rhumb distance from A to the foot
```

CTS is the rhumb course.

**To the waypoint**: BRG and DTG from P to B by the geodesic inverse (exact). **Arrival**: DTG ≤ the arrival radius,
or ATD ≥ the leg's length (past the waypoint); then the next leg (NEXT LEG AUTO).

**Along the plan**, with cum(k) the distance of the plan's point k from the start:

```
DTG to a waypoint W = DTG to B + cum(W) − cum(B)
TTG = DTG / V,   ETA = now + TTG
```

V is the SOG, or the plan's speed when the SOG is 0.5 kn or less.

### CPA and TCPA of your objects

On the local plane (an object is fixed): its position p = (Δλ cos φ, Δφ) × 60 nm and the vessel's velocity
v = SOG (sin COG, cos COG):

```
TCPA = (p · v) / |v|²            CPA = | p − v · TCPA |
```

**ZONE AHEAD**: CPA < the object's zone and 0 < TCPA < 30 min. **IN THE ZONE**: the distance is under the zone
(111 320 m a degree of longitude × cos φ, 110 574 m a degree of latitude: within metres at these distances).

### The gyro error

Error = Zn (the Sun's true azimuth, computed) − the bearing read on the gyro. Positive: the gyro reads **LOW** (add
the error to a gyro bearing); negative: **HIGH** (subtract).

### The pictures (not navigation)

BOW draws the world as a pinhole camera at the helideck: for a point *f* nm ahead and *r* nm to starboard, *h*
metres up, the column is 200 + F r / f and the row HOR + K / f · (1 − h / 20 m). Heights are seen from 20 m; far
platforms are drawn up to 4 times larger, so they stay visible. The live CHART draws the vessel at true length when it
is at least 40 pixels.

## 8. DP: DNV-ST-0111 level 1 and ABS SKP

`src/dp/dpcap.c` is DPCAP47's engine (copied by `make dpsync`, never edited here); `src/dpops.c` is NAV47's.

**DP OPS** uses **DNV-ST-0111 (December 2021), level 1**: the wind, current and wave-drift forces on the hull from the
vessel's dimensions and areas, multiplied by the dynamic factor 1.25, against the thrusters' forces by an optimal
thrust allocation. NAV47 adds:

- the wind, current and sea **from their own directions**;
- **the platform's lee**: within D = 5 platform heights downwind, the wind left is w = w_min + (1 − w_min) · t / D,
  *t* the distance from the platform along the wind, w_min = 0.3 close in; a solid platform shelters the waves the
  same way;
- the vessel **placed** with her working point STANDOFF m off a face, moved out until no corner is closer than
  MINCLEAR;
- **working limits** (a thruster's thrust capped, `LIM TUN 30`) in the intact case; full thrust after a failure.

**ABS SKP** follows the **ABS Guide for Dynamic Positioning Systems, Section 9**:

- the weather of the Guide's **Table 1** (the North Sea: Hs, Tp and the 1-minute mean wind, with the probability P that
  each wind is not exceeded), a fixed current, the forces × (1 + margin) (20 %);
- the ABS **bollard pull** where the vessel file gives a propeller's diameter D and power P:
  T₀ = K (P D)^(2/3), K = 848 open and 1250 ducted, less the current's effect
  EFC = 1 − (K₁ / P_A)ⁿ · 0.11 V_c (tunnels keep DNV's level 1 thrust);
- for each of the 36 directions (every 10°) the limiting wind, intact, with each single thruster lost and with each
  redundancy group lost;
- **SKP** = the mean over the 36 directions of P(the limiting wind is not exceeded): a intact, b and c the least and
  the most effective thruster lost, d the worst group lost.

The ABS Guide points to API RP 2SK or OCIMF for the wind and current forces; like DPCAP47, NAV47 uses DNV's level 1
formulas for them.

## 9. How it is tested

| Test | Command | Checks |
|---|---|---|
| The astronomy against Almanac 47's Python | `make test` | 302 dates × 71 results; MOON47 400 dates |
| The tides against Tide 47's Python | `make test` | 478 checks within 10⁻¹⁴ |
| The zones against IANA | `make test` | every change of 2026, 2027, 2030 |
| The rhumb line, composite and splits against mpmath and geographiclib | `make test-geo` | 8 ellipsoids × 8 cases |
| The NMEA parser, the DR and the current | `make test-gps` | 69 checks on known sentences and a synthetic voyage |
| The passage monitor against geographiclib | `make test-gps` | 1044 checks in C, 204 against the nearest point of each leg |
| DP OPS, and ABS SKP against `dp_abs` | `make test-dp` | the placements, the balances, ABS bit for bit |
| The USB console | `make test-usb` | the stream of the data sheet |

The ephemeris accuracy against JPL is Almanac 47's (its `tests/test_ephem.py`, against JPL DE421 with ERFA);
NAV47's C equals Almanac 47's Python to 10⁻¹¹°.

## 10. References

**Astronomy**

- Meeus, J. (1998). *Astronomical Algorithms*, 2nd ed. Willmann-Bell. (Julian date ch. 7; the Sun ch. 25 and 32;
  the Moon ch. 47; the phases ch. 48–49.)
- Bretagnon, P., Francou, G. (1988). Planetary theories in rectangular and spherical variables: VSOP87 solutions.
  *Astronomy & Astrophysics* 202, 309–315.
- Seidelmann, P. K. (1982). 1980 IAU Theory of Nutation: the final report of the IAU Working Group on Nutation.
  *Celestial Mechanics* 27, 79–106.
- Capitaine, N., Wallace, P. T., Chapront, J. (2003). Expressions for IAU 2000 precession quantities. *Astronomy &
  Astrophysics* 412, 567–586 (the P03 precession adopted by the IAU in 2006).
- Folkner, W. M., Williams, J. G., Boggs, D. H. (2009). The Planetary and Lunar Ephemeris DE421. *JPL IPN Progress
  Report* 42-178.
- IAU SOFA / ERFA: *Essential Routines for Fundamental Astronomy* (the reference in Almanac 47's tests).
- *The Nautical Almanac* and *The Astronomical Almanac*, HM Nautical Almanac Office and US Naval Observatory (the
  low-precision Sun, section C).

**Navigation and geodesy**

- Karney, C. F. F. (2013). Algorithms for geodesics. *Journal of Geodesy* 87, 43–55. doi:10.1007/s00190-012-0578-z.
- Karney, C. F. F. *GeographicLib*, the C library (`geodesic.c`, MIT licence), https://geographiclib.sourceforge.io.
- Snyder, J. P. (1987). *Map Projections: A Working Manual*. US Geological Survey Professional Paper 1395 (the
  isometric latitude, the Mercator projection on the ellipsoid).
- Bowditch, N. *The American Practical Navigator* (NGA Pub. 9): the sailings, dead reckoning, current sailing (set and
  drift), sight reduction, the gyro error by azimuth.
- NMEA 0183, *Standard for Interfacing Marine Electronic Devices*, v4.11, National Marine Electronics Association; IEC
  61162-1.
- Natural Earth (public domain): the coastlines of CHART and the web page.

**Tides**

- Schureman, P. (1958). *Manual of Harmonic Analysis and Prediction of Tides*. US Coast and Geodetic Survey, Special
  Publication 98.
- Doodson, A. T. (1921). The harmonic development of the tide-generating potential. *Proceedings of the Royal Society
  A* 100, 305–329.
- Codiga, D. L. (2011). *Unified Tidal Analysis and Prediction Using the UTide Matlab Functions*. GSO Technical Report
  2011-01, University of Rhode Island (used in Tide 47's tests).
- NOAA CO-OPS harmonic constants (public domain).
- TICON-3, tidal constants from GESLA tide gauges (CC BY 4.0), PANGAEA, doi:10.1594/PANGAEA.951610.

**Time zones**

- The IANA Time Zone Database, release 2026e (public domain), https://www.iana.org/time-zones.

**Dynamic positioning**

- DNV (2021). *DNV-ST-0111 Assessment of station keeping capability of dynamic positioning vessels*, Edition December
  2021.
- ABS (2024). *Guide for Dynamic Positioning Systems*, Section 9, *Station Keeping Performance* (free at eagle.org).

**The 47 family** (the author's own projects, which NAV47 joins)

- Almanac 47 (the astronomy and its tests against JPL), Tide 47 (the tide engine and its tests against NOAA and UTide),
  Zones 47 (the time zones), DPCAP47 (the DP engine, DNV and ABS).
