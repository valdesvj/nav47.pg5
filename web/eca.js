/* eca.js - NAV47 on the web: the Emission Control Areas (MARPOL Annex VI, regulations 13 and 14, and its definitions of
 * the sea areas), for the Port to port card's ECA miles. [lat, lon] polygons; a voyage is at sea, so a polygon may
 * cross the land: only its sea edges matter.
 *  - Baltic Sea: east of the Skaw's parallel (57°44.8′N) in the Kattegat (Annex I, 1.11.2).
 *  - North Sea: south of 62°N and east of 4°W; the Skagerrak south of 57°44.8′N east of the Skaw; the English Channel
 *    and its approaches east of 5°W and north of 48°30′N (Annex V, 1.14.6). With the Baltic: one polygon.
 *  - Mediterranean (SOx ECA from 1 May 2025): east of 5°36′W (Gibraltar), the Black Sea north of 41°N left out
 *    (Annex I, 1.11.1).
 *  - North American and United States Caribbean: APPROXIMATE here (about ±30 NM): the real limits (Appendix VII)
 *    follow 200 NM from the coasts and the EEZ lines with Mexico, Cuba and the Bahamas, hundreds of points.
 *  Not included: the Canadian Arctic and the Norwegian Sea ECAs (in force 2027).
 * Planning only: check the official coordinates (MARPOL Annex VI, Appendix VII) before a decision. */
"use strict";
const ECA = [
  { name: "North Sea & Baltic", exact: true, poly: [
    [62.0, -4.0], [62.0, 5.3], [61.0, 9.0], [59.9, 10.7], [59.0, 11.4], [57.744, 10.6],   /* Norway's coast, the Skaw */
    [60.0, 12.5], [63.0, 16.5], [66.2, 21.0], [66.2, 26.0], [60.8, 31.0], [59.0, 31.0],  /* over Sweden, the Gulfs */
    [54.0, 23.0], [53.5, 14.0], [53.0, 9.0], [51.0, 4.0], [50.5, 2.0], [49.5, 1.0],     /* the south shores, inland */
    [48.5, -1.5], [48.5, -5.0], [50.0, -5.0], [50.6, -3.0], [52.5, -1.5], [55.0, -2.0], [57.5, -3.0], [58.6, -4.0] ] },
  { name: "Mediterranean", exact: true, poly: [
    [35.85, -5.6], [37.5, -5.6], [40.0, 0.5], [43.6, 3.5], [44.0, 5.5], [44.5, 8.0], [46.5, 12.5], [46.0, 15.0],
    [44.0, 18.5], [42.5, 22.0], [41.0, 26.0], [41.0, 29.6], [38.5, 34.0], [37.2, 36.3], [33.0, 35.6], [31.3, 34.3],
    [31.0, 32.6], [30.8, 29.0], [31.0, 25.0], [32.3, 20.5], [30.3, 19.0], [32.6, 13.5], [33.6, 10.4], [35.0, 8.5],
    [36.6, 3.5], [35.5, -1.0], [35.2, -5.6] ] },
  { name: "North America (Atlantic, Gulf)", exact: false, poly: [
    [24.3, -81.6], [24.5, -80.3], [25.8, -79.5], [27.0, -79.4], [28.3, -77.2], [31.0, -76.6], [34.0, -72.8],
    [37.0, -70.8], [39.6, -67.6], [41.0, -64.4], [42.6, -60.0], [44.0, -55.5], [46.0, -50.0], [48.5, -47.0],
    [51.5, -49.0], [54.5, -52.0], [57.5, -57.0], [60.0, -61.5], [60.0, -68.0], [50.0, -72.0], [45.0, -78.0],
    [38.0, -85.0], [31.0, -94.0], [27.0, -97.6], [26.0, -96.9], [25.9, -93.5], [25.6, -88.5], [24.6, -84.5],
    [24.1, -83.0] ] },
  { name: "North America (Pacific)", exact: false, poly: [
    [32.5, -117.1], [32.0, -118.6], [31.5, -121.0], [33.5, -124.0], [36.5, -125.8], [40.0, -128.5], [44.0, -128.8],
    [48.0, -129.5], [51.5, -133.5], [54.5, -136.8], [56.5, -141.0], [58.0, -147.0], [56.5, -153.0], [54.5, -158.0],
    [53.0, -164.5], [56.0, -165.0], [60.0, -155.0], [61.5, -145.0], [60.0, -135.0], [55.0, -125.0], [49.0, -118.0],
    [40.0, -118.0], [34.0, -115.0] ] },
  { name: "Hawaii", exact: false, poly: [
    [15.6, -156.0], [16.5, -159.6], [19.3, -163.5], [22.7, -164.5], [25.7, -161.5], [25.7, -157.5], [23.5, -153.5],
    [20.0, -151.6], [17.0, -152.5] ] },
  { name: "US Caribbean", exact: false, poly: [
    [17.2, -68.2], [19.8, -68.2], [21.4, -67.0], [21.0, -64.3], [19.4, -63.9], [17.5, -64.4], [16.6, -65.0],
    [16.6, -67.3] ] },
];
/* is the point inside an ECA? its name, or "" (ray casting in degrees: fine at these scales, no 180° crossing) */
function ecaAt(lat, lon) {
  for (const z of ECA) {
    let inside = false; const p = z.poly;
    for (let i = 0, j = p.length - 1; i < p.length; j = i++) {
      const [yi, xi] = p[i], [yj, xj] = p[j];
      if ((yi > lat) !== (yj > lat) && lon < (xj - xi) * (lat - yi) / (yj - yi) + xi) inside = !inside;
    }
    if (inside) return z;
  }
  return null;
}
