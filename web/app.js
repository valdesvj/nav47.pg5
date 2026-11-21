/* app.js - NAV47 on the web, the Modern tabs: the calculator's engines (web/api.c in nav47.js), the Route tab: a chart
 * (Mercator or globe) with the coastline, your objects (platforms, buoys, ...) and their keep-out zones, a route planned
 * round the zones, the plan, the comparison of the sailings, the voyage. The other tabs: tabs.js. */
"use strict";
const D2R = Math.PI / 180, R2D = 180 / Math.PI, NM = 1852;
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

/* ---------------------------------------------------------------- formats */
const FMT = {
  dm(v, w) {                                                  /* 47°43.7′ */
    let a = Math.abs(v), d = Math.floor(a), m = Math.round((a - d) * 600) / 10;
    if (m >= 60) { d++; m = 0; }
    return String(d).padStart(w || 0, "0") + "°" + m.toFixed(1).padStart(4, "0") + "′";
  },
  lat: v => FMT.dm(v, 2) + (v < 0 ? "S" : "N"),
  lon(v) { v = ((v + 540) % 360) - 180; return FMT.dm(v, 3) + (v < 0 ? "W" : "E"); },
  pos: (la, lo) => FMT.lat(la) + " " + FMT.lon(lo),
  crs: v => String(Math.round(((v % 360) + 360) % 360) % 360).padStart(3, "0") + "°",
  nm: m => (m / NM).toFixed(1),
  m: v => Math.abs(v) < 1000 ? Math.round(v) + " m" : (v / NM).toFixed(2) + " NM",
  hm(h) { if (!(h < 98)) return "--:--"; const m = Math.round(h * 60); return String(Math.floor(m / 60) % 24).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"); },
  dur(h) { const m = Math.max(0, Math.round(h * 60)), d = Math.floor(m / 1440); return (d ? d + "d " : "") + String(Math.floor(m / 60) % 24).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0"); },
  dt(jd, off) {                                               /* dd-mm hh:mm in UT + off hours */
    const t = new Date(Math.round(((jd - 2440587.5) * 864e5 + (off || 0) * 36e5) / 6e4) * 6e4);   /* to the minute */
    const p = n => String(n).padStart(2, "0");
    return p(t.getUTCDate()) + "-" + p(t.getUTCMonth() + 1) + " " + p(t.getUTCHours()) + ":" + p(t.getUTCMinutes());
  },
};
/* "27 12.0N 049 42.0E", "27°12.0'N 49°42.0'E", "27.2 49.7", "-33.92, 18.42": [lat, lon] or null */
function parsePos(s) {
  s = String(s).toUpperCase().replace(/[°'′"]/g, " ").replace(/,/g, " ");
  const re = /(-?\d+(?:\.\d+)?)(?:\s+(\d+(?:\.\d+)?))?\s*([NSEW])?/g;
  const v = [];
  let m;
  while ((m = re.exec(s)) && v.length < 2) {
    let x = parseFloat(m[1]);
    if (m[2] !== undefined) x = Math.sign(x || 1) * (Math.abs(x) + parseFloat(m[2]) / 60);
    if (m[3] === "S" || m[3] === "W") x = -Math.abs(x);
    v.push({ x, h: m[3] });
  }
  if (v.length < 2) return null;
  let [a, b] = v;
  if (a.h === "E" || a.h === "W") [a, b] = [b, a];
  if (Math.abs(a.x) > 90 || Math.abs(b.x) > 180) return null;
  return [a.x, b.x];
}
const jdOf = ms => ms / 864e5 + 2440587.5;
const msOf = jd => (jd - 2440587.5) * 864e5;

/* ---------------------------------------------------------------- the engines (web/api.c) */
const NAV = {
  o() { const p = Module._api_out() >> 3; return Module.HEAPF64.subarray(p, p + 8192); },
  str: p => Module.UTF8ToString(p),
  inv(a, b, c, d) { Module._api_inv(a, b, c, d); const o = NAV.o(); return { s: o[0], a1: o[1], a2: o[2] }; },
  dir(a, b, az, s) { Module._api_dir(a, b, az, s); const o = NAV.o(); return { lat: o[0], lon: o[1], az: o[2] }; },
  rinv(a, b, c, d) { Module._api_rinv(a, b, c, d); const o = NAV.o(); return { s: o[0], az: o[1] }; },
  rdir(a, b, az, s) { const rc = Module._api_rdir(a, b, az, s); const o = NAV.o(); return rc ? null : { lat: o[0], lon: o[1] }; },
  vertex(a, b, c, d) { const i = Module._api_vertex(a, b, c, d); const o = NAV.o(); return { lat: o[0], lon: o[1], s: o[2], inside: !!i }; },
  geodPts(a, b, c, d, n) {
    const k = Module._api_geod_pts(a, b, c, d, n), o = NAV.o(), r = [];
    for (let i = 0; i < k; i++) r.push([o[2 * i], o[2 * i + 1]]);
    return r;
  },
  plan(pts, sail, lim, split, val) {
    if (pts.length < 2) return { rows: [], rc: 0 };
    const p = Module._api_in() >> 3, IN = Module.HEAPF64.subarray(p, p + 4096);
    const n0 = Math.min(pts.length, 2000);
    for (let i = 0; i < n0; i++) { IN[2 * i] = pts[i].lat; IN[2 * i + 1] = pts[i].lon; }
    const n = Module._api_plan(n0, sail, lim, split, val), o = NAV.o(), rows = [];
    for (let i = 0; i < n; i++) {
      const q = o.subarray(10 * i, 10 * i + 10);
      rows.push({ lat: q[0], lon: q[1], leg: q[2], cum: q[3], crsIn: q[4], crsOut: q[5], rl: q[6], rlm: q[7],
                  kind: String.fromCharCode(q[8]), flags: q[9] });
    }
    return { rows, rc: o[8190], full: !!o[8191] };
  },
  ell(i) { const e = { name: NAV.str(Module._api_ell_name(i)), a: Module._api_ell_a(i), rf: Module._api_ell_rf(i) }; return e; },
  setEll(a, rf) { Module._api_ell(a, rf); },
};

/* ---------------------------------------------------------------- the state (localStorage) */
const OBJ = {
  platform: { label: "Platform", r: 500 }, rig: { label: "Rig", r: 500 }, wellhead: { label: "Wellhead", r: 500 },
  spm: { label: "SPM", r: 500 }, wreck: { label: "Wreck", r: 100 }, buoy: { label: "Buoy", r: 50 },
  light: { label: "Light / beacon", r: 50 }, port: { label: "Port / berth", r: 0 },
};
const SPLITS = [["none", "No split"], ["dist", "Every … NM"], ["legs", "Into … legs"], ["dlon", "On meridians every …°"]];
const store = {
  get(k, d) { try { const v = localStorage.getItem("nav47web:" + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem("nav47web:" + k, JSON.stringify(v)); } catch (e) { /* full */ } },
};
const S = {
  tab: store.get("tab", "route"),
  live: true, jd: jdOf(Date.now()),
  pos: store.get("pos", [26.5, 50.1667]),
  times: store.get("times", "ut") === "ut" ? "ut" : "zt",
  route: Object.assign({ wps: [], sail: 1, lim: 60, split: 0, val: 10, ell: 0, avoid: true, speed: 12, etd: null, margin: 50, clear: 50, len: 95 },
                       store.get("route", {})),
  objects: store.get("objects", []),
  view: store.get("view", { mode: "merc", lat0: 30, lon0: 10, k: 220 }),
  tool: "move", objType: "platform", sel: null, keepEta: store.get("keepEta", false), etaStep: store.get("etaStep", 0.5), pass: { from: "", to: "" }, showStreams: store.get("streams", false),
};
if (S.route.len === 100) S.route.len = 95;                       /* the old default: the Mermaid Asiana is 95 m */
const save = () => { store.set("route", S.route); store.set("objects", S.objects); store.set("view", S.view); store.set("pos", S.pos); store.set("times", S.times); };
const zd = () => S.times === "zt" ? Math.round(S.pos[1] / 15) : 0;       /* the times shown: UT or ZT (the DR's zone) */
const nowJd = () => S.live ? jdOf(Date.now()) : S.jd;
let uid = Date.now();
const newId = () => "o" + (uid++).toString(36);

/* ---------------------------------------------------------------- tide files: NOAA's currents (in the page) and yours
 * Your .TID stations and .FLW atlas (made by tools/gen_tides.py files) stay in this browser (localStorage), never sent
 * anywhere; the calculator's own parsers read them. They also go to the Calculator tab's disk (/NAV47/TIDES/). */
const TFILES = {
  KEY: "nav47web:tidefile:",
  call(fn, text, extra) {
    const n = Module.lengthBytesUTF8(text) + 1, p = Module._malloc(n);
    Module.stringToUTF8(text, p, n);
    const r = extra === undefined ? fn(p) : fn(p, extra);
    Module._free(p);
    return r;
  },
  names() { return Object.keys(localStorage).filter(k => k.startsWith(TFILES.KEY)).map(k => k.slice(TFILES.KEY.length)).sort(); },
  noaa: [],
  init() {
    /* the page's own public files: NOAA's current stations, TICON-3's tide gauges worldwide */
    TFILES.noaa = [];
    for (const dir of ["/noaa", "/ticon"]) {
      let fs = [];
      try { fs = Module.FS.readdir(dir).filter(f => /\.TID$/i.test(f)).sort(); } catch (e) { /* none */ }
      for (const f of fs) { TFILES.call(Module._api_tid_load, Module.FS.readFile(dir + "/" + f, { encoding: "utf8" }), 1); TFILES.noaa.push(f); }
    }
    TFILES.reload();
  },
  reload() {                                                 /* your files, again from the start */
    Module._api_tid_forget(); Module._api_flw_forget();
    const out = [];
    for (const nm of TFILES.names()) {
      const t = localStorage.getItem(TFILES.KEY + nm), flw = /^NAV47 FLOW/.test(t);
      const r = TFILES.call(flw ? Module._api_flw_load : Module._api_tid_load, t, flw ? undefined : 0);
      out.push({ name: nm, flw, n: r, err: r > 0 ? "" : NAV.str(Module._api_err()) });
    }
    TFILES.loaded = out;
    STREAMS.list = null;
    return out;
  },
  add(files, done) {
    Promise.all([...files].map(f => f.text().then(t => {
      t = t.replace(/\r/g, "");
      if (!/^NAV47 (TIDES|FLOW)/.test(t)) return `${f.name}: not a NAV47 .TID or .FLW file`;
      try { localStorage.setItem(TFILES.KEY + f.name.toUpperCase(), t); } catch (e) { return `${f.name}: too big for this browser's storage`; }
      return "";
    }))).then(errs => { TFILES.reload(); done(errs.filter(Boolean)); });
  },
  remove(nm) { localStorage.removeItem(TFILES.KEY + nm); TFILES.reload(); },
  /* the card: load, the list, forget (the Tide and Flow tabs) */
  card(redraw) {
    const L = TFILES.loaded || [];
    return `<div class="card"><h2>Your tide files <span class="meta">.TID stations, .FLW atlas</span></h2>
      <div class="controls"><label>Load <input type="file" class="tfload" accept=".tid,.TID,.flw,.FLW,.txt" multiple></label></div>
      <table>${L.map(f => `<tr><td class="mono">${esc(f.name)}</td><td>${f.flw ? "FLOW atlas" : "stations"}</td>
        <td class="num">${f.n > 0 ? f.n + (f.flw ? " sea cells" : " stations") : `<span style="color:var(--now)">${esc(f.err)}</span>`}</td>
        <td><button class="small tfx" data-n="${esc(f.name)}">forget</button></td></tr>`).join("") || `<tr><td class="meta">None loaded.</td></tr>`}</table>
      <div class="meta">Your own stations and FLOW atlas (Aramco, Admiralty ...): <span class="mono">make tidefiles TIDE47=~/opt/tide47</span>
        writes them to <span class="mono">build/tidefiles/</span> (as for the calculator's /NAV47/TIDES/). They stay in this browser,
        never sent anywhere and never in the repository; the Calculator tab gets them too (its FILES, TIDE, T&amp;F pages).
        The page itself has NOAA's current stations and TICON-3's tide gauges worldwide (${TFILES.noaa.length} files).</div></div>`;
  },
  bind(el, redraw) {
    el.querySelectorAll(".tfload").forEach(i => i.onchange = e => TFILES.add(e.target.files, errs => { if (errs.length) alert(errs.join("\n")); redraw(); }));
    el.querySelectorAll(".tfx").forEach(b => b.onclick = () => { TFILES.remove(b.dataset.n); redraw(); });
  },
};
/* the stream stations (NOAA's, yours): their positions, for the chart and the Flow tab */
const STREAMS = {
  list: null,
  all() {
    if (STREAMS.list) return STREAMS.list;
    const n = Module._api_tide_count(), out = [];
    for (let i = 0; i < n; i++) {
      Module._api_tide_info(i); const o = NAV.o();
      if (o[3] !== 0) out.push({ i, lat: o[0], lon: o[1], kind: o[3], name: NAV.str(Module._api_tide_name(i)), src: Module._api_tide_src(i) });
    }
    return (STREAMS.list = out);
  },
  now(i, jd) { if (!Module._api_stream_now(i, jd)) return null; const o = NAV.o(); return { kn: o[0], set: o[1] }; },
};
function arrowPath(g, x, y, set, len) {
  const a = set * D2R, x1 = x + len * Math.sin(a), y1 = y - len * Math.cos(a);
  g.beginPath(); g.moveTo(x - len * 0.3 * Math.sin(a), y + len * 0.3 * Math.cos(a)); g.lineTo(x1, y1);
  g.moveTo(x1, y1); g.lineTo(x1 - 6 * Math.sin(a - 0.45), y1 + 6 * Math.cos(a - 0.45));
  g.moveTo(x1, y1); g.lineTo(x1 - 6 * Math.sin(a + 0.45), y1 + 6 * Math.cos(a + 0.45)); g.stroke();
}
const rateCol = kn => kn < 0.5 ? "#4f8fc0" : kn < 1.5 ? "#2a9d6a" : kn < 3 ? "#d99a00" : "#c0392b";
/* the streams on the chart at the shown time: the stations, and the atlas's cells when the view is small */
function drawStreams() {
  if (!S.showStreams) return;
  const g = CH.g, J = nowJd(), span = spanDeg();
  g.save(); g.lineWidth = 2.2;
  if (span <= 40) for (const st of STREAMS.all()) {           /* the stations: on a regional chart, not the world */
    const [x, y, v] = proj(st.lat, st.lon);
    if (!v || x < -20 || y < -20 || x > CH.W + 20 || y > CH.H + 20) continue;
    const q = STREAMS.now(st.i, J); if (!q) continue;
    const kn = Math.abs(q.kn);
    g.strokeStyle = rateCol(kn); arrowPath(g, x, y, q.set, 10 + 7 * Math.min(kn, 4));
    if (span < 3) { g.fillStyle = css("--ink"); g.font = "11px system-ui"; g.fillText(`${kn.toFixed(1)} kn`, x + 8, y + 12); }
  }
  if (Module._api_flw_have() && span < 4) {
    const c = unproj(CH.W / 2, CH.H / 2);
    if (c && Module._api_flow(c[0], c[1], J, 0, 0, 0) !== undefined) {
      Module._api_flw_grid(); const o = NAV.o(), la0 = o[0], lo0 = o[1], st = o[2] / 60, nla = o[3], nlo = o[4];
      for (let a = 0; a < nla; a++) for (let b = 0; b < nlo; b++) {
        const la = la0 + (a + 0.5) * st, lo = lo0 + (b + 0.5) * st, [x, y, v] = proj(la, lo);
        if (!v || x < 0 || y < 0 || x > CH.W || y > CH.H) continue;
        if (!Module._api_flow_at(la, lo, -1)) continue;
        const q = NAV.o(), kn = q[1];
        g.strokeStyle = rateCol(kn); g.lineWidth = 1.6; arrowPath(g, x, y, q[0], 6 + 6 * Math.min(kn, 3));
      }
    }
  }
  g.restore();
}

/* ---------------------------------------------------------------- the coastline (COAST_C/L.BIN inside nav47.js) */
const COAST = {};
function loadCoast(which) {
  if (COAST[which]) return COAST[which];
  const d = Module.FS.readFile("/coast/COAST_" + which + ".BIN"), v = new DataView(d.buffer, d.byteOffset, d.byteLength);
  const n = v.getUint16(6, true), sc = v.getUint16(8, true) || 100, delta = v.getUint16(10, true) & 1, polys = [];
  for (let i = 0; i < n; i++) {
    const o = 16 + 16 * i, a = v.getInt16(o, true) / sc, b = v.getInt16(o + 2, true) / sc, c = v.getInt16(o + 4, true) / sc,
          e = v.getInt16(o + 6, true) / sc, off = v.getUint32(o + 8, true), np = v.getUint16(o + 12, true), lev = v.getUint8(o + 14);
    const lat = new Float32Array(np), lon = new Float32Array(np);
    let p = off, la = v.getInt16(p, true), lo = v.getInt16(p + 2, true);
    p += 4; lat[0] = la / sc; lon[0] = lo / sc;
    for (let j = 1; j < np; j++) {
      if (!delta) { la = v.getInt16(p, true); lo = v.getInt16(p + 2, true); p += 4; }
      else if (d[p] === 0x80) { la = v.getInt16(p + 2, true); lo = v.getInt16(p + 4, true); p += 6; }
      else { la += v.getInt8(p); lo += v.getInt8(p + 1); p += 2; }
      lat[j] = la / sc; lon[j] = lo / sc;
    }
    polys.push({ s: a, n: b, w: c, e, lat, lon, lev });
  }
  return (COAST[which] = polys);
}

/* ---------------------------------------------------------------- planning round the zones
 * A leg is planned on the azimuthal equidistant plane centred on its middle (made with the geodesic engine: distances
 * from the centre exact, within a metre at 100 km). Each zone becomes a polygon a little outside its circle; the
 * shortest way is found among the polygons' corners (visibility graph, Dijkstra), from every start to every end
 * (a leg from or to an object starts or ends on a circle its zone + the margin out, the side found by the search),
 * then pulled tight. The legs are checked afterwards on the real sailing (rhumb or geodesic) with exact distances. */
const AV = { M: 0.015, NV: 16, NC: 48 };
function aePlane(lat0, lon0) {
  return {
    fwd(la, lo) { const r = NAV.inv(lat0, lon0, la, lo), a = r.a1 * D2R; return [r.s * Math.sin(a), r.s * Math.cos(a)]; },
    inv(x, y) { const s = Math.hypot(x, y); if (s < 1e-6) return [lat0, lon0]; const r = NAV.dir(lat0, lon0, Math.atan2(x, y) * R2D, s); return [r.lat, r.lon]; },
  };
}
function segDist(px, py, ax, ay, bx, by) {
  const dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
  let t = l2 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
  t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - ax - t * dx, py - ay - t * dy);
}
/* starts or ends: a point, or a circle round an object */
function ends(w) {
  const o = w.obj && S.objects.find(x => x.id === w.obj);
  if (!o || !S.route.avoid) return { pts: [{ lat: w.lat, lon: w.lon }], obj: null };
  const R0 = o.r + S.route.margin;
  if (R0 <= 0) return { pts: [{ lat: o.lat, lon: o.lon }], obj: o, R: 0 };
  /* the circle round it; when other zones cover all of it, a wider one (100 m steps, up to 5 km) */
  const others = S.objects.filter(z => z !== o && z.r > 0 && NAV.inv(o.lat, o.lon, z.lat, z.lon).s < z.r + R0 + 5100);
  for (let R = R0; R <= R0 + 5000; R += 100) {
    const pts = [];
    for (let i = 0; i < AV.NC; i++) {
      const p = NAV.dir(o.lat, o.lon, i * 360 / AV.NC, R);
      if (!others.some(z => NAV.inv(p.lat, p.lon, z.lat, z.lon).s < z.r + 1)) pts.push({ lat: p.lat, lon: p.lon });
    }
    if (pts.length) return { pts, obj: o, R, wide: R > R0 };
  }
  return { pts: [{ lat: w.lat, lon: w.lon }], obj: o, R: 0 };
}
function avoidLeg(As, Bs, zones, clr) {
  const a0 = As.pts[0], b0 = Bs.pts[0];
  const ab = NAV.inv(a0.lat, a0.lon, b0.lat, b0.lon), mid = NAV.dir(a0.lat, a0.lon, ab.a1, ab.s / 2);
  const ae = aePlane(mid.lat, mid.lon), L = ab.s;
  const near = [];
  for (const z of zones) {
    const d = NAV.inv(mid.lat, mid.lon, z.lat, z.lon).s;
    if (d < L / 2 + z.r + 8000) {
      /* the leg's own objects (it starts or ends on their circles): the zone itself; the others: zone + clearance */
      const own = z === As.obj || z === Bs.obj, [x, y] = ae.fwd(z.lat, z.lon);
      near.push({ z, x, y, r: (z.r + (own ? 0 : clr)) * (1 + AV.M) + 1 });
    }
  }
  const inside = (x, y, set) => set.some(z => (x - z.x) ** 2 + (y - z.y) ** 2 < z.r * z.r - 1e-6);
  const clear = (ax, ay, bx, by, set) => { for (const z of set) if (segDist(z.x, z.y, ax, ay, bx, by) < z.r - 1e-6) return false; return true; };
  const raw = near.map(z => ({ x: z.x, y: z.y, r: z.z.r }));     /* the ends: against the zones themselves */
  const A = As.pts.map(p => ae.fwd(p.lat, p.lon)).filter(([x, y]) => !inside(x, y, raw));
  const B = Bs.pts.map(p => ae.fwd(p.lat, p.lon)).filter(([x, y]) => !inside(x, y, raw));
  if (!A.length || !B.length) return { ok: false, why: "the start or the end is inside a zone" };
  const active = new Set();
  let path = null;
  for (let it = 0; it < 30; it++) {
    const act = near.filter(z => active.has(z));
    const nodes = [];
    A.forEach(p => nodes.push({ x: p[0], y: p[1], t: 1 }));
    B.forEach(p => nodes.push({ x: p[0], y: p[1], t: 2 }));
    const rv = 1 / Math.cos(Math.PI / AV.NV) * (1 + 0.004);
    for (const z of act)
      for (let i = 0; i < AV.NV; i++) {
        const a = (i + 0.5) * 2 * Math.PI / AV.NV, x = z.x + z.r * rv * Math.sin(a), y = z.y + z.r * rv * Math.cos(a);
        if (!inside(x, y, near)) nodes.push({ x, y, t: 0 });
      }
    /* Dijkstra from all the starts (distance 0) to the first end reached */
    const n = nodes.length, dist = new Float64Array(n).fill(Infinity), prev = new Int32Array(n).fill(-1), done = new Uint8Array(n);
    nodes.forEach((p, i) => { if (p.t === 1) dist[i] = 0; });
    let end = -1;
    for (;;) {
      let u = -1, best = Infinity;
      for (let i = 0; i < n; i++) if (!done[i] && dist[i] < best) { best = dist[i]; u = i; }
      if (u < 0) break;
      done[u] = 1;
      if (nodes[u].t === 2) { end = u; break; }
      const pu = nodes[u];
      for (let v = 0; v < n; v++) {
        if (done[v] || nodes[v].t === 1) continue;
        const pv = nodes[v], d = best + Math.hypot(pv.x - pu.x, pv.y - pu.y);
        if (d < dist[v] && clear(pu.x, pu.y, pv.x, pv.y, act)) { dist[v] = d; prev[v] = u; }
      }
    }
    if (end < 0) {                                          /* boxed in by zones not yet in the search: all of them in */
      if (active.size < near.length) { near.forEach(z => active.add(z)); continue; }
      return { ok: false, why: "no way round the zones" };
    }
    path = [];
    for (let i = end; i >= 0; i = prev[i]) path.unshift(nodes[i]);
    let added = false;                                      /* zones the path crosses that were not in the search */
    for (let i = 1; i < path.length; i++)
      for (const z of near)
        if (!active.has(z) && segDist(z.x, z.y, path[i - 1].x, path[i - 1].y, path[i].x, path[i].y) < z.r - 1e-6) { active.add(z); added = true; }
    if (!added) break;
  }
  /* pulled tight: from each point straight to the farthest one in clear sight */
  const tight = [path[0]];
  for (let i = 0; i < path.length - 1;) {
    let j = path.length - 1;
    while (j > i + 1 && !clear(path[i].x, path[i].y, path[j].x, path[j].y, near)) j--;
    tight.push(path[j]); i = j;
  }
  return { ok: true, pts: tight.map(p => { const [la, lo] = ae.inv(p.x, p.y); return { lat: la, lon: lo, t: p.t }; }) };
}
/* points along a leg as it is sailed: the rhumb line or the geodesic */
function sailPts(a, b, n, rhumb) {
  if (!rhumb) return NAV.geodPts(a.lat, a.lon, b.lat, b.lon, n);
  const r = NAV.rinv(a.lat, a.lon, b.lat, b.lon), out = [];
  for (let i = 0; i <= n; i++) { const p = NAV.rdir(a.lat, a.lon, r.az, r.s * i / n); out.push(p ? [p.lat, p.lon] : [b.lat, b.lon]); }
  return out;
}
/* the route: the waypoints round the zones, the plan, the check */
let ROUTE = { pts: [], plan: { rows: [] }, check: null };
function compute() {
  const R = S.route, wps = R.wps, zones = S.objects.filter(o => o.r > 0);
  const e = NAV.ell(R.ell); NAV.setEll(e.a, e.rf);
  const pts = [], warn = [];
  if (wps.length >= 2) {
    let from = ends(wps[0]);
    for (let i = 1; i < wps.length; i++) {
      const to = ends(wps[i]);
      let leg;
      if (R.avoid && zones.length) {
        /* the leg's own objects are zones too, except that its ends sit on their circles */
        if (to.wide) warn.push(`${esc(to.obj.name)}: its ${R.margin} m ring is inside other zones; the route ends ${Math.round(to.R - to.obj.r)} m outside its zone.`);
        if (i === 1 && from.wide) warn.push(`${esc(from.obj.name)}: its ${R.margin} m ring is inside other zones; the route starts ${Math.round(from.R - from.obj.r)} m outside its zone.`);
        for (const clr of [R.clear, R.clear / 2, 0]) {
          leg = avoidLeg(from, to, zones, clr);
          if (leg.ok) { if (clr < R.clear) warn.push(`Leg ${i}: no way with ${R.clear} m clearance; planned with ${clr} m.`); break; }
          if (clr === 0) break;
        }
        if (!leg.ok) { warn.push(`Leg ${i}: ${leg.why}; sailed straight.`); leg = { pts: [from.pts[0], to.pts[0]] }; }
      } else leg = { pts: [from.pts[0], to.pts[0]] };
      leg.pts.forEach((p, k) => {
        if (k === 0 && pts.length) return;                   /* the joint: already there */
        const w = k === 0 ? wps[i - 1] : k === leg.pts.length - 1 ? wps[i] : null;
        pts.push({ lat: p.lat, lon: p.lon, name: w ? w.name : "", user: !!w, obj: w && w.obj,
                   off: w && w.obj && R.avoid ? (k === 0 ? "dep" : "arr") : "" });
      });
      from = { pts: [pts[pts.length - 1]], obj: to.obj, R: to.R };   /* the next leg starts where this one ended */
    }
  }
  const plan = NAV.plan(pts, R.sail, R.lim, [0, 1, 2, 3][R.split], R.val);
  ROUTE = { pts, plan, check: check(plan.rows, zones, warn) };
}
/* land: does a leg as sailed cross the coastline (Natural Earth 50 m, its land polygons)? The pieces of the leg, every
   ~2 km, against the polygons' edges; the first and last 3 km of the route and 3 km round a port are left out
   (a route starts and ends in harbour). Returns the crossings [lat, lon]. */
function segX(ax, ay, bx, by, cx, cy, dx, dy) {
  const d = (bx - ax) * (dy - cy) - (by - ay) * (dx - cx);
  if (Math.abs(d) < 1e-15) return null;
  const t = ((cx - ax) * (dy - cy) - (cy - ay) * (dx - cx)) / d, u = ((cx - ax) * (by - ay) - (cy - ay) * (bx - ax)) / d;
  return t >= 0 && t <= 1 && u >= 0 && u <= 1 ? t : null;
}
function landCrossings(P, exempt) {
  const polys = loadCoast("L"), out = [];
  for (let i = 1; i < P.length; i++) {
    let [a1, o1] = P[i - 1], [a2, o2] = P[i];
    o2 = nearLon(o2, o1);
    const s = Math.min(a1, a2), n = Math.max(a1, a2), w = Math.min(o1, o2), e = Math.max(o1, o2);
    for (const Q of polys) {
      if (Q.lev !== 1 || Q.n < s || Q.s > n) continue;
      for (const off of [-360, 0, 360]) {
        if (Q.e + off < w || Q.w + off > e) continue;
        const la = Q.lat, lo = Q.lon;
        for (let j = 1; j < la.length; j++) {
          const x1 = lo[j - 1] + off, y1 = la[j - 1], x2 = lo[j] + off, y2 = la[j];
          if (Math.max(x1, x2) < w || Math.min(x1, x2) > e || Math.max(y1, y2) < s || Math.min(y1, y2) > n) continue;
          const t = segX(o1, a1, o2, a2, x1, y1, x2, y2);
          if (t === null) continue;
          const p = [a1 + (a2 - a1) * t, nearLon(o1 + (o2 - o1) * t, 0)];
          if (!exempt(p)) out.push(p);
        }
      }
    }
  }
  return out;
}
/* every leg of the plan, sampled every ~25 m (or 400 a leg), against every zone near it: exact distances; and land */
function check(rows, zones, warn) {
  const out = { min: Infinity, minObj: null, minLeg: 0, hits: [], warn: warn.slice(), land: [] };
  if (rows.length < 2) return out;
  /* land: for a vessel; a flight (100 kn or more) passes over it */
  out.flight = S.route.speed >= 100;
  const ports = S.objects.filter(o => o.type === "port"), first = rows[0], last = rows[rows.length - 1];
  const exempt = p => NAV.inv(p[0], p[1], first.lat, first.lon).s < 3000 || NAV.inv(p[0], p[1], last.lat, last.lon).s < 3000 ||
    ports.some(o => NAV.inv(p[0], p[1], o.lat, o.lon).s < 3000);
  for (let i = 1; i < rows.length && !out.flight; i++) {
    const a = rows[i - 1], b = rows[i], k = rows[i].kind !== "G" && (S.route.sail === 0 || rows[i].kind === "P" || rows[i].kind === "R");
    const n = Math.max(2, Math.min(600, Math.ceil(rows[i].leg / 2000)));
    const X = landCrossings(sailPts(a, b, n, k), exempt);
    if (X.length) out.land.push({ leg: i, at: X[0], n: X.length });
  }
  if (!zones.length) return out;
  const rhumb = S.route.sail === 0;
  for (let i = 1; i < rows.length; i++) {
    const a = rows[i - 1], b = rows[i], len = rows[i].leg;
    const k = rows[i].kind === "G" ? false : rhumb || rows[i].kind === "P" || rows[i].kind === "R";
    const mid = NAV.inv(a.lat, a.lon, b.lat, b.lon), m = NAV.dir(a.lat, a.lon, mid.a1, mid.s / 2);
    const cand = zones.filter(z => NAV.inv(m.lat, m.lon, z.lat, z.lon).s < len / 2 + z.r + 3000);
    if (!cand.length) continue;
    const n = Math.max(2, Math.min(400, Math.ceil(len / 25)));
    const P = sailPts(a, b, n, k);
    for (const z of cand) {
      let best = Infinity;
      for (const [la, lo] of P) { const d = NAV.inv(la, lo, z.lat, z.lon).s; if (d < best) best = d; }
      const c = best - z.r;
      if (c < out.min) { out.min = c; out.minObj = z; out.minLeg = i; }
      if (c < -0.5) out.hits.push({ leg: i, obj: z, by: -c });
    }
  }
  return out;
}

/* ---------------------------------------------------------------- the chart */
const CH = { cv: null, g: null, W: 0, H: 0, dpr: 1, play: null, t: 0 };
const E_WGS = Math.sqrt(1 / 298.257223563 * (2 - 1 / 298.257223563));
const psi = la => { const s = Math.sin(Math.max(-85, Math.min(85, la)) * D2R); return Math.atanh(s) - E_WGS * Math.atanh(E_WGS * s); };
function latOfPsi(p) { let f = 2 * Math.atan(Math.exp(p)) - Math.PI / 2; for (let i = 0; i < 4; i++) f = Math.asin(Math.tanh(p + E_WGS * Math.atanh(E_WGS * Math.sin(f)))); return f * R2D; }
const nearLon = (lo, ref) => { while (lo - ref > 180) lo -= 360; while (lo - ref < -180) lo += 360; return lo; };
function proj(la, lo) {
  const V = S.view, cx = CH.W / 2, cy = CH.H / 2;
  if (V.mode === "merc") return [cx + V.k * (nearLon(lo, V.lon0) - V.lon0) * D2R, cy - V.k * (psi(la) - psi(V.lat0)), true];
  const p = la * D2R, d = (lo - V.lon0) * D2R, p0 = V.lat0 * D2R;
  const sp = Math.sin(p), cp = Math.cos(p), cd = Math.cos(d);
  return [cx + V.k * cp * Math.sin(d), cy - V.k * (Math.cos(p0) * sp - Math.sin(p0) * cp * cd), Math.sin(p0) * sp + Math.cos(p0) * cp * cd >= 0];
}
function unproj(x, y) {
  const V = S.view, cx = CH.W / 2, cy = CH.H / 2;
  if (V.mode === "merc") return [latOfPsi(psi(V.lat0) + (cy - y) / V.k), nearLon(V.lon0 + (x - cx) / V.k * R2D, 0)];
  const X = (x - cx) / V.k, Y = (cy - y) / V.k, r = Math.hypot(X, Y);
  if (r > 1) return null;
  const c = Math.asin(r), p0 = V.lat0 * D2R;
  const la = r < 1e-12 ? p0 : Math.asin(Math.cos(c) * Math.sin(p0) + Y * Math.sin(c) * Math.cos(p0) / r);
  const lo = V.lon0 * D2R + Math.atan2(X * Math.sin(c), r * Math.cos(c) * Math.cos(p0) - Y * Math.sin(c) * Math.sin(p0));
  return [la * R2D, nearLon(lo * R2D, 0)];
}
function clampView() {
  const V = S.view, kmin = V.mode === "merc" ? CH.W / (2 * Math.PI) : Math.min(CH.W, CH.H) / 2 - 10;
  V.k = Math.max(kmin, Math.min(V.k, 6e8));
  V.lat0 = Math.max(V.mode === "merc" ? -82 : -90, Math.min(V.mode === "merc" ? 82 : 90, V.lat0));
  V.lon0 = nearLon(V.lon0, 0);
}
const spanDeg = () => S.view.mode === "merc" ? CH.W / S.view.k * R2D : 2 * Math.asin(Math.min(1, CH.W / 2 / S.view.k)) * R2D;
const pxPerM = la => { const V = S.view, s = Math.sin(la * D2R), N = 6378137 / Math.sqrt(1 - E_WGS * E_WGS * s * s);
  return V.mode === "merc" ? V.k / (N * Math.cos(la * D2R)) : V.k / 6371000; };
const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();

function fit(list) {
  if (!list.length) list = [{ lat: S.pos[0], lon: S.pos[1] }];
  const V = S.view;
  let lo0 = list[0].lon, s = 90, n = -90, w = 1e9, e = -1e9;
  for (const p of list) { const lo = nearLon(p.lon, lo0); s = Math.min(s, p.lat); n = Math.max(n, p.lat); w = Math.min(w, lo); e = Math.max(e, lo); }
  const ml = (n - s) * 0.12 + 0.01, mo = (e - w) * 0.12 + 0.01;
  s -= ml; n += ml; w -= mo; e += mo;
  if (V.mode === "merc") {
    V.lat0 = latOfPsi((psi(s) + psi(n)) / 2); V.lon0 = (w + e) / 2;
    V.k = Math.min(CH.W / Math.max(1e-5, (e - w) * D2R), CH.H / Math.max(1e-7, psi(n) - psi(s)));
  } else {
    let x = 0, y = 0, z = 0;
    for (const p of list) { const a = p.lat * D2R, b = p.lon * D2R; x += Math.cos(a) * Math.cos(b); y += Math.cos(a) * Math.sin(b); z += Math.sin(a); }
    V.lat0 = Math.atan2(z, Math.hypot(x, y)) * R2D; V.lon0 = Math.atan2(y, x) * R2D;
    let k = 6e8;
    for (const p of list) {
      const P = p.lat * D2R, d = (p.lon - V.lon0) * D2R, p0 = V.lat0 * D2R;
      const vis = Math.sin(p0) * Math.sin(P) + Math.cos(p0) * Math.cos(P) * Math.cos(d);
      if (vis < 0.17) { k = 0; break; }
      const px = Math.abs(Math.cos(P) * Math.sin(d)), py = Math.abs(Math.cos(p0) * Math.sin(P) - Math.sin(p0) * Math.cos(P) * Math.cos(d));
      if (px > 1e-9) k = Math.min(k, (CH.W / 2 - 30) / px);
      if (py > 1e-9) k = Math.min(k, (CH.H / 2 - 30) / py);
    }
    V.k = k;
  }
  clampView();
}
function fitRoute() {
  const l = ROUTE.plan.rows.length ? ROUTE.plan.rows.slice() : S.route.wps.slice();
  for (let i = 1; i < S.route.wps.length; i++) {          /* the geodesics' vertices: they bulge */
    const a = S.route.wps[i - 1], b = S.route.wps[i], v = NAV.vertex(a.lat, a.lon, b.lat, b.lon);
    if (v.inside) l.push(v);
  }
  if (l.length < 2 && S.objects.length) S.objects.forEach(o => l.push(o));
  fit(l);
}

/* drawing helpers */
function polyline(pts, style) {
  const g = CH.g;
  g.beginPath();
  let pen = false, px = 0, py = 0;
  for (const [la, lo] of pts) {
    const [x, y, v] = proj(la, lo);
    if (!v || (pen && Math.abs(x - px) > CH.W * 0.7)) { pen = false; if (!v) continue; }
    if (!pen) { g.moveTo(x, y); pen = true; } else g.lineTo(x, y);
    px = x; py = y;
  }
  if (style) style(g);
  g.stroke();
}
function geodLine(a, b) {
  const [x1, y1] = proj(a.lat, a.lon), [x2, y2] = proj(b.lat, b.lon);
  const L = NAV.inv(a.lat, a.lon, b.lat, b.lon).s, n = Math.max(2, Math.min(256, Math.ceil(Math.hypot(x2 - x1, y2 - y1) / 6) + Math.ceil(L / 2e5)));
  return NAV.geodPts(a.lat, a.lon, b.lat, b.lon, n);
}
function rhumbLine(a, b) {
  if (S.view.mode === "merc") { const lo2 = nearLon(b.lon, a.lon); return [[a.lat, a.lon], [b.lat, lo2]]; }
  const out = [], lo2 = nearLon(b.lon, a.lon), p1 = psi(a.lat), p2 = psi(b.lat), n = 64;
  for (let i = 0; i <= n; i++) { const t = i / n; out.push([a.lat === b.lat ? a.lat : latOfPsi(p1 + (p2 - p1) * t), a.lon + (lo2 - a.lon) * t]); }
  return out;
}
function circlePts(o, R) { const out = []; for (let i = 0; i <= 64; i++) { const p = NAV.dir(o.lat, o.lon, i * 360 / 64, R); out.push([p.lat, p.lon]); } return out; }

function drawCoast() {
  const g = CH.g, V = S.view, span = spanDeg(), which = span < 25 ? "L" : "C", polys = loadCoast(which);
  const land = css("--land"), coast = css("--coast"), sea = css("--sea");
  const [s, n] = V.mode === "merc" ? [unproj(0, CH.H)[0], unproj(0, 0)[0]] : [-90, 90];
  const lw = V.lon0 - span / 2 - 1, le = V.lon0 + span / 2 + 1;
  g.lineWidth = 0.8; g.strokeStyle = coast;
  for (const P of polys) {
    if (V.mode === "merc") {
      if (P.n < s || P.s > n) continue;
      for (const off of [-360, 0, 360]) {
        if (P.e + off < lw || P.w + off > le) continue;
        g.beginPath();
        for (let j = 0; j < P.lat.length; j++) {
          const x = CH.W / 2 + V.k * (P.lon[j] + off - V.lon0) * D2R, y = CH.H / 2 - V.k * (psi(P.lat[j]) - psi(V.lat0));
          j ? g.lineTo(x, y) : g.moveTo(x, y);
        }
        g.closePath(); g.fillStyle = P.lev === 2 ? sea : land; g.fill(); g.stroke();
      }
    } else {
      let all = true;
      const xs = [];
      for (let j = 0; j < P.lat.length; j++) { const q = proj(P.lat[j], P.lon[j]); xs.push(q); if (!q[2]) all = false; }
      g.beginPath();
      let pen = false;
      for (const [x, y, v] of xs) { if (!v) { pen = false; continue; } if (!pen) { g.moveTo(x, y); pen = true; } else g.lineTo(x, y); }
      if (all) { g.closePath(); g.fillStyle = P.lev === 2 ? sea : land; g.fill(); }
      g.stroke();
    }
  }
  return which;
}
function drawGrid() {
  const g = CH.g, V = S.view, span = spanDeg();
  const step = [0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30].find(s => span / s <= 9) || 30;
  g.strokeStyle = css("--grid"); g.lineWidth = 1; g.fillStyle = css("--soft"); g.font = "11px system-ui";
  const lw = V.mode === "merc" ? V.lon0 - span / 2 : -180, le = V.mode === "merc" ? V.lon0 + span / 2 : 180;
  let ls = -85, ln = 85;
  if (V.mode === "merc") { ls = Math.max(-85, unproj(0, CH.H)[0]); ln = Math.min(85, unproj(0, 0)[0]); }
  else if (span < 60) { ls = Math.max(-90, V.lat0 - span); ln = Math.min(90, V.lat0 + span); }
  const glo = V.mode === "globe" && span < 60 ? [V.lon0 - span / Math.max(0.1, Math.cos(Math.min(85, Math.abs(V.lat0) + span / 2) * D2R)), 0] : null;
  const lw2 = glo ? glo[0] : lw, le2 = glo ? 2 * V.lon0 - glo[0] : le;
  for (let lo = Math.ceil(lw2 / step) * step; lo <= le2 + 1e-9; lo += step) {
    const pts = []; for (let la = ls; la <= ln + 1e-9; la += Math.max((ln - ls) / 120, Math.min(step, 2.5))) pts.push([la, lo]);
    polyline(pts);
    if (V.mode === "merc") { const [x] = proj(0, lo); g.fillText(fmtDeg(lo, "EW"), x + 3, 13); }
  }
  for (let la = Math.ceil(ls / step) * step; la <= ln + 1e-9; la += step) {
    const pts = []; for (let lo = lw2; lo <= le2 + 1e-9; lo += Math.min(step, (le2 - lw2) / 90)) pts.push([la, lo]);
    polyline(pts);
    if (V.mode === "merc") { const [, y] = proj(la, V.lon0); if (y > 20 && y < CH.H - 4) g.fillText(fmtDeg(la, "NS"), 4, y - 3); }
  }
  return step;
}
function fmtDeg(v, h) {
  if (h === "EW") v = nearLon(v, 0);
  const a = Math.abs(v), d = Math.floor(a + 1e-9), m = Math.round((a - d) * 60);
  return d + "°" + (m ? String(m).padStart(2, "0") + "′" : "") + (v === 0 ? "" : v < 0 ? h[1] : h[0]);
}
function drawObj(o, sel) {
  const g = CH.g, [x, y, v] = proj(o.lat, o.lon);
  if (!v) return;
  const rp = o.r * pxPerM(o.lat);
  if (o.r > 0 && rp > 1.5) {                                 /* the zone */
    g.beginPath();
    if (S.view.mode === "merc") g.arc(x, y, rp, 0, 2 * Math.PI); else circlePts(o, o.r).forEach(([la, lo], i) => { const [a, b] = proj(la, lo); i ? g.lineTo(a, b) : g.moveTo(a, b); });
    g.fillStyle = css("--zone"); g.fill();
    g.strokeStyle = css("--zonel"); g.lineWidth = 1; g.setLineDash([4, 3]); g.stroke(); g.setLineDash([]);
  }
  const ink = css("--ink");
  g.lineWidth = 1.6; g.strokeStyle = ink; g.fillStyle = ink;
  g.beginPath();
  switch (o.type) {
    case "platform": g.rect(x - 5, y - 5, 10, 10); g.moveTo(x - 5, y - 5); g.lineTo(x + 5, y + 5); g.moveTo(x + 5, y - 5); g.lineTo(x - 5, y + 5); g.stroke(); break;
    case "rig": g.moveTo(x, y - 7); g.lineTo(x + 6, y + 5); g.lineTo(x - 6, y + 5); g.closePath(); g.stroke(); g.beginPath(); g.moveTo(x, y - 7); g.lineTo(x, y + 5); g.stroke(); break;
    case "wellhead": g.arc(x, y, 4, 0, 2 * Math.PI); g.moveTo(x - 7, y); g.lineTo(x + 7, y); g.moveTo(x, y - 7); g.lineTo(x, y + 7); g.stroke(); break;
    case "spm": g.arc(x, y, 6, 0, 2 * Math.PI); g.stroke(); g.beginPath(); g.arc(x, y, 2, 0, 2 * Math.PI); g.fill(); break;
    case "wreck": g.moveTo(x - 8, y); g.lineTo(x + 8, y); g.moveTo(x, y - 4); g.lineTo(x, y + 4);
      g.moveTo(x - 4, y - 3); g.lineTo(x - 4, y + 3); g.moveTo(x + 4, y - 3); g.lineTo(x + 4, y + 3); g.stroke(); break;
    case "buoy": g.moveTo(x, y - 6); g.lineTo(x + 4, y + 3); g.lineTo(x - 4, y + 3); g.closePath(); g.fillStyle = "#d9a400"; g.fill(); g.stroke();
      g.beginPath(); g.moveTo(x - 6, y + 4); g.lineTo(x + 6, y + 4); g.stroke(); break;
    case "light": g.fillStyle = "#c9840a";
      for (let i = 0; i < 10; i++) { const a = i * Math.PI / 5 - Math.PI / 2, r = i % 2 ? 2.5 : 7; i ? g.lineTo(x + r * Math.cos(a), y + r * Math.sin(a)) : g.moveTo(x + r * Math.cos(a), y + r * Math.sin(a)); }
      g.closePath(); g.fill(); g.stroke(); break;
    default: g.font = "15px system-ui"; g.textAlign = "center"; g.fillText("⚓", x, y + 5); g.textAlign = "left";
  }
  if (sel) { g.beginPath(); g.arc(x, y, 11, 0, 2 * Math.PI); g.strokeStyle = css("--accent"); g.lineWidth = 2; g.stroke(); }
  if (rp > 6 || spanDeg() < 1.5) { g.font = "12px system-ui"; g.fillStyle = ink; g.fillText(o.name, x + 9, y - 7); }
}
/* the ship (the Mermaid Asiana, as on the calculator) and the plane */
const SHIP = [0x0000004000, 0x000000E000, 0x0000004000, 0x000000E000, 0x0000004000, 0x000001F000, 0x000003FFFF, 0x000002AB02,
              0x00FFE3FE84, 0x007FDFFF48, 0x0060BFFFB0, 0x00607FFFFC, 0x00F1FFFFFC, 0x1FFFFFFFFC, 0x1FFFFFFFF8, 0x1FFFFFFFF0];
/* the vessel on the chart, as on an ECDIS: seen from above, turned to the course; at true scale when that is big
   enough (the Mermaid Asiana 95 m x 22 m, the plane 64 m long, 60 m span), else a symbol of a fixed size; a course
   vector ahead (6 minutes' run, at least 40 px). Its side profile (the photograph) is the HUD's badge. */
const VESSEL = { len: 95, beam: 22, planeLen: 64, minPx: 60 };
function drawVessel(la, lo, crs, plane, stopped) {
  const g = CH.g, [x, y, v] = proj(la, lo);
  if (!v) return;
  const [ax, ay] = proj(la + 0.001 * Math.cos(crs * D2R), lo + 0.001 * Math.sin(crs * D2R) / Math.max(0.01, Math.cos(la * D2R)));
  let fx = ax - x, fy = ay - y; const l = Math.hypot(fx, fy) || 1; fx /= l; fy /= l;   /* forward on the LCD */
  const ppm = pxPerM(la), L = plane ? VESSEL.planeLen : (S.route.len || VESSEL.len);
  const scale = Math.max(ppm, VESSEL.minPx / L);                   /* px per metre of the drawing */
  VOY.trueScale = ppm >= VESSEL.minPx / L;
  /* the course vector: 6 minutes' run */
  const run = S.route.speed * NM / 10 * ppm, vl = Math.max(40, run) + L / 2 * scale;
  g.save();
  g.strokeStyle = css("--now"); g.lineWidth = 2;
  g.beginPath(); g.moveTo(x + fx * L / 2 * scale, y + fy * L / 2 * scale); g.lineTo(x + fx * vl, y + fy * vl); g.stroke();
  g.beginPath(); g.moveTo(x + fx * vl, y + fy * vl); g.lineTo(x + fx * (vl - 7) - fy * 4, y + fy * (vl - 7) + fx * 4);
  g.lineTo(x + fx * (vl - 7) + fy * 4, y + fy * (vl - 7) - fx * 4); g.closePath(); g.fillStyle = css("--now"); g.fill();
  if (stopped && !plane) {                                         /* at the start or the arrival: its side, the bow to the course */
    g.setTransform(CH.dpr, 0, 0, CH.dpr, 0, 0);
    const W = 37, Hh = 16, px = Math.max(2.4, L * scale / W), flip = fx < 0;
    const on = (r, k) => Math.floor(SHIP[r] / 2 ** (W - 1 - k)) % 2;
    const col = (r, k) => r >= 11 || (r >= 10 && k >= 29) || (r === 9 && k >= 31) ? "#d63a2a"
      : r <= 6 || (r === 7 && k >= 27) ? "#5b6470" : r === 9 && k >= 15 && k <= 18 ? "#f08a24" : "#f4f4f2";
    const X = k => x + ((flip ? W - 1 - k : k) - W / 2) * px, Y = r => y + (r - Hh + 3) * px;
    g.fillStyle = "#1b2129";
    for (let r = 0; r < Hh; r++) for (let k = 0; k < W; k++) if (on(r, k)) g.fillRect(X(k) - 1, Y(r) - 1, px + 2, px + 2);
    for (let r = 0; r < Hh; r++) for (let k = 0; k < W; k++) if (on(r, k)) { g.fillStyle = col(r, k); g.fillRect(X(k), Y(r), px + 0.3, px + 0.3); }
    g.restore(); return;
  }
  /* the vessel's own frame: +X forward, +Y to starboard, metres */
  g.setTransform(CH.dpr * fx * scale, CH.dpr * fy * scale, -CH.dpr * fy * scale, CH.dpr * fx * scale, CH.dpr * x, CH.dpr * y);
  const lw = 1.2 / scale;
  if (plane) {
    const k = 3.2;                                                  /* the path is in units of ~3.2 m */
    g.beginPath();
    [[10, 0], [6, 1.4], [2, 1.4], [-3, 10], [-5, 10], [-2.5, 1.4], [-6.5, 1.2], [-8.5, 4], [-10, 4], [-9, 0]]
      .forEach(([X, Y], i) => i ? g.lineTo(X * k, Y * k) : g.moveTo(X * k, Y * k));
    [[-10, -4], [-8.5, -4], [-6.5, -1.2], [-2.5, -1.4], [-5, -10], [-3, -10], [2, -1.4], [6, -1.4]].forEach(([X, Y]) => g.lineTo(X * k, Y * k));
    g.closePath(); g.fillStyle = css("--ink"); g.fill(); g.strokeStyle = css("--card"); g.lineWidth = lw; g.stroke();
  } else {
    const H = L / 2, B = VESSEL.beam / 2 * L / VESSEL.len;
    /* the hull: a square stern, straight sides, a pointed bow */
    g.beginPath(); g.moveTo(-H, -B); g.lineTo(H * 0.45, -B); g.quadraticCurveTo(H * 0.9, -B, H, 0); g.quadraticCurveTo(H * 0.9, B, H * 0.45, B);
    g.lineTo(-H, B); g.closePath(); g.fillStyle = "#d63a2a"; g.fill(); g.strokeStyle = "#1b2129"; g.lineWidth = lw * 1.4; g.stroke();
    g.fillStyle = "#e9ebec"; g.fillRect(-H * 0.95, -B * 0.82, H * 1.3, B * 1.64);                         /* the open deck */
    g.fillStyle = "#9aa1a8"; g.fillRect(H * 0.2, -B * 0.85, H * 0.25, B * 1.7); g.strokeRect(H * 0.2, -B * 0.85, H * 0.25, B * 1.7);  /* the bridge block */
    g.fillStyle = "#9aa1a8"; g.fillRect(-H * 0.92, B * 0.3, H * 1.1, B * 0.45); g.strokeRect(-H * 0.92, B * 0.3, H * 1.1, B * 0.45); /* the crane, parked to starboard */
    g.beginPath(); g.arc(-H * 0.82, B * 0.52, B * 0.3, 0, 2 * Math.PI); g.fillStyle = "#6b737b"; g.fill(); g.stroke();  /* its pedestal, aft */
    g.beginPath(); g.arc(H * 0.7, 0, B * 0.9, 0, 2 * Math.PI); g.fillStyle = "#3d444c"; g.fill(); g.stroke();   /* the helideck, at the bow */
    g.save(); g.translate(H * 0.7, 0); g.rotate(Math.PI / 2); g.fillStyle = "#ffffff";
    g.font = `bold ${B * 1.0}px system-ui`; g.textAlign = "center"; g.textBaseline = "middle"; g.fillText("H", 0, 0); g.restore();
    g.fillStyle = "#f08a24"; g.fillRect(-H * 0.1, -B * 0.75, H * 0.14, B * 0.35);                                /* the rescue boat, port */
  }
  g.restore();
}
/* the HUD's badge: the side profile of the photograph */
function vesselBadge() {
  const c = document.createElement("canvas"), W = 37, H = 16, s = 2; c.width = W * s + 4; c.height = H * s + 4;
  const g = c.getContext("2d"), on = (r, k) => r >= 0 && r < H && k >= 0 && k < W && Math.floor(SHIP[r] / 2 ** (W - 1 - k)) % 2;
  const col = (r, k) => r >= 11 || (r >= 10 && k >= 29) || (r === 9 && k >= 31) ? "#d63a2a"
    : r <= 6 || (r === 7 && k >= 27) ? "#5b6470" : r === 9 && k >= 15 && k <= 18 ? "#f08a24" : "#f4f4f2";
  g.fillStyle = "#1b2129";
  for (let r = 0; r < H; r++) for (let k = 0; k < W; k++) if (on(r, k)) g.fillRect(2 + k * s - 1, 2 + r * s - 1, s + 2, s + 2);
  for (let r = 0; r < H; r++) for (let k = 0; k < W; k++) if (on(r, k)) { g.fillStyle = col(r, k); g.fillRect(2 + k * s, 2 + r * s, s, s); }
  return c.toDataURL();
}
let BADGE = null;

function draw() {
  if (!CH.g || $("t-route").hidden) return;
  const g = CH.g, V = S.view;
  g.setTransform(CH.dpr, 0, 0, CH.dpr, 0, 0);
  g.clearRect(0, 0, CH.W, CH.H);
  if (V.mode === "globe") {
    g.fillStyle = "#0b1220"; g.fillRect(0, 0, CH.W, CH.H);
    let z = 47; g.fillStyle = "#cfd8e6";
    for (let i = 0; i < 140; i++) { z = (z * 1103515245 + 12345) % 2147483648; const sx = z % CH.W; z = (z * 1103515245 + 12345) % 2147483648; g.fillRect(sx, z % CH.H, 1.2, 1.2); }
    g.beginPath(); g.arc(CH.W / 2, CH.H / 2, V.k, 0, 2 * Math.PI); g.fillStyle = css("--sea"); g.fill();
  } else { g.fillStyle = css("--sea"); g.fillRect(0, 0, CH.W, CH.H); }
  const step = drawGrid();
  const which = drawCoast();
  if (V.mode === "globe") { g.beginPath(); g.arc(CH.W / 2, CH.H / 2, V.k, 0, 2 * Math.PI); g.strokeStyle = css("--soft"); g.lineWidth = 1.5; g.stroke(); }
  drawStreams();
  for (const o of S.objects) drawObj(o, S.sel && S.sel.t === "o" && S.sel.id === o.id);
  /* the route */
  const R = S.route, wps = R.wps, rows = ROUTE.plan.rows;
  g.lineWidth = 1.3; g.strokeStyle = css("--soft");
  for (let i = 1; i < wps.length && wps.length < 60; i++) {
    if (R.sail !== 0) { g.setLineDash([7, 5]); polyline(rhumbLine(wps[i - 1], wps[i])); }
    if (R.sail !== 1) { g.setLineDash([2, 4]); polyline(geodLine(wps[i - 1], wps[i])); }
  }
  g.setLineDash([]);
  for (const w of wps) {                                      /* departure / arrival circles */
    const o = w.obj && S.objects.find(x => x.id === w.obj);
    if (o && R.avoid && o.r + R.margin > 0 && (o.r + R.margin) * pxPerM(o.lat) > 4) {
      g.setLineDash([2, 3]); g.strokeStyle = css("--accent"); g.lineWidth = 1; polyline(circlePts(o, o.r + R.margin)); g.setLineDash([]);
    }
  }
  g.strokeStyle = css("--track"); g.lineWidth = 3;
  for (let i = 1; i < rows.length; i++) polyline(rows[i].kind === "G" ? geodLine(rows[i - 1], rows[i]) : rhumbLine(rows[i - 1], rows[i]));
  if (ROUTE.check) for (const h of ROUTE.check.hits.concat(ROUTE.check.land)) {   /* legs into a zone or over land: red */
    g.strokeStyle = css("--now"); g.lineWidth = 4; polyline(geodLine(rows[h.leg - 1], rows[h.leg]));
  }
  if (ROUTE.check) for (const L of ROUTE.check.land) {         /* where: a red cross */
    const [x, y, v] = proj(L.at[0], L.at[1]); if (!v) continue;
    g.strokeStyle = css("--now"); g.lineWidth = 3; g.beginPath(); g.moveTo(x - 7, y - 7); g.lineTo(x + 7, y + 7); g.moveTo(x + 7, y - 7); g.lineTo(x - 7, y + 7); g.stroke();
  }
  g.font = "12px system-ui";
  rows.forEach((w, i) => {
    const [x, y, v] = proj(w.lat, w.lon); if (!v) return;
    if (w.flags & 1) { g.fillStyle = css("--card"); g.strokeStyle = css("--track"); g.lineWidth = 2; g.fillRect(x - 4, y - 4, 8, 8); g.strokeRect(x - 4, y - 4, 8, 8); }
    else { g.fillStyle = css("--track"); g.beginPath(); g.arc(x, y, 2.6, 0, 2 * Math.PI); g.fill(); }
  });
  ROUTE.pts.forEach((p, i) => {                               /* the planned points: numbered */
    const [x, y, v] = proj(p.lat, p.lon); if (!v) return;
    g.fillStyle = css("--track"); g.beginPath(); g.arc(x, y, p.user ? 5 : 3.5, 0, 2 * Math.PI); g.fill();
    g.fillStyle = css("--ink"); g.fillText((i + 1) + (p.name ? " " + p.name : ""), x + 7, y + 13);
  });
  wps.forEach((w, i) => {                                     /* the user's waypoints: handles */
    const [x, y, v] = proj(w.lat, w.lon); if (!v) return;
    g.strokeStyle = css("--accent"); g.lineWidth = 2; g.beginPath(); g.arc(x, y, 7, 0, 2 * Math.PI); g.stroke();
    if (S.sel && S.sel.t === "w" && S.sel.i === i) { g.beginPath(); g.arc(x, y, 11, 0, 2 * Math.PI); g.stroke(); }
  });
  { const [x, y, v] = proj(S.pos[0], S.pos[1]); if (v) { g.strokeStyle = css("--now"); g.lineWidth = 2; g.beginPath(); g.moveTo(x - 8, y); g.lineTo(x + 8, y); g.moveTo(x, y - 8); g.lineTo(x, y + 8); g.stroke(); } }
  if (VOY.on) { const p = VOY.at(); if (p) drawVessel(p.lat, p.lon, p.crs, R.speed >= 100, !VOY.playing && (VOY.t <= 0 || VOY.t >= 1)); }
  /* the scale and the corner */
  g.font = "12px system-ui"; g.fillStyle = css("--soft");
  const la0 = unproj(CH.W / 2, CH.H / 2);
  if (la0) {
    const mpp = 1 / pxPerM(la0[0]), want = mpp * 120, nice = [1, 2, 5].flatMap(m => [1, 10, 100, 1e3, 1e4, 1e5, 1e6].map(p => m * p)).sort((a, b) => a - b).filter(v => v <= want).pop() || 1;
    const px = nice / mpp, x0 = CH.W - px - 16, y0 = CH.H - 16;
    g.strokeStyle = css("--ink"); g.lineWidth = 2; g.beginPath(); g.moveTo(x0, y0 - 5); g.lineTo(x0, y0); g.lineTo(x0 + px, y0); g.lineTo(x0 + px, y0 - 5); g.stroke();
    g.fillStyle = css("--ink"); g.fillText(nice >= NM ? (nice / NM).toFixed(nice >= 10 * NM ? 0 : 1) + " NM" : nice + " m", x0, y0 - 8);
  }
  g.fillStyle = css("--soft"); g.fillText(`${which === "L" ? "Natural Earth 50 m" : "Natural Earth 110 m"} · grid ${step}°`, 10, CH.H - (VOY.on ? 8 : 10));
  hud();
  const al = $("alarm");
  if (al) {
    const c = ROUTE.check, n1 = c ? c.land.length : 0, n2 = c ? c.hits.length : 0;
    al.hidden = !(n1 || n2);
    al.textContent = `⛔ Not a safe route: ${[n1 ? n1 + " leg" + (n1 > 1 ? "s" : "") + " over land" : "", n2 ? n2 + " leg" + (n2 > 1 ? "s" : "") + " into a keep-out zone" : ""].filter(Boolean).join(", ")}`;
  }
}
function resize() {
  const cv = CH.cv; if (!cv) return;
  const r = cv.getBoundingClientRect(); if (!r.width) return;
  CH.dpr = window.devicePixelRatio || 1; CH.W = r.width; CH.H = r.height;
  cv.width = Math.round(r.width * CH.dpr); cv.height = Math.round(r.height * CH.dpr);
  clampView(); draw();
}

/* ---------------------------------------------------------------- the voyage */
const VOY = {
  on: false, t: 0, playing: false, last: 0, dur: 30,
  total() { const r = ROUTE.plan.rows; return r.length ? r[r.length - 1].cum : 0; },
  hours() { return VOY.total() / NM / S.route.speed; },
  etd() { return S.route.etd || nowJd(); },
  at() {
    const r = ROUTE.plan.rows; if (r.length < 2) return null;
    const s = VOY.t * VOY.total();
    let i = 1; while (i < r.length - 1 && r[i].cum < s) i++;
    const a = r[i - 1], b = r[i], ds = Math.max(0, Math.min(s - a.cum, b.cum - a.cum));
    if (b.kind === "G") { const q = NAV.inv(a.lat, a.lon, b.lat, b.lon), p = NAV.dir(a.lat, a.lon, q.a1, ds); return { lat: p.lat, lon: p.lon, crs: (p.az + 360) % 360, s }; }
    const p = NAV.rdir(a.lat, a.lon, a.rl, ds) || b; return { lat: p.lat, lon: p.lon, crs: a.rl, s };
  },
  tick(ts) {
    if (!VOY.playing) return;
    if (VOY.last) VOY.t = Math.min(1, VOY.t + (ts - VOY.last) / 1000 / VOY.dur);
    VOY.last = ts;
    if (VOY.t >= 1) VOY.playing = false;
    if ($("follow") && $("follow").checked) { const p = VOY.at(); if (p) { S.view.lat0 = p.lat; S.view.lon0 = p.lon; clampView(); } }
    draw(); voyUI();
    if (VOY.playing) requestAnimationFrame(VOY.tick);
  },
};
function hud() {
  const h = $("hud"); if (!h) return;
  if (!VOY.on || ROUTE.plan.rows.length < 2) { h.hidden = true; return; }
  h.hidden = false;
  const p = VOY.at(), H = VOY.hours(), gone = H * VOY.t, etd = VOY.etd(), off = zd(), lab = off ? "ZT" : "UT";
  const plane = S.route.speed >= 100;
  if (!BADGE) BADGE = vesselBadge();
  h.innerHTML = `<div style="display:flex;align-items:center;gap:8px;margin-bottom:2px">${plane ? '<span style="font-size:22px">✈</span>' : `<img src="${BADGE}" width="78" height="36" alt="">`}
      <span class="meta">${plane ? "Airliner, 64 m" : `Mermaid Asiana, ${S.route.len || VESSEL.len} m`} · ${VOY.trueScale ? "true scale" : "symbol (too small at this scale)"}</span></div>
    <div class="big">${FMT.dt(etd + gone / 24, off)} ${lab}</div>
    <table><tr><td>DEP</td><td class="num">${FMT.dt(etd, off)}</td><td>ARR</td><td class="num">${FMT.dt(etd + H / 24, off)}</td></tr>
    <tr><td>Gone</td><td class="num">${FMT.dur(gone)}</td><td>To go</td><td class="num">${FMT.dur(H - gone)}</td></tr>
    <tr><td>Run</td><td class="num">${FMT.nm(p.s)} NM</td><td>DTG</td><td class="num">${FMT.nm(VOY.total() - p.s)} NM</td></tr>
    <tr><td>Course</td><td class="num">${FMT.crs(p.crs)}</td><td>Speed</td><td class="num">${S.route.speed} kn</td></tr></table>
    <div class="mono" style="margin-top:2px">${FMT.pos(p.lat, p.lon)}</div>`;
}
function voyUI() { const s = $("voyt"); if (s) s.value = Math.round(VOY.t * 1000); const b = $("voyplay"); if (b) b.textContent = VOY.playing ? "Pause" : VOY.t >= 1 ? "Again" : "Play"; }

/* ---------------------------------------------------------------- the Route tab */
function routeTab() {
  const el = $("t-route");
  el.innerHTML = `<div class="split"><div>
    <div class="card"><div class="tools">
      <button data-tool="move" title="drag the chart, drag a waypoint or an object, click to select">✋ Move</button>
      <button data-tool="wp" title="click: a waypoint; on an object: from / to it">＋ Waypoint</button>
      <button data-tool="obj" title="click: an object of this type">＋ Object</button>
      <select id="objtype">${Object.entries(OBJ).map(([k, v]) => `<option value="${k}">${v.label}${v.r ? " (" + v.r + " m)" : ""}</option>`).join("")}</select>
      <span style="flex:1"></span>
      <button id="mode">${S.view.mode === "merc" ? "🌍 Globe" : "🗺 Mercator"}</button>
      <button id="streams" title="the tidal streams at the time shown (NOAA's stations, yours, your FLOW atlas)">〰 Streams</button>
      <button id="fit">Fit</button> <button id="del" title="delete the selected waypoint or object (Delete key)">Delete</button>
    </div>
    <div class="chartwrap"><canvas id="chart"></canvas><div class="hud" id="hud" hidden></div><div class="tip" id="tip"></div>
      <div id="alarm" hidden style="position:absolute;top:10px;left:50%;transform:translateX(-50%);background:var(--now);color:#fff;font-weight:700;
        padding:6px 14px;border-radius:999px;box-shadow:0 2px 10px rgba(0,0,0,.25)"></div></div>
    <div class="legend">
      <span><svg width="26" height="8"><line x1="0" y1="4" x2="26" y2="4" stroke="var(--track)" stroke-width="3"/></svg> the plan</span>
      <span><svg width="26" height="8"><line x1="0" y1="4" x2="26" y2="4" stroke="var(--soft)" stroke-dasharray="7 5" stroke-width="1.3"/></svg> rhumb line</span>
      <span><svg width="26" height="8"><line x1="0" y1="4" x2="26" y2="4" stroke="var(--soft)" stroke-dasharray="2 4" stroke-width="1.3"/></svg> great circle</span>
      <span><svg width="16" height="16"><circle cx="8" cy="8" r="6" fill="var(--zone)" stroke="var(--zonel)" stroke-dasharray="3 2"/></svg> keep-out zone</span>
      <span><svg width="16" height="16"><circle cx="8" cy="8" r="6" fill="none" stroke="var(--accent)" stroke-dasharray="2 2"/></svg> departure / arrival circle</span>
      <span id="cursor" class="mono"></span>
    </div></div>
    <div class="card" id="plancard"></div>
    </div><div>
    <div class="card" id="routecard"></div>
    <div class="card" id="checkcard"></div>
    <div class="card" id="voycard"></div>
    <div class="card" id="objcard"></div>
    <div class="card" id="cmpcard"></div>
  </div></div>`;
  CH.cv = $("chart"); CH.g = CH.cv.getContext("2d");
  el.querySelectorAll("[data-tool]").forEach(b => b.onclick = () => { S.tool = b.dataset.tool; tools(); });
  $("objtype").value = S.objType; $("objtype").onchange = e => { S.objType = e.target.value; S.tool = "obj"; tools(); };
  $("mode").onclick = () => { const c = unproj(CH.W / 2, CH.H / 2) || [S.view.lat0, S.view.lon0], s0 = spanDeg();
    S.view.mode = S.view.mode === "merc" ? "globe" : "merc"; S.view.lat0 = c[0]; S.view.lon0 = c[1];
    S.view.k = S.view.mode === "merc" ? CH.W / (s0 * D2R) : (CH.W / 2) / Math.sin(Math.min(89, s0 / 2) * D2R); clampView();
    $("mode").textContent = S.view.mode === "merc" ? "🌍 Globe" : "🗺 Mercator"; save(); draw(); };
  $("fit").onclick = () => { fitRoute(); save(); draw(); };
  $("streams").classList.toggle("on", !!S.showStreams);
  $("streams").onclick = () => { S.showStreams = !S.showStreams; store.set("streams", S.showStreams); $("streams").classList.toggle("on", S.showStreams); draw(); };
  $("del").onclick = delSel;
  chartEvents();
  tools();
  new ResizeObserver(resize).observe(CH.cv);
  refresh(true);
}
function tools() { document.querySelectorAll("[data-tool]").forEach(b => b.classList.toggle("on", b.dataset.tool === S.tool)); if (CH.cv) CH.cv.style.cursor = S.tool === "move" ? "grab" : "crosshair"; }
function delSel() {
  if (!S.sel) return;
  if (S.sel.t === "w") S.route.wps.splice(S.sel.i, 1);
  else { S.objects = S.objects.filter(o => o.id !== S.sel.id); S.route.wps.forEach(w => { if (w.obj === S.sel.id) w.obj = null; }); }
  S.sel = null; refresh();
}
function hit(x, y) {
  for (let i = S.route.wps.length - 1; i >= 0; i--) { const w = S.route.wps[i], [a, b, v] = proj(w.lat, w.lon); if (v && Math.hypot(a - x, b - y) < 10) return { t: "w", i }; }
  for (let i = S.objects.length - 1; i >= 0; i--) { const o = S.objects[i], [a, b, v] = proj(o.lat, o.lon); if (v && Math.hypot(a - x, b - y) < 11) return { t: "o", id: o.id }; }
  return null;
}
function chartEvents() {
  const cv = CH.cv;
  let down = null;
  const xy = e => { const r = cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  cv.addEventListener("pointerdown", e => {
    cv.setPointerCapture(e.pointerId);
    const [x, y] = xy(e), h = S.tool === "move" ? hit(x, y) : null;
    down = { x, y, moved: false, h, lat0: S.view.lat0, lon0: S.view.lon0, p0: psi(S.view.lat0) };
    if (h) S.sel = h;
  });
  cv.addEventListener("pointermove", e => {
    const [x, y] = xy(e), p = unproj(x, y);
    $("cursor").textContent = p ? FMT.pos(p[0], p[1]) : "";
    tip(x, y);
    if (!down) return;
    const dx = x - down.x, dy = y - down.y;
    if (Math.hypot(dx, dy) > 3) down.moved = true;
    if (!down.moved) return;
    if (down.h) {                                             /* drag a waypoint or an object */
      if (!p) return;
      if (down.h.t === "w") { const w = S.route.wps[down.h.i]; if (w.obj) w.obj = null; w.lat = p[0]; w.lon = p[1]; }
      else { const o = S.objects.find(o => o.id === down.h.id); o.lat = p[0]; o.lon = p[1]; S.route.wps.forEach(w => { if (w.obj === o.id) { w.lat = o.lat; w.lon = o.lon; } }); }
      CH.dragging = true; quick(); return;
    }
    const V = S.view;
    if (V.mode === "merc") { V.lon0 = down.lon0 - dx / V.k * R2D; V.lat0 = latOfPsi(down.p0 + dy / V.k); }
    else { V.lon0 = down.lon0 - dx / V.k * R2D / Math.max(0.2, Math.cos(V.lat0 * D2R)); V.lat0 = Math.max(-90, Math.min(90, down.lat0 + dy / V.k * R2D)); }
    clampView(); draw();
  });
  cv.addEventListener("pointerup", e => {
    const [x, y] = xy(e);
    if (down && !down.moved) click(x, y);
    if (CH.dragging) { CH.dragging = false; refresh(); }
    down = null; save();
  });
  cv.addEventListener("wheel", e => {
    e.preventDefault();
    const [x, y] = xy(e), f = Math.exp(-e.deltaY * 0.0015), before = unproj(x, y);
    S.view.k *= f; clampView();
    if (S.view.mode === "merc" && before) {                   /* the point under the cursor stays */
      const after = unproj(x, y);
      S.view.lon0 += before[1] - after[1]; S.view.lat0 = latOfPsi(psi(S.view.lat0) + psi(before[0]) - psi(after[0])); clampView();
    }
    draw(); save();
  }, { passive: false });
  cv.addEventListener("pointerleave", () => { $("tip").style.display = "none"; });
  window.addEventListener("keydown", e => {
    if ($("t-route").hidden || /INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
    if (e.key === "Delete" || e.key === "Backspace") { delSel(); e.preventDefault(); }
    if (e.key === "Escape") { S.sel = null; S.tool = "move"; tools(); draw(); }
  });
}
function click(x, y) {
  const p = unproj(x, y);
  if (S.tool === "move") { S.sel = hit(x, y); draw(); objList(); return; }
  if (!p) return;
  if (S.tool === "wp") {
    const h = hit(x, y);
    const o = h && h.t === "o" ? S.objects.find(o => o.id === h.id) : null;
    S.route.wps.push(o ? { lat: o.lat, lon: o.lon, name: o.name, obj: o.id } : { lat: p[0], lon: p[1], name: "", obj: null });
    S.sel = { t: "w", i: S.route.wps.length - 1 };
  } else {
    const n = S.objects.filter(o => o.type === S.objType).length + 1;
    const o = { id: newId(), type: S.objType, name: OBJ[S.objType].label.split(" ")[0] + " " + n, lat: p[0], lon: p[1], r: OBJ[S.objType].r };
    S.objects.push(o); S.sel = { t: "o", id: o.id };
  }
  refresh();
}
function tip(x, y) {
  const t = $("tip"), h = hit(x, y);
  if (!h) { t.style.display = "none"; return; }
  let s;
  if (h.t === "o") { const o = S.objects.find(o => o.id === h.id); s = `<b>${esc(o.name)}</b> · ${OBJ[o.type].label}${o.r ? " · zone " + o.r + " m" : ""}<br>${FMT.pos(o.lat, o.lon)}`; }
  else { const w = S.route.wps[h.i]; s = `<b>WP ${h.i + 1}${w.name ? " " + esc(w.name) : ""}</b><br>${FMT.pos(w.lat, w.lon)}`; }
  t.innerHTML = s; t.style.display = "block"; t.style.left = (x + 14) + "px"; t.style.top = (y + 10) + "px";
}
/* a quick redraw while dragging: the plan without the zones; the full plan when the drag ends */
function quick() {
  const R = S.route, pts = R.wps.map(w => ({ lat: w.lat, lon: w.lon, user: true, name: w.name }));
  ROUTE = { pts, plan: NAV.plan(pts, R.sail, R.lim, R.split, R.val), check: null };
  draw();
}
function refresh(first) {
  const t0 = performance.now();
  compute();
  ROUTE.ms = performance.now() - t0;
  if (first) fitRoute();
  save(); draw(); routeCard(); checkCard(); planCard(); objList(); cmpCard(); voyCard();
}

/* a passage between two objects: the route replaced by them, planned round every zone */
function passage(a, b) {
  const A = S.objects.find(o => o.id === a), B = S.objects.find(o => o.id === b);
  if (!A || !B || A === B) { alert("Choose two different objects: from and to."); return; }
  S.pass = { from: a, to: b };
  S.route.wps = [{ lat: A.lat, lon: A.lon, name: A.name, obj: A.id }, { lat: B.lat, lon: B.lon, name: B.name, obj: B.id }];
  S.route.avoid = true; S.route.split = 0;
  VOY.on = false; VOY.t = 0;
  refresh(); fitRoute(); draw();
}
function routeCard() {
  const R = S.route, el = $("routecard"); if (!el) return;
  const ells = Array.from({ length: Module._api_ell_count() - 1 }, (_, i) => NAV.ell(i));
  const objs = S.objects, opt = sel => objs.map(o => `<option value="${o.id}" ${o.id === sel ? "selected" : ""}>${esc(o.name)} (${OBJ[o.type].label})</option>`).join("");
  el.innerHTML = `<h2>Route</h2>
    ${objs.length >= 2 ? `<div class="controls" style="background:var(--chip);border-radius:10px;padding:6px 8px;margin-bottom:8px">
      <b>Passage</b> <label>from <select id="pfrom"><option value="">—</option>${opt(S.pass.from)}</select></label>
      <label>to <select id="pto"><option value="">—</option>${opt(S.pass.to)}</select></label>
      <button id="pgo" class="on">Plan</button>
      <span class="meta" style="flex-basis:100%">The shortest way between the two, clear of every zone by ${R.clear} m; it starts and ends
        ${R.margin} m outside their zones, on the side that makes it shortest.</span></div>` : ""}
    <div class="controls">
      <label>Sailing <select id="sail"><option value="0">RL rhumb line</option><option value="1">GC great circle</option><option value="2">CS composite</option></select></label>
      <label ${R.sail === 2 ? "" : "hidden"}>limit <input class="num" id="lim" type="number" min="10" max="85" step="1" value="${R.lim}">°</label>
      <label>Ellipsoid <select id="ell">${ells.map((e, i) => `<option value="${i}">${e.name}</option>`).join("")}</select></label>
    </div>
    <div class="controls" style="margin-top:6px">
      <label>Split <select id="split">${SPLITS.map(([k, t], i) => `<option value="${i}">${t}</option>`).join("")}</select></label>
      <input class="num" id="splitv" type="number" min="0" step="any" value="${R.val}" ${R.split ? "" : "hidden"}>
    </div>
    <div class="controls" style="margin-top:6px">
      <label><input type="checkbox" id="avoid" ${R.avoid ? "checked" : ""}> keep out of the zones</label>
      <label ${R.avoid ? "" : "hidden"}>pass zones by ≥ <input class="num" id="clear" type="number" min="0" step="10" value="${R.clear}"> m</label>
      <label ${R.avoid ? "" : "hidden"}>start / end <input class="num" id="margin" type="number" min="10" step="10" value="${R.margin}"> m outside the object's zone</label>
    </div>
    <h3>Waypoints <span class="meta">(click on the chart with ＋ Waypoint; on an object: from / to it)</span></h3>
    <div class="scroll" style="max-height:260px"><table id="wptab"><tr><th>#</th><th>Name</th><th>Position</th><th>Object</th><th></th></tr>
    ${R.wps.map((w, i) => `<tr class="${S.sel && S.sel.t === "w" && S.sel.i === i ? "sel" : ""}"><td>${i + 1}</td>
      <td><input class="name" data-wn="${i}" value="${esc(w.name || "")}"></td>
      <td><input class="pos mono" data-wp="${i}" value="${FMT.pos(w.lat, w.lon)}"></td>
      <td><select data-wo="${i}"><option value="">—</option>${S.objects.map(o => `<option value="${o.id}" ${o.id === w.obj ? "selected" : ""}>${esc(o.name)}</option>`).join("")}</select></td>
      <td><button class="small" data-up="${i}">↑</button><button class="small" data-dn="${i}">↓</button><button class="small" data-x="${i}">✕</button></td></tr>`).join("")}
    </table></div>
    <div class="controls" style="margin-top:6px"><label>Load ROUTE.TXT <input type="file" id="rtload" accept=".txt,.TXT"></label>
      <button id="rtsave" title="for the DM42n: /NAV47/ROUTE.TXT (the planned points, round the zones)">Save ROUTE.TXT</button></div>
    <div class="controls" style="margin-top:6px"><button id="wprev">Reverse</button><button id="wpclr">Clear</button>
      <button id="wpex">Example: Cape Town – Melbourne</button></div>`;
  $("sail").value = R.sail; $("ell").value = R.ell; $("split").value = R.split;
  if ($("pgo")) {
    $("pfrom").onchange = e => { S.pass.from = e.target.value; };
    $("pto").onchange = e => { S.pass.to = e.target.value; };
    $("pgo").onclick = () => passage(S.pass.from, S.pass.to);
  }
  const upd = () => { refresh(); };
  $("sail").onchange = e => { R.sail = +e.target.value; upd(); };
  $("lim").onchange = e => { R.lim = Math.max(10, Math.min(85, +e.target.value || 60)); upd(); };
  $("ell").onchange = e => { R.ell = +e.target.value; upd(); };
  $("split").onchange = e => { R.split = +e.target.value; R.val = [0, 100, 4, 10][R.split]; upd(); };
  $("splitv").onchange = e => { R.val = Math.max(0, +e.target.value || 0); upd(); };
  $("avoid").onchange = e => { R.avoid = e.target.checked; upd(); };
  $("margin").onchange = e => { R.margin = Math.max(10, +e.target.value || 50); upd(); };
  $("clear").onchange = e => { R.clear = Math.max(0, +e.target.value || 0); upd(); };
  document.querySelectorAll("[data-wn]").forEach(i => i.onchange = e => { R.wps[+i.dataset.wn].name = e.target.value; upd(); });
  document.querySelectorAll("[data-wp]").forEach(i => i.onchange = e => { const p = parsePos(e.target.value), w = R.wps[+i.dataset.wp];
    if (p) { w.lat = p[0]; w.lon = p[1]; w.obj = null; } upd(); });
  document.querySelectorAll("[data-wo]").forEach(s => s.onchange = e => { const w = R.wps[+s.dataset.wo], o = S.objects.find(o => o.id === e.target.value);
    w.obj = o ? o.id : null; if (o) { w.lat = o.lat; w.lon = o.lon; if (!w.name) w.name = o.name; } upd(); });
  document.querySelectorAll("[data-up]").forEach(b => b.onclick = () => { const i = +b.dataset.up; if (i > 0) { [R.wps[i - 1], R.wps[i]] = [R.wps[i], R.wps[i - 1]]; upd(); } });
  document.querySelectorAll("[data-dn]").forEach(b => b.onclick = () => { const i = +b.dataset.dn; if (i < R.wps.length - 1) { [R.wps[i + 1], R.wps[i]] = [R.wps[i], R.wps[i + 1]]; upd(); } });
  document.querySelectorAll("[data-x]").forEach(b => b.onclick = () => { R.wps.splice(+b.dataset.x, 1); S.sel = null; upd(); });
  $("wprev").onclick = () => { R.wps.reverse(); upd(); };
  $("rtload").onchange = e => { const f = e.target.files[0]; if (f) f.text().then(loadRouteTxt); };
  $("rtsave").onclick = () => { if (ROUTE.pts.length > 100) { alert(`The calculator holds 100 waypoints; this route has ${ROUTE.pts.length} points.`); return; }
    download("ROUTE.TXT", routeTxt()); };
  $("wpclr").onclick = () => { if (confirm("Clear the waypoints?")) { R.wps = []; VOY.on = false; upd(); } };
  $("wpex").onclick = () => { Object.assign(R, { wps: [{ lat: -33.80, lon: 18.30, name: "Off Cape Town", obj: null }, { lat: -34.40, lon: 18.20, name: "W of Cape Point", obj: null },
    { lat: -34.70, lon: 18.70, name: "S of Cape Point", obj: null }, { lat: -35.10, lon: 20.00, name: "Off Agulhas", obj: null },
    { lat: -39.00, lon: 143.50, name: "Off Cape Otway", obj: null }, { lat: -38.33, lon: 144.65, name: "Port Phillip Heads", obj: null }],
    sail: 2, lim: 45, split: 3, val: 10, speed: 12 }); refresh(); fitRoute(); draw(); };
}
function checkCard() {
  const el = $("checkcard"); if (!el) return;
  const rows = ROUTE.plan.rows, c = ROUTE.check, R = S.route;
  if (rows.length < 2) { el.innerHTML = `<h2>Check</h2><div class="meta">Two waypoints or more make a route.</div>`; return; }
  const zones = S.objects.filter(o => o.r > 0);
  const chips = [`<span class="chip">${FMT.nm(rows[rows.length - 1].cum)} NM</span>`, `<span class="chip">${ROUTE.pts.length} points, ${rows.length} with the split</span>`];
  if (c.flight) chips.push(`<span class="chip">✈ a flight (${R.speed} kn): over land allowed</span>`);
  else if (c.land.length) chips.unshift(`<span class="chip warn">⛔ ${c.land.length} leg${c.land.length > 1 ? "s" : ""} over land</span>`);
  else chips.push(`<span class="chip ok">✓ no leg over land</span>`);
  if (zones.length) {
    if (c.hits.length) chips.push(`<span class="chip warn">⚠ ${c.hits.length} leg${c.hits.length > 1 ? "s" : ""} into a zone</span>`);
    else if (c.min < Infinity) chips.push(`<span class="chip ${c.min >= Math.min(R.clear, R.margin) - 1 ? "ok" : "warn"}">✓ clear of every zone: the closest ${FMT.m(c.min)} off ${esc(c.minObj.name)} (leg ${c.minLeg})</span>`);
    else chips.push(`<span class="chip ok">✓ no zone near the route</span>`);
  }
  const det = ROUTE.pts.filter(p => !p.user).length;
  if (det) chips.push(`<span class="chip">${det} turning point${det > 1 ? "s" : ""} round the zones</span>`);
  if (ROUTE.plan.rc) chips.push(`<span class="chip warn">some legs sailed GC, not CS</span>`);
  if (ROUTE.plan.full) chips.push(`<span class="chip warn">plan full: a larger split</span>`);
  const inz = [];
  R.wps.forEach((w, i) => { if (w.obj) return; for (const z of zones) { const d = NAV.inv(w.lat, w.lon, z.lat, z.lon).s; if (d < z.r) inz.push(`WP ${i + 1} is inside the ${z.r} m zone of ${esc(z.name)} (${Math.round(d)} m off).`); } });
  el.innerHTML = `<h2>Check</h2><div class="chips">${chips.join("")}</div>
    ${c.land.map(L => `<div class="meta" style="color:var(--now)">⛔ Leg ${L.leg} crosses the coast near ${FMT.pos(L.at[0], L.at[1])}${L.n > 1 ? ` (${L.n} times)` : ""}.</div>`).join("")}
    ${c.hits.map(h => `<div class="meta warn" style="color:var(--now)">⛔ Leg ${h.leg} goes ${FMT.m(h.by)} into the ${h.obj.r} m zone of ${esc(h.obj.name)}.</div>`).join("")}
    ${c.warn.concat(inz).map(w => `<div class="meta" style="color:var(--now)">${w}</div>`).join("")}
    <div class="meta">Land: the Natural Earth 50 m coastline (simplified to ~2 km): it finds a leg across a headland, an island or a
      peninsula, not close inshore; the first and last 3 km of the route (and round a Port object) are left out. Check on the official chart.</div>
    <div class="meta">${R.avoid ? `The route passes every zone by ${R.clear} m or more; from or to an object it starts or ends ${R.margin} m outside its zone (${500 + R.margin} m from a platform), on the side that makes the route shortest. Checked on the ${["rhumb lines", "great circles", "legs"][R.sail]} as sailed, with exact distances.` : "Keeping out of the zones is off."} ${ROUTE.ms ? `(${Math.round(ROUTE.ms)} ms)` : ""}</div>`;
}
function planCard() {
  const el = $("plancard"); if (!el) return;
  const rows = ROUTE.plan.rows, R = S.route;
  if (rows.length < 2) { el.innerHTML = "<h2>Plan</h2><div class='meta'>No route yet: ＋ Waypoint, then click on the chart (or Example).</div>"; return; }
  const etd = VOY.etd(), off = zd();
  el.innerHTML = `<h2>Plan</h2><div class="scroll" style="max-height:340px"><table><tr><th>WP</th><th>Name</th><th>Lat</th><th>Lon</th><th class="num">Steer</th><th class="num">Leg NM</th><th class="num">Cum NM</th><th></th><th class="num">ETA ${off ? "ZT" : "UT"}</th></tr>
    ${rows.map((w, i) => { const p = ROUTE.pts.find(q => Math.abs(q.lat - w.lat) < 1e-9 && Math.abs(q.lon - w.lon) < 1e-9);
      return `<tr class="${w.flags & 1 ? "" : "dim"}"><td>${i + 1}</td><td>${esc(p && p.name ? p.name + (p.off === "dep" ? " (dep.)" : p.off === "arr" ? " (arr.)" : "") : p && !p.user ? "turn" : "")}</td>
      <td class="mono">${FMT.lat(w.lat)}</td><td class="mono">${FMT.lon(w.lon)}</td>
      <td class="num">${i ? FMT.crs(rows[i - 1].rl) : ""}</td><td class="num">${i ? FMT.nm(w.leg) : ""}</td><td class="num">${i ? FMT.nm(w.cum) : ""}</td>
      <td>${i ? w.kind : ""}</td><td class="num">${FMT.dt(etd + w.cum / NM / R.speed / 24, off)}</td></tr>`; }).join("")}</table></div>
    <div class="controls" style="margin-top:8px"><button id="xcsv">CSV</button><button id="xnmea">NMEA</button><button id="xgpx">GPX</button>
      <button id="xtxt">ROUTE.TXT</button><button id="xcalc">To the calculator</button></div>
    <div class="meta">Steer: the rhumb course to the next point. G geodesic, R rhumb, P parallel (composite). ETA at ${R.speed} kn from the departure in Voyage.</div>`;
  $("xcsv").onclick = () => download("PLAN.CSV", planCSV());
  $("xnmea").onclick = () => download("PLAN.NMEA", planNMEA());
  $("xgpx").onclick = () => download("PLAN.GPX", planGPX());
  $("xtxt").onclick = () => download("ROUTE.TXT", routeTxt());
  $("xcalc").onclick = () => {
    if (ROUTE.pts.length > 100) { alert(`The calculator holds 100 waypoints; this route has ${ROUTE.pts.length} points.`); return; }
    if (confirm("Send this route to the calculator (its ROUTE.TXT) and open it?")) CALC.putAndRestart("ROUTE.TXT", routeTxt());
  };
}
function planCSV() {
  const r = ROUTE.plan.rows, L = ["WPT,LAT,LON,LAT_DM,LON_DM,LEG_NM,CUM_NM,STEER,SEG"];
  r.forEach((w, i) => L.push([`WP${String(i + 1).padStart(3, "0")}`, w.lat.toFixed(6), w.lon.toFixed(6), FMT.lat(w.lat), FMT.lon(w.lon),
    i ? FMT.nm(w.leg) : "0.0", FMT.nm(w.cum), i ? FMT.crs(r[i - 1].rl) : "", i ? w.kind : ""].join(",")));
  return L.join("\r\n") + "\r\n";
}
function nmea(s) { let c = 0; for (const ch of s.slice(1)) c ^= ch.charCodeAt(0); return s + "*" + c.toString(16).toUpperCase().padStart(2, "0"); }
function nmeaPos(v, lat) {
  const a = Math.abs(v), d = Math.floor(a), m = (a - d) * 60;
  return (lat ? String(d).padStart(2, "0") : String(d).padStart(3, "0")) + m.toFixed(4).padStart(7, "0") + "," + (lat ? (v < 0 ? "S" : "N") : (v < 0 ? "W" : "E"));
}
function planNMEA() {
  const r = ROUTE.plan.rows, L = [], id = "NAV47";
  r.forEach((w, i) => L.push(nmea(`$GPWPL,${nmeaPos(w.lat, 1)},${nmeaPos(nearLon(w.lon, 0), 0)},WP${String(i + 1).padStart(3, "0")}`)));
  const k = Math.floor((77 - (15 + id.length)) / 6), ns = Math.ceil(r.length / k);
  for (let j = 0; j < ns; j++) L.push(nmea(`$GPRTE,${ns},${j + 1},c,${id},` + r.slice(j * k, j * k + k).map((_, i) => "WP" + String(j * k + i + 1).padStart(3, "0")).join(",")));
  return L.join("\r\n") + "\r\n";
}
function planGPX() {
  const r = ROUTE.plan.rows;
  return `<?xml version="1.0" encoding="UTF-8"?>\n<gpx version="1.1" creator="NAV47" xmlns="http://www.topografix.com/GPX/1/1"><rte><name>NAV47</name>\n` +
    r.map((w, i) => `<rtept lat="${w.lat.toFixed(7)}" lon="${nearLon(w.lon, 0).toFixed(7)}"><name>WP${String(i + 1).padStart(3, "0")}</name></rtept>`).join("\n") + "\n</rte></gpx>\n";
}
/* the calculator's ROUTE.TXT: the planned points (round the zones) as its waypoints, its sailing, split, ellipsoid, speed */
function routeTxt() {
  const R = S.route;
  return `NAV47 ROUTE 1\n${["RL", "GC", "CS"][R.sail]}\nLIM ${R.lim.toFixed(1)}\nSPLIT ${R.split} ${(+R.val).toFixed(3)}\nELL ${R.ell}\nSPEED ${(+R.speed).toFixed(4)}\nETD ${(R.etd || 0).toFixed(6)}\n` +
    ROUTE.pts.map(p => `${p.lat.toFixed(7)} ${nearLon(p.lon, 0).toFixed(7)}`).join("\n") + "\n";
}
/* the calculator's ROUTE.TXT into the route: its waypoints, sailing, limit, split, ellipsoid, speed, departure */
function loadRouteTxt(t) {
  const R = S.route, wps = [];
  for (let l of t.replace(/\r/g, "").split("\n")) {
    l = l.trim(); let m;
    if (l === "RL") R.sail = 0; else if (l === "GC") R.sail = 1; else if (l === "CS") R.sail = 2;
    else if ((m = l.match(/^LIM\s+(-?[\d.]+)/))) R.lim = Math.max(10, Math.min(85, +m[1]));
    else if ((m = l.match(/^SPLIT\s+(\d)\s+(-?[\d.]+)/))) { R.split = Math.min(3, +m[1]); R.val = +m[2]; }
    else if ((m = l.match(/^ELL\s+(\d+)/))) R.ell = Math.min(+m[1], Module._api_ell_count() - 2);
    else if ((m = l.match(/^SPEED\s+([\d.]+)/))) R.speed = Math.max(1, Math.min(1000, +m[1]));
    else if ((m = l.match(/^ETD\s+([\d.]+)/))) R.etd = +m[1] > 2400000 ? +m[1] : null;
    else if ((m = l.match(/^(-?[\d.]+)\s+(-?[\d.]+)$/)) && Math.abs(+m[1]) <= 90 && Math.abs(+m[2]) <= 180 && wps.length < 100)
      wps.push({ lat: +m[1], lon: +m[2], name: "", obj: null });
  }
  if (wps.length < 1) { alert("No waypoints in this file."); return; }
  R.wps = wps; VOY.on = false; VOY.t = 0;
  refresh(); fitRoute(); draw();
}
function download(name, text) {
  const u = URL.createObjectURL(new Blob([text], { type: "text/plain" }));
  const a = document.createElement("a"); a.href = u; a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(u), 1000);
}

function objList() {
  const el = $("objcard"); if (!el) return;
  el.innerHTML = `<h2>Objects <span class="meta">${S.objects.length}</span></h2>
    <div class="controls"><label>Load <input type="file" id="objload" accept=".csv,.txt,.json,.geojson,.gpx,.kml" multiple></label>
      <button id="objcsv">Save CSV</button><button id="objex">Example field</button><button id="objclr">Forget all</button></div>
    <div class="scroll" style="max-height:300px;margin-top:6px"><table><tr><th>Type</th><th>Name</th><th>Position</th><th class="num">Zone m</th><th></th></tr>
    ${S.objects.map(o => `<tr class="click ${S.sel && S.sel.t === "o" && S.sel.id === o.id ? "sel" : ""}" data-oid="${o.id}">
      <td><select data-ot="${o.id}">${Object.entries(OBJ).map(([k, v]) => `<option value="${k}" ${k === o.type ? "selected" : ""}>${v.label}</option>`).join("")}</select></td>
      <td><input class="name" data-on="${o.id}" value="${esc(o.name)}"></td>
      <td><input class="pos mono" data-op="${o.id}" value="${FMT.pos(o.lat, o.lon)}"></td>
      <td><input class="num" style="width:4.5em" type="number" min="0" step="10" data-or="${o.id}" value="${o.r}"></td>
      <td style="white-space:nowrap"><button class="small ${S.pass.from === o.id ? "on" : ""}" data-pf="${o.id}" title="the passage from it">From</button><button class="small ${S.pass.to === o.id ? "on" : ""}" data-pt="${o.id}" title="the passage to it">To</button><button class="small" data-ofrom="${o.id}" title="a waypoint at it (added to the route)">＋WP</button><button class="small" data-ox="${o.id}">✕</button></td></tr>`).join("")}</table></div>
    <div class="meta">Load a CSV (<span class="mono">type, name, lat, lon[, zone m]</span> or <span class="mono">name, lat, lon</span>; positions as
      27.2 or 27 12.0N), GeoJSON, GPX or KML points. They stay in this browser, never sent anywhere.</div>`;
  const find = id => S.objects.find(o => o.id === id);
  const upd = () => refresh();
  document.querySelectorAll("[data-ot]").forEach(s => s.onchange = e => { const o = find(s.dataset.ot); o.type = e.target.value; o.r = OBJ[o.type].r; upd(); });
  document.querySelectorAll("[data-on]").forEach(s => s.onchange = e => { find(s.dataset.on).name = e.target.value; upd(); });
  document.querySelectorAll("[data-op]").forEach(s => s.onchange = e => { const p = parsePos(e.target.value), o = find(s.dataset.op);
    if (p) { o.lat = p[0]; o.lon = p[1]; S.route.wps.forEach(w => { if (w.obj === o.id) { w.lat = o.lat; w.lon = o.lon; } }); } upd(); });
  document.querySelectorAll("[data-or]").forEach(s => s.onchange = e => { find(s.dataset.or).r = Math.max(0, +e.target.value || 0); upd(); });
  document.querySelectorAll("[data-ox]").forEach(b => b.onclick = () => { S.sel = { t: "o", id: b.dataset.ox }; delSel(); });
  document.querySelectorAll("[data-ofrom]").forEach(b => b.onclick = () => { const o = find(b.dataset.ofrom); S.route.wps.push({ lat: o.lat, lon: o.lon, name: o.name, obj: o.id }); upd(); });
  /* From, then To: the passage is planned as soon as both are chosen */
  document.querySelectorAll("[data-pf]").forEach(b => b.onclick = () => { S.pass.from = b.dataset.pf; if (S.pass.to && S.pass.to !== S.pass.from) passage(S.pass.from, S.pass.to); else { routeCard(); objList(); } });
  document.querySelectorAll("[data-pt]").forEach(b => b.onclick = () => { S.pass.to = b.dataset.pt; if (S.pass.from && S.pass.to !== S.pass.from) passage(S.pass.from, S.pass.to); else { routeCard(); objList(); } });
  document.querySelectorAll("tr[data-oid]").forEach(tr => tr.onclick = e => { if (e.target.tagName !== "TD") return; S.sel = { t: "o", id: tr.dataset.oid }; const o = find(tr.dataset.oid);
    S.view.lat0 = o.lat; S.view.lon0 = o.lon; S.view.k = Math.max(S.view.k, CH.W / (0.15 * D2R)); clampView(); draw(); objList(); });
  $("objload").onchange = e => Promise.all([...e.target.files].map(f => f.text().then(t => parseObjects(t, f.name)))).then(ls => {
    const add = ls.flat(); S.objects.push(...add); alert(`${add.length} objects loaded.`); refresh(); fit(add); draw(); });
  $("objcsv").onclick = () => download("objects.csv", "type,name,lat,lon,zone_m\n" + S.objects.map(o => [o.type, `"${o.name.replace(/"/g, "'")}"`, o.lat.toFixed(7), o.lon.toFixed(7), o.r].join(",")).join("\n") + "\n");
  $("objex").onclick = exampleField;
  $("objclr").onclick = () => { if (confirm("Forget every object in this browser?")) { S.objects = []; S.route.wps.forEach(w => w.obj = null); refresh(); } };
}
function typeOf(s) {
  s = String(s || "").toLowerCase();
  for (const k of Object.keys(OBJ)) if (s.startsWith(k) || s.startsWith(OBJ[k].label.toLowerCase())) return k;
  if (/plat|jacket|tp|gosp/.test(s)) return "platform";
  if (/buoy|bouy/.test(s)) return "buoy";
  if (/well/.test(s)) return "wellhead";
  if (/rig|jack/.test(s)) return "rig";
  if (/light|beacon/.test(s)) return "light";
  if (/wreck/.test(s)) return "wreck";
  if (/port|berth|pier/.test(s)) return "port";
  return null;
}
function parseObjects(t, name) {
  const out = [], add = (type, nm, la, lo, r) => {
    if (!isFinite(la) || !isFinite(lo)) return;
    const ty = type || typeOf(nm) || "platform";
    out.push({ id: newId(), type: ty, name: String(nm || OBJ[ty].label).trim(), lat: la, lon: lo, r: isFinite(r) && r !== null ? r : OBJ[ty].r });
  };
  if (/\.(geo)?json$/i.test(name) || /^\s*{/.test(t)) {
    const g = JSON.parse(t);
    for (const f of g.features || []) if (f.geometry && f.geometry.type === "Point") {
      const p = f.properties || {};
      add(typeOf(p.type || p.kind), p.name || p.title, f.geometry.coordinates[1], f.geometry.coordinates[0], p.zone_m !== undefined ? +p.zone_m : p.radius !== undefined ? +p.radius : null);
    }
    return out;
  }
  if (/<gpx|<kml/i.test(t)) {
    const d = new DOMParser().parseFromString(t, "text/xml");
    d.querySelectorAll("wpt, rtept").forEach(w => add(typeOf((w.querySelector("type") || {}).textContent), (w.querySelector("name") || {}).textContent, +w.getAttribute("lat"), +w.getAttribute("lon"), null));
    d.querySelectorAll("Placemark").forEach(p => { const c = (p.querySelector("coordinates") || {}).textContent; if (!c) return;
      const [lo, la] = c.trim().split(/[\s,]+/).map(Number); add(null, (p.querySelector("name") || {}).textContent, la, lo, null); });
    return out;
  }
  for (let line of t.split(/\r?\n/)) {
    line = line.trim(); if (!line || line.startsWith("#")) continue;
    const f = line.split(/\s*[,;\t]\s*/).map(s => s.replace(/^"|"$/g, ""));
    if (/^(type|name)$/i.test(f[0])) continue;
    let ty = typeOf(f[0]) && f.length >= 4 && !parsePos(f[0] + " " + f[1]) ? typeOf(f[0]) : null, rest = ty ? f.slice(1) : f;
    const nm = rest[0], pos = parsePos(rest.slice(1, 3).join(" ")) || parsePos(rest.slice(1).join(" "));
    if (!pos) continue;
    const r = rest.length >= 4 && /^\d+(\.\d+)?$/.test(rest[rest.length - 1]) && rest.length > 3 ? +rest[rest.length - 1] : null;
    add(ty, nm, pos[0], pos[1], r);
  }
  return out;
}
/* a made-up field (positions invented, in the open North Sea) to try the planning */
function exampleField() {
  const c = [57.20, 1.90], d = (dn, de) => [c[0] + dn / 60, c[1] + de / 60 / Math.cos(c[0] * D2R)];
  const L = [["platform", "Alpha", 0, 0], ["platform", "Bravo", 1.2, 0.9], ["platform", "Charlie", 2.6, 1.4], ["platform", "Delta", 1.0, 2.6],
             ["platform", "Echo", 3.6, 3.0], ["wellhead", "Well 7", 2.0, 2.1], ["rig", "Rig Juliet", 0.4, 1.6], ["spm", "SPM 1", 4.4, 0.6],
             ["buoy", "Buoy N1", 1.8, -0.5], ["buoy", "Buoy N2", 3.1, 2.2], ["buoy", "Buoy E", 0.5, 3.4], ["light", "Light Echo", 3.9, 3.6],
             ["wreck", "Wreck", 2.4, -0.2]];
  const ids = {};
  for (const [ty, nm, n, e] of L) { const [la, lo] = d(n, e), o = { id: newId(), type: ty, name: nm, lat: la, lon: lo, r: OBJ[ty].r }; ids[nm] = o; S.objects.push(o); }
  S.route.wps = [{ lat: ids.Alpha.lat, lon: ids.Alpha.lon, name: "Alpha", obj: ids.Alpha.id }, { lat: ids.Echo.lat, lon: ids.Echo.lon, name: "Echo", obj: ids.Echo.id }];
  Object.assign(S.route, { sail: 0, split: 0, avoid: true, speed: 10 });
  refresh(); fitRoute(); draw();
}
function cmpCard() {
  const el = $("cmpcard"); if (!el) return;
  const w = S.route.wps;
  if (w.length < 2) { el.innerHTML = "<h2>Compare</h2><div class='meta'>The sailings for the first and the last waypoint.</div>"; return; }
  const a = w[0], b = w[w.length - 1], g = NAV.inv(a.lat, a.lon, b.lat, b.lon), r = NAV.rinv(a.lat, a.lon, b.lat, b.lon), v = NAV.vertex(a.lat, a.lon, b.lat, b.lon);
  const cs = NAV.plan([a, b], 2, S.route.lim, 0, 0), csd = cs.rows.length ? cs.rows[cs.rows.length - 1].cum : NaN;
  const e = NAV.ell(S.route.ell);
  el.innerHTML = `<h2>Compare <span class="meta">${esc(a.name || "WP 1")} → ${esc(b.name || "WP " + w.length)} · ${e.name}</span></h2>
    <table><tr><th></th><th class="num">Great circle</th><th class="num">Rhumb line</th></tr>
    <tr><td>Distance</td><td class="num">${FMT.nm(g.s)} NM</td><td class="num">${FMT.nm(r.s)} NM</td></tr>
    <tr><td>Initial course</td><td class="num">${(((g.a1 % 360) + 360) % 360).toFixed(1)}°</td><td class="num">${(((r.az % 360) + 360) % 360).toFixed(1)}°</td></tr>
    <tr><td>Final course</td><td class="num">${(((g.a2 % 360) + 360) % 360).toFixed(1)}°</td><td class="num">${(((r.az % 360) + 360) % 360).toFixed(1)}°</td></tr>
    <tr><td>GC saves</td><td class="num" colspan="2">${FMT.nm(r.s - g.s)} NM (${(100 * (r.s - g.s) / r.s).toFixed(1)}%)</td></tr>
    <tr><td>Vertex</td><td class="num mono" colspan="2">${FMT.pos(v.lat, v.lon)}${v.inside ? "" : " (off the leg)"}</td></tr>
    <tr><td>Composite ${S.route.lim}°</td><td class="num" colspan="2">${Math.abs(v.lat) > S.route.lim && v.inside ? FMT.nm(csd) + " NM" : "the limit is not reached"}</td></tr></table>
    <div class="meta">Without the zones. Geodesics: GeographicLib; rhumb lines exact on the ellipsoid.</div>`;
}
function voyCard() {
  const el = $("voycard"); if (!el) return;
  const R = S.route;
  if (ROUTE.plan.rows.length < 2) { el.innerHTML = "<h2>Voyage</h2><div class='meta'>Plan a route first.</div>"; return; }
  const etd = VOY.etd(), H = VOY.hours();
  const iso = new Date(msOf(etd)).toISOString().slice(0, 16);
  el.innerHTML = `<h2>Voyage <span class="meta">${R.speed >= 100 ? "✈ flight" : "🚢 the Mermaid Asiana"}</span></h2>
    <div class="controls"><label>Speed <input class="num" id="speed" type="number" min="1" max="1000" step="0.01" value="${(+R.speed).toFixed(2)}"> kn</label>
      <label ${R.speed >= 100 ? "hidden" : ""}>Length <input class="num" id="vlen" type="number" min="5" max="500" step="1" value="${R.len || 95}"> m</label>
      <label>Departure UT <input type="datetime-local" id="etd" value="${iso}" step="60"></label><button id="etdnow" class="small">now</button>
      <label>Arrival UT <input type="datetime-local" id="eta" value="${new Date(msOf(etd + H / 24)).toISOString().slice(0, 16)}" step="60"></label>
      <label title="a new departure keeps the arrival (the speed follows), else the speed (the arrival follows)"><input type="checkbox" id="keepeta" ${S.keepEta ? "checked" : ""}> a new departure keeps the arrival</label></div>
    <div class="controls" style="margin-top:6px"><button id="voyplay">Play</button><input type="range" id="voyt" min="0" max="1000" value="${Math.round(VOY.t * 1000)}" style="flex:1">
      <label><input type="checkbox" id="follow" checked> follow</label></div>
    ${ROUTE.check && (ROUTE.check.land.length || ROUTE.check.hits.length) ? `<div class="meta" style="color:var(--now);font-weight:600">⛔ Not a safe route
      (${[ROUTE.check.land.length ? "over land" : "", ROUTE.check.hits.length ? "into a keep-out zone" : ""].filter(Boolean).join(", ")}): see Check.</div>` : ""}
    <h3>ETA at the set speed and slower <span class="meta">step <select id="etastep"><option>0.25</option><option>0.5</option><option>1</option></select> kn</span></h3>
    <table><tr><th>Speed</th><th class="num">Time</th><th class="num">ETA ${zd() ? "ZT" : "UT"}</th><th></th></tr>
    ${[0, 1, 2, 3].map(k => { const kn = R.speed - k * S.etaStep; if (kn < 0.5) return "";
      const h = VOY.total() / NM / kn;
      return `<tr><td class="mono">${kn.toFixed(2)} kn${k ? "" : " *"}</td><td class="num mono">${FMT.dur(h)}</td><td class="num mono">${FMT.dt(etd + h / 24, zd())}</td>
        <td>${k ? `<button class="small" data-use="${kn}">use</button>` : ""}</td></tr>`; }).join("")}</table>
    <div class="meta">${FMT.nm(VOY.total())} NM in ${FMT.dur(H)}: arrival ${FMT.dt(etd + H / 24, zd())} ${zd() ? "ZT" : "UT"}. The whole voyage plays in ${VOY.dur} s.</div>`;
  $("vlen").onchange = e => { R.len = Math.max(5, Math.min(500, +e.target.value || 100)); save(); draw(); };
  $("speed").onchange = e => { const was = R.speed >= 100; R.speed = Math.max(1, Math.min(1000, +e.target.value || 12)); save();
    if (was !== (R.speed >= 100)) refresh(); else { voyCard(); planCard(); draw(); } };
  $("etd").onchange = e => {
    const t = Date.parse(e.target.value + "Z"); if (!isFinite(t)) return;
    const eta = etd + H / 24, nd = jdOf(t);
    if (S.keepEta) { const h = (eta - nd) * 24, v = VOY.total() / NM / h;
      if (!(h > 0) || v < 1 || v > 1000) { alert("The departure must be before the arrival (1 to 1000 kn)."); voyCard(); return; }
      R.speed = v; }
    R.etd = nd; save(); voyCard(); planCard(); draw(); };
  $("eta").onchange = e => {                                   /* the arrival typed: the speed it needs */
    const t = Date.parse(e.target.value + "Z"); if (!isFinite(t)) return;
    const h = (jdOf(t) - etd) * 24, v = VOY.total() / NM / h;
    if (!(h > 0) || v < 1 || v > 1000) { alert("The arrival must be after the departure (1 to 1000 kn)."); voyCard(); return; }
    R.speed = v; if (!R.etd) R.etd = etd; save(); voyCard(); planCard(); draw(); };
  $("etastep").value = String(S.etaStep);
  $("etastep").onchange = e => { S.etaStep = +e.target.value; store.set("etaStep", S.etaStep); voyCard(); };
  document.querySelectorAll("[data-use]").forEach(b => b.onclick = () => { R.speed = +b.dataset.use; save(); voyCard(); planCard(); draw(); });
  $("keepeta").onchange = e => { S.keepEta = e.target.checked; store.set("keepEta", S.keepEta); };
  $("etdnow").onclick = () => { R.etd = null; save(); voyCard(); planCard(); draw(); };
  $("voyplay").onclick = () => { VOY.on = true; if (VOY.t >= 1) VOY.t = 0; VOY.playing = !VOY.playing; VOY.last = 0; voyUI(); if (VOY.playing) requestAnimationFrame(VOY.tick); else draw(); };
  $("voyt").oninput = e => { VOY.on = true; VOY.playing = false; VOY.t = e.target.value / 1000; voyUI();
    if ($("follow").checked) { const p = VOY.at(); if (p) { S.view.lat0 = p.lat; S.view.lon0 = p.lon; clampView(); } } draw(); };
  voyUI();
}

/* ---------------------------------------------------------------- the page */
const TABS = [["route", "Route"], ["weather", "Weather"], ["almanac", "Almanac"], ["sky", "Sky"], ["tide", "Tide"], ["flow", "Flow"], ["zones", "Zones"], ["dp", "DP"], ["calc", "Calculator"]];
function showTab(t) {
  S.tab = t; store.set("tab", t);
  document.querySelectorAll(".tab").forEach(b => b.classList.toggle("on", b.dataset.tab === t));
  TABS.forEach(([k]) => $("t-" + k).hidden = k !== t);
  if (t === "route") resize();
  else if (t === "calc") CALC.start();
  else if (t === "weather") WX.show();
  else if (typeof TABS2 !== "undefined" && TABS2[t]) TABS2[t]();
}
function header() {
  const ut = $("ut"), dr = $("dr"), now = $("now"), times = $("times");
  times.innerHTML = '<option value="ut">all times UT</option><option value="zt">all times ZT (the DR\'s zone)</option>';
  const show = () => {
    ut.value = new Date(msOf(nowJd())).toISOString().slice(0, 16); now.classList.toggle("on", S.live);
    const z = Math.round(S.pos[1] / 15), d = new Date(msOf(nowJd()) + z * 36e5), p2 = n => String(n).padStart(2, "0");
    $("ztclock").textContent = `ZT ${p2(d.getUTCDate())}-${p2(d.getUTCMonth() + 1)} ${p2(d.getUTCHours())}:${p2(d.getUTCMinutes())} (UTC${z >= 0 ? "+" : "−"}${Math.abs(z)})`;
  };
  dr.value = FMT.pos(S.pos[0], S.pos[1]); times.value = S.times; show();
  now.onclick = () => { S.live = !S.live; S.jd = jdOf(Date.now()); show(); rerender(); };
  ut.onchange = () => { const t = Date.parse(ut.value + "Z"); if (isFinite(t)) { S.live = false; S.jd = jdOf(t); } show(); rerender(); };
  dr.onchange = () => { const p = parsePos(dr.value); if (p) S.pos = p; dr.value = FMT.pos(S.pos[0], S.pos[1]); save(); show(); rerender(); };
  times.onchange = () => { S.times = times.value; save(); rerender(); };
  setInterval(() => { if (S.live) { show(); if (S.tab !== "route" && S.tab !== "calc") rerender(); } }, 5000);
}
function rerender() { if (S.tab === "route") { draw(); planCard(); voyCard(); } else if (typeof TABS2 !== "undefined" && TABS2[S.tab]) TABS2[S.tab](); }

/* #test=avoid: random fields, random routes between two objects; every leg checked with exact distances */
function selfTest() {
  let seed = 4747;
  const rnd = () => (seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648;
  const res = [], keep = { o: S.objects, r: JSON.parse(JSON.stringify(S.route)) };
  for (let t = 0; t < 40; t++) {
    const dense = /dense/.test(location.hash);
    const n = dense ? 40 + Math.floor(rnd() * 41) : 15 + Math.floor(rnd() * 66), c = [-60 + rnd() * 120, -180 + rnd() * 360],
          span = dense ? 2 + rnd() * 4 : 4 + rnd() * 20;   /* NM */
    const types = ["platform", "platform", "platform", "wellhead", "rig", "spm", "buoy", "buoy", "light", "wreck"];
    S.objects = [];
    for (let i = 0; i < n; i++) {
      const ty = types[Math.floor(rnd() * types.length)], p = NAV.dir(c[0], c[1], rnd() * 360, rnd() * span * NM);
      S.objects.push({ id: "t" + i, type: ty, name: ty + i, lat: p.lat, lon: p.lon, r: OBJ[ty].r });
    }
    const pl = S.objects.filter(o => o.type === "platform");
    if (pl.length < 2) continue;
    const a = pl[0], b = pl[pl.length - 1];
    S.route.wps = [{ lat: a.lat, lon: a.lon, name: a.name, obj: a.id }, { lat: b.lat, lon: b.lon, name: b.name, obj: b.id }];
    S.route.sail = Math.floor(rnd() * 2); S.route.split = 0; S.route.avoid = true; S.route.clear = 50; S.route.margin = 50;
    const t0 = performance.now(); compute(); const ms = performance.now() - t0, c2 = ROUTE.check;
    const d0 = NAV.inv(ROUTE.pts[0].lat, ROUTE.pts[0].lon, a.lat, a.lon).s, d1 = NAV.inv(ROUTE.pts[ROUTE.pts.length - 1].lat, ROUTE.pts[ROUTE.pts.length - 1].lon, b.lat, b.lon).s;
    res.push({ n, pts: ROUTE.pts.length, hits: c2.hits.length, min: Math.round(c2.min * 10) / 10, dep: Math.round(d0 * 10) / 10, arr: Math.round(d1 * 10) / 10,
               warn: c2.warn.filter(w => /sailed straight/.test(w)).length, notes: c2.warn.length, why: c2.warn.join(" | "), ms: Math.round(ms),
               /* how boxed in the ends are: the other zones' edges within 600 m of the departure / arrival object */
               boxA: S.objects.filter(o => o !== a && o.r > 0 && NAV.inv(a.lat, a.lon, o.lat, o.lon).s < o.r + 600).length,
               boxB: S.objects.filter(o => o !== b && o.r > 0 && NAV.inv(b.lat, b.lon, o.lat, o.lon).s < o.r + 600).length });
  }
  S.objects = keep.o; S.route = keep.r; compute();
  const pre = document.createElement("pre"); pre.id = "testout"; pre.textContent = JSON.stringify(res); document.body.appendChild(pre);
}

const APP = {
  start() {
    $("tabs").innerHTML = TABS.map(([k, t]) => `<button class="tab" data-tab="${k}">${t}</button>`).join("");
    document.querySelectorAll(".tab").forEach(b => b.onclick = () => showTab(b.dataset.tab));
    TFILES.init();
    if (Module._api_private()) {                              /* a private build: never to be published */
      const b = document.createElement("div");
      b.textContent = "PRIVATE BUILD (your private tide data compiled in): for this computer only, do not publish";
      b.style.cssText = "background:#c0392b;color:#fff;font-weight:700;padding:6px 16px;text-align:center";
      document.body.prepend(b);
    }
    header();
    routeTab();
    const h = location.hash.match(/tab=(\w+)/);
    showTab(h ? h[1] : S.tab);
    const ex = location.hash.match(/ex=(\w+)/);                /* #ex=field or #ex=cape: an example, for demos and tests */
    if (ex && ex[1] === "field") { S.objects = []; exampleField(); }
    if (ex && ex[1] === "cape") { const b = document.getElementById("wpex"); if (b) b.click(); }
    if (/mode=globe/.test(location.hash) && S.view.mode !== "globe") $("mode").click();
    if (/test=avoid/.test(location.hash)) selfTest();
    const vt = location.hash.match(/voy=([\d.]+)/);              /* #voy=0.5: the voyage half-way (demos) */
    if (vt) { VOY.on = true; VOY.t = Math.min(1, +vt[1]); const p = VOY.at(); if (p) { S.view.lat0 = p.lat; S.view.lon0 = p.lon; clampView(); } draw(); voyUI(); }
    const wq = location.hash.match(/wps=([-\d.,;]+)/);           /* #wps=26.5,50.8;25,52.2: waypoints (tests) */
    if (wq) { S.route.wps = wq[1].split(";").map(p => { const [a, b] = p.split(",").map(Number); return { lat: a, lon: b, name: "", obj: null }; });
              S.route.sail = 0; S.route.split = 0; refresh(); fitRoute(); draw(); }
    const ps = location.hash.match(/pass=([^&]+)/);                 /* #pass=Alpha,Delta: a passage by names (demos) */
    if (ps) { const [x, y] = decodeURIComponent(ps[1]).split(","), f = n => (S.objects.find(o => o.name === n) || {}).id; passage(f(x), f(y)); }
    const sp = location.hash.match(/spd=([\d.]+)/);               /* #spd=480: the speed (demos) */
    if (sp) { S.route.speed = +sp[1]; refresh(); }
    const zm = location.hash.match(/zoom=([\d.]+)/);              /* #zoom=6: closer (demos) */
    if (zm) { S.view.k *= +zm[1]; clampView(); draw(); }
  },
  route: () => ROUTE.plan.rows,
  download, fmt: FMT, nav: NAV, state: S, parsePos, tfiles: TFILES, streams: STREAMS, loadCoast, arrowPath, rateCol, showTab,
};
