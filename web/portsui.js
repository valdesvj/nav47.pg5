/* portsui.js - NAV47 on the web: port to port. The world's ports (NGA's World Port Index, web/ports.js) and the sea
 * lanes (Eurostat SeaRoute's network, web/sealanes.js): the shortest way between ports along the lanes, through Suez
 * and Panama or round the capes, a voyage of several ports with days in port, the fuel (t/day at a service speed, as
 * the cube of the speed) and the ETAs; drawn on the Route tab's chart, the ports a layer of their own; the voyage into
 * the Route tab as waypoints. For planning, not for navigation. */
"use strict";
const PORTSUI = (() => {
  const KEY = "nav47web:ports";
  const P0 = { stops: [], stops_days: {}, kn: 13, v0: 13, tpd: 30, tport: 5, etd: null, use: { suez: true, panama: true, northwest: false, northeast: false },
               layer: true, sel: -1 };
  let P = Object.assign({}, P0, JSON.parse(localStorage.getItem(KEY) || "{}"));
  P.use = Object.assign({}, P0.use, P.use || {});
  const save = () => localStorage.setItem(KEY, JSON.stringify(P));
  const NMm = 1852;

  /* ---------------------------------------------------------------- the data, decoded once */
  let PT = null, NET = null;
  function ports() {
    /* name: the common one first (Dubai), wpi: the index's (Dubayy), aka: every other name and the UN/LOCODE, searched */
    if (!PT) PT = (typeof PORTS === "string" ? PORTS : "").split("\n").filter(Boolean).map((l, i) => {
      const [wpi, cc, la, lo, no, aka] = l.split("|"), al = (aka || "").split(";").filter(Boolean);
      const common = al.find(a => !/^[A-Z]{5}$/.test(a)) || wpi;
      return { i, name: common, wpi, aka: al, cc, lat: +la, lon: +lo, no: +no, key: [wpi, ...al].join(" ").toLowerCase() };
    });
    return PT;
  }
  const dec = (b64, T) => { const s = atob(b64), u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return new T(u.buffer); };
  function net() {
    if (NET || typeof SEALANES === "undefined") return NET;
    const nd = dec(SEALANES.nodes, Int32Array), ed = dec(SEALANES.edges, Uint32Array), cd = dec(SEALANES.codes, Uint8Array);
    const n = nd.length / 2, m = ed.length / 3, lat = new Float64Array(n), lon = new Float64Array(n), deg = new Int32Array(n + 1);
    for (let i = 0; i < n; i++) { lat[i] = nd[2 * i] / 1e6; lon[i] = nd[2 * i + 1] / 1e6; }
    for (let e = 0; e < m; e++) { deg[ed[3 * e] + 1]++; deg[ed[3 * e + 1] + 1]++; }
    for (let i = 0; i < n; i++) deg[i + 1] += deg[i];
    const adj = new Int32Array(2 * m), fill = deg.slice(0, n);
    for (let e = 0; e < m; e++) { adj[fill[ed[3 * e]]++] = e; adj[fill[ed[3 * e + 1]]++] = e; }
    NET = { n, m, lat, lon, ed, cd, deg, adj, pass: SEALANES.pass };
    return NET;
  }
  /* the node nearest a position (the network's own points; a port joins the lanes there) */
  function nearest(la, lo) {
    const N = net(); let best = -1, bd = Infinity; const c = Math.cos(la * Math.PI / 180);
    for (let i = 0; i < N.n; i++) {
      let dl = Math.abs(N.lon[i] - lo); if (dl > 180) dl = 360 - dl;
      const d = (N.lat[i] - la) ** 2 + (dl * c) ** 2; if (d < bd) { bd = d; best = i; }
    }
    return best;
  }
  /* Dijkstra (a binary heap) from node a to b, the passages not used dropped: {m metres, nodes, passes used} */
  function shortest(a, b, use) {
    const N = net(), dist = new Float64Array(N.n).fill(Infinity), prev = new Int32Array(N.n).fill(-1), pe = new Int32Array(N.n).fill(-1);
    const off = new Uint8Array(N.pass.length); N.pass.forEach((p, k) => { if (k && use[p] === false) off[k] = 1; });
    const H = [], push = (d, v) => { H.push([d, v]); let i = H.length - 1; while (i) { const p = (i - 1) >> 1; if (H[p][0] <= H[i][0]) break; [H[p], H[i]] = [H[i], H[p]]; i = p; } };
    const pop = () => { const t = H[0], l = H.pop(); if (H.length) { H[0] = l; let i = 0; for (;;) { const a = 2 * i + 1, b2 = a + 1; let s = i; if (a < H.length && H[a][0] < H[s][0]) s = a; if (b2 < H.length && H[b2][0] < H[s][0]) s = b2; if (s === i) break; [H[s], H[i]] = [H[i], H[s]]; i = s; } } return t; };
    dist[a] = 0; push(0, a);
    while (H.length) {
      const [d, u] = pop(); if (d > dist[u]) continue; if (u === b) break;
      for (let k = N.deg[u]; k < N.deg[u + 1]; k++) {
        const e = N.adj[k]; if (off[N.cd[e]]) continue;
        const v = N.ed[3 * e] === u ? N.ed[3 * e + 1] : N.ed[3 * e], nd = d + N.ed[3 * e + 2];
        if (nd < dist[v]) { dist[v] = nd; prev[v] = u; pe[v] = e; push(nd, v); }
      }
    }
    if (!isFinite(dist[b])) return null;
    const nodes = [], passes = new Set();
    for (let v = b; v >= 0; v = prev[v]) { nodes.unshift(v); if (pe[v] >= 0 && N.cd[pe[v]]) passes.add(N.pass[N.cd[pe[v]]]); }
    return { m: dist[b], nodes, passes: [...passes] };
  }
  /* port to port: the lanes between their nearest nodes, joined to the ports by straight legs; the points [lat, lon] */
  function leg(A, B, use) {
    const a = nearest(A.lat, A.lon), b = nearest(B.lat, B.lon), N = net();
    const r = a === b ? { m: 0, nodes: [a], passes: [] } : shortest(a, b, use);
    if (!r) return null;
    const pts = [[A.lat, A.lon], ...r.nodes.map(v => [N.lat[v], N.lon[v]]), [B.lat, B.lon]];
    const m = r.m + NAV.inv(A.lat, A.lon, N.lat[a], N.lon[a]).s + NAV.inv(N.lat[b], N.lon[b], B.lat, B.lon).s;
    return { m, pts, passes: r.passes };
  }
  /* the miles of a leg inside the Emission Control Areas (eca.js), by area, in 5 NM steps; the stretches inside (drawn) */
  function ecaOf(pts) {
    const r = { m: 0, by: {}, approx: false, segs: [] };
    if (typeof ecaAt !== "function") return r;
    let run = null;
    for (let i = 1; i < pts.length; i++) {
      const [a1, o1] = pts[i - 1], [a2, o2] = pts[i], s0 = NAV.inv(a1, o1, a2, o2), n = Math.max(1, Math.ceil(s0.s / 9260));
      for (let k = 0; k < n; k++) {
        const p0 = NAV.dir(a1, o1, s0.a1, s0.s * k / n), p1 = NAV.dir(a1, o1, s0.a1, s0.s * (k + 1) / n), pm = NAV.dir(a1, o1, s0.a1, s0.s * (k + 0.5) / n);
        const z = ecaAt(pm.lat, nearLon(pm.lon, 0));
        if (z) { const d = s0.s / n; r.m += d; r.by[z.name] = (r.by[z.name] || 0) + d; if (!z.exact) r.approx = true;
          if (!run) { run = [[p0.lat, p0.lon]]; r.segs.push(run); } run.push([p1.lat, p1.lon]); }
        else run = null;
      }
    }
    return r;
  }
  /* the voyage: each leg, and for a canal used, the way without it */
  let V = null, VK = "";
  function compute() {
    const L = ports(), st = P.stops.map(i => L[i]).filter(Boolean), key = JSON.stringify([P.stops, P.use]);
    if (V && key === VK) return;                              /* the same ports and passages: the same voyage */
    VK = key; V = { legs: [], m: 0 };
    if (st.length < 2 || !net()) return;
    for (let k = 1; k < st.length; k++) {
      const g = leg(st[k - 1], st[k], P.use);
      if (!g) { V.legs.push({ from: st[k - 1], to: st[k], none: true }); continue; }
      const alt = {};
      for (const c of ["suez", "panama"]) if (g.passes.includes(c)) { const w = leg(st[k - 1], st[k], Object.assign({}, P.use, { [c]: false })); alt[c] = w ? w.m : null; }
      V.legs.push(Object.assign({ from: st[k - 1], to: st[k], alt, eca: ecaOf(g.pts) }, g)); V.m += g.m;
    }
  }
  /* Douglas-Peucker on the route (in degrees, longitudes unrolled): its turning points within tol degrees */
  /* does the great circle a -> b cross land (Natural Earth's coastline, as the Route tab's Check)? */
  function overLand(a, b) {
    if (typeof landCrossings !== "function") return false;
    const L = NAV.inv(a[0], a[1], b[0], b[1]).s, n = Math.max(2, Math.min(400, Math.ceil(L / 2000)));
    return landCrossings(NAV.geodPts(a[0], a[1], b[0], b[1], n), () => false).length > 0;
  }
  function thin(pts, tol) {
    const u = pts.map(([a, b], i) => [a, i ? nearLon(b, pts[i - 1][1]) : b]);
    const keep = new Uint8Array(u.length); keep[0] = keep[u.length - 1] = 1;
    /* Douglas-Peucker; a shortcut over land is split too (the lanes' own segments through a canal stay: no shorter piece) */
    const rec = (i, j) => { if (j - i < 2) return; let mx = 0, k = -1; const [y1, x1] = u[i], [y2, x2] = u[j], L2 = (x2 - x1) ** 2 + (y2 - y1) ** 2;
      for (let q = i + 1; q < j; q++) { const [y, x] = u[q]; const t = L2 ? Math.max(0, Math.min(1, ((x - x1) * (x2 - x1) + (y - y1) * (y2 - y1)) / L2)) : 0;
        const d = Math.hypot(x - x1 - t * (x2 - x1), y - y1 - t * (y2 - y1)); if (d >= mx) { mx = d; k = q; } }
      if (mx > tol || overLand(pts[i], pts[j])) { keep[k] = 1; rec(i, k); rec(k, j); } };
    rec(0, u.length - 1);
    return pts.filter((p, i) => keep[i]);
  }

  /* ---------------------------------------------------------------- the card */
  let find = "";
  const fmtD = d => { const h = Math.round(d * 24); return `${Math.floor(h / 24)}d ${String(h % 24).padStart(2, "0")}h`; };
  function card() {
    const el = document.getElementById("portcard"); if (!el) return;
    if (!net()) { el.innerHTML = "<h2>Port to port</h2><div class='meta'>The sea lanes are not loaded (web/sealanes.js).</div>"; return; }
    compute();
    const L = ports(), f = find.trim().toLowerCase();
    const hits = f.length >= 2 ? L.filter(p => p.key.includes(f) || (p.cc || "").toLowerCase() === f).slice(0, 12) : [];
    const also = p => p.wpi !== p.name ? ` <span class="meta">(${esc(p.wpi)})</span>` : "";
    const etd = P.etd || (S.live ? Date.now() / 864e5 + 2440587.5 : S.jd), off = S.times === "zt" ? Math.round(S.pos[1] / 15) : 0;
    let t = etd, fuel = 0, rows = "", ecaM = 0, ecaT = 0, approx = false;
    V.legs.forEach((g, k) => {
      const stay = k ? (P.stops_days || {})[k] || 0 : 0;
      t += stay; fuel += stay * P.tport;
      if (g.none) { rows += `<tr><td>${esc(g.from.name)} → ${esc(g.to.name)}: <b>no way</b> with the passages chosen</td></tr>`; return; }
      const nm = g.m / NMm, days = nm / P.kn / 24, f2 = days * P.tpd * (P.kn / P.v0) ** 3;
      const dep = t; t += days; fuel += f2;
      const E = g.eca, ef = g.m ? E.m / g.m : 0; ecaM += E.m; ecaT += f2 * ef; approx = approx || E.approx;
      const via = g.passes.filter(p => p !== "dover" && p !== "gibraltar" && p !== "malacca").map(p => p[0].toUpperCase() + p.slice(1)).join(", ");
      const alt = Object.entries(g.alt).map(([c, m]) => m ? `without ${c[0].toUpperCase() + c.slice(1)} ${Math.round(m / NMm).toLocaleString()} NM (+${Math.round((m - g.m) / NMm).toLocaleString()})` : `no way without ${c}`).join(" · ");
      /* a leg in two lines (the card is narrow): the way and its miles; the days, the fuel, departure and arrival */
      rows += `<tr><td style="white-space:normal"><div><b>${k + 1}</b> ${esc(g.from.name)} → ${esc(g.to.name)} · <b>${Math.round(nm).toLocaleString()} NM</b>${via ? ` <span class="meta">via ${via}</span>` : ""}</div>
        <div class="meta" style="white-space:normal">${fmtD(days)} at sea · ${f2.toFixed(0)} t · ${FMT.dt(dep, off)} → ${FMT.dt(t, off)} ${off ? "ZT" : "UT"}</div>${E.m > 0 ? `<div style="white-space:normal;color:#15803d">ECA ${Math.round(E.m / NMm).toLocaleString()} NM${E.approx ? " (approx.)" : ""}: ${Object.entries(E.by).map(([n, m]) => `${esc(n)} ${Math.round(m / NMm).toLocaleString()}`).join(", ")} · low-sulphur ${(f2 * ef).toFixed(0)} t</div>` : ""}${alt ? `<div class="meta">${alt}</div>` : ""}</td></tr>`;
    });
    const sea = V.m / NMm / P.kn / 24;
    el.innerHTML = `<h2>Port to port <span class="meta">the sea lanes between the world's ports: for planning, not for navigation</span></h2>
      <div class="controls"><input id="pfind" placeholder="find a port: Dubai, Dubayy, AEDXB; a country: SA" value="${esc(find)}" style="width:20em">
        <label><input type="checkbox" id="player" ${P.layer ? "checked" : ""}> ports on the chart</label>
        <button id="pcsv" title="the world's ports for the calculator: /NAV47/PORTS.CSV (its CHART shows them)">PORTS.CSV</button></div>
      ${hits.length ? `<div class="chips" style="margin-top:4px">${hits.map(p => `<button class="small" data-padd="${p.i}" title="${p.lat.toFixed(3)}, ${p.lon.toFixed(3)}">＋ ${esc(p.name)}${also(p)} <span class="meta">${esc(p.cc)}</span></button>`).join("")}</div>` : ""}
      <table style="margin-top:6px"><tr><th>#</th><th>Port</th><th class="num">days in port</th><th></th></tr>
      ${P.stops.map((i, k) => `<tr><td>${k + 1}</td><td>${L[i] ? esc(L[i].name) + also(L[i]) : "?"} <span class="meta">${esc(L[i] ? L[i].cc : "")}</span></td>
        <td class="num">${k && k < P.stops.length - 1 ? `<input class="num" style="width:4em" type="number" min="0" step="0.5" data-pdays="${k}" value="${(P.stops_days || {})[k] || 0}">` : ""}</td>
        <td style="white-space:nowrap"><button class="small" data-pup="${k}">↑</button><button class="small" data-pdn="${k}">↓</button><button class="small" data-px="${k}">✕</button></td></tr>`).join("")}</table>
      <div class="controls" style="margin-top:6px"><label>Speed <input class="num" id="pkn" type="number" min="1" step="0.5" value="${P.kn}"> kn</label>
        <label>fuel <input class="num" id="ptpd" type="number" min="0" step="1" value="${P.tpd}"> t/day at <input class="num" id="pv0" type="number" min="1" step="0.5" value="${P.v0}"> kn</label>
        <label>in port <input class="num" id="ptport" type="number" min="0" step="0.5" value="${P.tport}"> t/day</label></div>
      <div class="controls" style="margin-top:6px"><label>ETD <input id="petd" type="datetime-local" value="${P.etd ? new Date((P.etd - 2440587.5) * 864e5).toISOString().slice(0, 16) : ""}"> UT</label><span class="meta">(empty: now)</span>
        ${["suez", "panama", "northwest", "northeast"].map(c => `<label><input type="checkbox" data-puse="${c}" ${P.use[c] !== false ? "checked" : ""}> ${{ suez: "Suez", panama: "Panama", northwest: "NW passage", northeast: "NE passage" }[c]}</label>`).join("")}</div>
      ${V.legs.length ? `<table style="margin-top:8px;width:100%">${rows}
        <tr><td style="background:var(--chip);white-space:normal"><div><b>Voyage · ${Math.round(V.m / NMm).toLocaleString()} NM</b></div>
        <div style="white-space:normal"><b>${fmtD(sea)}</b> at sea, ${fmtD(t - etd)} in all · <b>${fuel.toFixed(0)} t</b> · arrives <b>${FMT.dt(t, off)} ${off ? "ZT" : "UT"}</b></div>${ecaM > 0 ? `<div style="white-space:normal;color:#15803d">in ECAs <b>${Math.round(ecaM / NMm).toLocaleString()} NM</b> (${Math.round(ecaM / V.m * 100)} %)${approx ? ", some approximate" : ""} · low-sulphur <b>${ecaT.toFixed(0)} t</b> of ${fuel.toFixed(0)} at sea + in port</div>` : ""}</td></tr></table>
        <div class="controls" style="margin-top:6px"><button id="pfit">Fit the voyage</button><button id="proute" title="the voyage's turning points into the Route tab as your waypoints (the planner off): then To the calculator as ROUTE.TXT">To the route</button><button id="pclr">Clear</button><span class="meta" id="pmsg"></span></div>` :
        `<div class="meta" style="margin-top:6px">Two ports or more: find them above, or click a port on the chart (the ports' layer on).</div>`}
      <div class="meta">The ports: NGA's World Port Index (public domain). The lanes: Eurostat SeaRoute's maritime network (EUPL-1.2), from Oak Ridge National Laboratory's Global Shipping Lane Network. Distances along the lanes, geodesic (WGS84); a published distance table may differ by a few percent. Fuel: t/day × (speed / service speed)³. ECAs (green on the chart): MARPOL Annex VI; Baltic, North Sea, Mediterranean (from May 2025) by their defining lines; North America and the US Caribbean approximate (±30 NM): check the official coordinates.</div>`;
    bind(el);
  }
  function upd(fit) { save(); card(); if (fit) fitVoyage(); else draw(); }
  function bind(el) {
    const $ = id => document.getElementById(id);
    $("pfind").oninput = e => { find = e.target.value; const c = e.target.selectionStart; card(); const i = $("pfind"); i.focus(); i.setSelectionRange(c, c); };
    $("player").onchange = e => { P.layer = e.target.checked; upd(); };
    $("pcsv").onclick = () => download("PORTS.CSV", "name,lat,lon,country\n" + ports().map(p => `${p.name.replace(/[,;"]/g, " ").slice(0, 20)},${p.lat.toFixed(5)},${p.lon.toFixed(5)},${p.cc}`).join("\n") + "\n");
    el.querySelectorAll("[data-padd]").forEach(b => b.onclick = () => { P.stops.push(+b.dataset.padd); find = ""; upd(P.stops.length >= 2); });
    el.querySelectorAll("[data-px]").forEach(b => b.onclick = () => { P.stops.splice(+b.dataset.px, 1); P.stops_days = {}; upd(true); });
    el.querySelectorAll("[data-pup]").forEach(b => b.onclick = () => { const k = +b.dataset.pup; if (k > 0) { [P.stops[k - 1], P.stops[k]] = [P.stops[k], P.stops[k - 1]]; upd(true); } });
    el.querySelectorAll("[data-pdn]").forEach(b => b.onclick = () => { const k = +b.dataset.pdn; if (k < P.stops.length - 1) { [P.stops[k + 1], P.stops[k]] = [P.stops[k], P.stops[k + 1]]; upd(true); } });
    el.querySelectorAll("[data-pdays]").forEach(i => i.onchange = e => { P.stops_days = P.stops_days || {}; P.stops_days[+i.dataset.pdays] = Math.max(0, +e.target.value || 0); upd(); });
    el.querySelectorAll("[data-puse]").forEach(i => i.onchange = e => { P.use[i.dataset.puse] = e.target.checked; upd(true); });
    const num = (id, k, min) => { const e = $(id); if (e) e.onchange = () => { const v = parseFloat(e.value); if (isFinite(v) && v >= min) P[k] = v; upd(); }; };
    num("pkn", "kn", 0.5); num("pv0", "v0", 0.5); num("ptpd", "tpd", 0); num("ptport", "tport", 0);
    if ($("petd")) $("petd").onchange = e => { const t = Date.parse(e.target.value + "Z"); P.etd = isFinite(t) ? t / 864e5 + 2440587.5 : null; upd(); };
    if ($("pfit")) $("pfit").onclick = fitVoyage;
    if ($("pclr")) $("pclr").onclick = () => { P.stops = []; P.stops_days = {}; upd(); };
    if ($("proute")) $("proute").onclick = toRoute;
  }

  /* ---------------------------------------------------------------- the chart: the voyage, the ports' layer */
  function fitVoyage() {
    if (!V || !V.legs.length) return;
    const pts = V.legs.filter(g => !g.none).flatMap(g => g.pts).map(([lat, lon]) => ({ lat, lon }));
    if (pts.length) { fit(pts); draw(); }
  }
  function drawLayer(g) {                                      /* called by the chart's draw() */
    const L = ports();
    if (P.layer) {
      const span = spanDeg(), many = span > 40;
      g.fillStyle = "#3b6ea5"; g.font = "11px system-ui";
      for (const p of L) {
        const [x, y, v] = proj(p.lat, p.lon); if (!v || x < -5 || y < -5 || x > CH.W + 5 || y > CH.H + 5) continue;
        if (many) { g.fillRect(x - 1, y - 1, 2, 2); continue; }
        g.beginPath(); g.arc(x, y, 2.5, 0, 2 * Math.PI); g.fill();
        if (span < 12) placeLabel(p.name, x, y, false);
      }
    }
    if (V && V.legs.length) {
      g.strokeStyle = "#c2410c"; g.lineWidth = 3;
      for (const l of V.legs) if (!l.none) polyline(l.pts);
      g.strokeStyle = "#16a34a"; g.lineWidth = 4;                /* inside an ECA */
      for (const l of V.legs) if (!l.none && l.eca) for (const sg of l.eca.segs) polyline(sg);
      g.fillStyle = "#c2410c"; g.font = "bold 12px system-ui";
      const st = P.stops.map(i => L[i]).filter(Boolean);
      st.forEach((p, k) => { const [x, y, v] = proj(p.lat, p.lon); if (!v) return; g.beginPath(); g.arc(x, y, 5, 0, 2 * Math.PI); g.fill(); placeLabel(`${k + 1} ${p.name}`, x, y, true); });
    }
  }
  /* a click on the chart (the move tool, nothing else under it): the nearest port within 8 px joins the voyage */
  function click(x, y) {
    if (!P.layer) return false;
    let best = null, bd = 8;
    for (const p of ports()) { const [a, b, v] = proj(p.lat, p.lon); if (!v) continue; const d = Math.hypot(a - x, b - y); if (d < bd) { bd = d; best = p; } }
    if (!best) return false;
    P.stops.push(best.i); upd(P.stops.length === 2);
    return true;
  }
  /* the voyage into the Route tab: its turning points (within ~1 NM), named by port; your waypoints, the planner off */
  let lastTol = 0;
  function toRoute() {
    if (!V || !V.legs.length) return;
    /* the turning points within 1', widened until the voyage fits the calculator's 100 waypoints (the tolerance said) */
    let wps = [], tol = 1 / 60;
    for (let it = 0; it < 12; it++, tol *= 1.6) {
      wps = [];
      V.legs.forEach((g, k) => {
        if (g.none) return;
        const t = thin(g.pts, tol);
        t.forEach(([lat, lon], q) => {
          if (k && q === 0) return;
          const name = q === 0 ? g.from.name : q === t.length - 1 ? g.to.name : "";
          wps.push({ lat, lon: nearLon(lon, 0), name: name.slice(0, 20), obj: null });
        });
      });
      if (wps.length <= 99) break;
    }
    lastTol = tol;
    S.route.wps = wps; S.route.avoid = false; S.route.sail = 1; S.route.split = 0; S.route.speed = P.kn;
    S.route.name = P.stops.map(i => ports()[i].name).filter(Boolean).join(" - ").slice(0, 40);
    if (P.etd) S.route.etd = P.etd;
    refresh(); fitRoute(); draw();
    const m = document.getElementById("pmsg");
    if (m) m.textContent = `${wps.length} waypoints into the Route tab: the lanes' turning points, within ${(lastTol * 60).toFixed(1)}′ of them.`;
  }
  return { card, drawLayer, click, fitVoyage, ports, net, leg, shortest, nearest, state: () => P };
})();
