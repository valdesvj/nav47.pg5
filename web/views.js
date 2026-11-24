/* views.js - NAV47 on the web: three planning views beside the Route tab, on the same chart.
 *  - Flight: airport to airport (OurAirports, public domain: airports.js), great circles, times and ETAs.
 *  - Helicopter: from Tanajib or Ras Tanura to the field's helidecks (your platforms named ...ACP..., the
 *    accommodation platforms, and your rigs) and back: the shortest round trip, refuelled on an ACP when the
 *    endurance needs it.
 *  - Supply: a supply vessel from Tanajib or Ras Tanura to your platforms and back: the shortest schedule, the
 *    time alongside each, ETAs; To the route plans its legs round the zones.
 * The platforms are yours (the Objects card: loaded in this browser, never sent anywhere): nothing of them here.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
"use strict";
const VIEWS = (() => {
  const $ = id => document.getElementById(id), St = () => APP.state, NMm = 1852;
  const keep = (k, d) => { try { const v = localStorage.getItem("nav47web:" + k); return v === null ? d : Object.assign(d, JSON.parse(v)); } catch (e) { return d; } };
  const put = (k, v) => { try { localStorage.setItem("nav47web:" + k, JSON.stringify(v)); } catch (e) { /* full */ } };
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const AP = AIRPORTS.split("\n").map(l => { const f = l.split("|"); return { id: f[0], iata: f[1], name: f[2], city: f[3], cc: f[4], lat: +f[5], lon: +f[6], kind: f[7] }; });
  const apById = new Map(AP.map(a => [a.id, a]));
  const leg = (a, b) => { const q = APP.nav.inv(a.lat, a.lon, b.lat, b.lon); return { nm: q.s / NMm, crs: (q.a1 + 360) % 360 }; };
  const off = () => zd(), lab = () => (zd() ? "ZT" : "UT");
  const etdOf = v => v ? v : nowJd();
  const isoOf = jd => new Date(msOf(jd)).toISOString().slice(0, 16);
  const hm = min => FMT.dur(min / 60);
  /* the departure: the field always shows it; "now" (lit) follows the clock, a time typed fixes it */
  const depHtml = (p, o) => `<label>Departure UT <input type="datetime-local" id="${p}etd" value="${isoOf(etdOf(o.etd))}" step="60"></label>
    <button class="small ${o.etd ? "" : "on"}" id="${p}now" title="the departure now: it follows the clock">now</button>`;
  function depBind(p, o, upd) {
    $(p + "etd").onchange = e => { const t = Date.parse(e.target.value + "Z"); o.etd = isFinite(t) ? jdOf(t) : null; $(p + "now").classList.toggle("on", !o.etd); upd(); };
    $(p + "now").onclick = () => { o.etd = null; $(p + "etd").value = isoOf(nowJd()); $(p + "now").classList.add("on"); upd(); };
  }

  /* ---------------------------------------------------------------- the order of the stops
     a round trip from node 0 (the base) through 1..n and back: Held-Karp, exact, to 13 stops; more: the nearest
     neighbour, then 2-opt. c(i, j) the cost of a leg. Returns the stops' order (1..n). */
  function bestOrder(n, c) {
    if (n <= 1) return n ? [1] : [];
    if (n <= 13) {
      const N = 1 << n, D = new Float64Array(N * n).fill(Infinity), P = new Int8Array(N * n).fill(-1);
      for (let j = 0; j < n; j++) D[(1 << j) * n + j] = c(0, j + 1);
      for (let m = 1; m < N; m++) for (let j = 0; j < n; j++) {
        const d = D[m * n + j]; if (!(m & (1 << j)) || d === Infinity) continue;
        for (let k = 0; k < n; k++) {
          if (m & (1 << k)) continue;
          const m2 = m | (1 << k), v = d + c(j + 1, k + 1);
          if (v < D[m2 * n + k]) { D[m2 * n + k] = v; P[m2 * n + k] = j; }
        }
      }
      let best = Infinity, last = -1, m = N - 1;
      for (let j = 0; j < n; j++) { const v = D[m * n + j] + c(j + 1, 0); if (v < best) { best = v; last = j; } }
      const out = [];
      while (last >= 0) { out.push(last + 1); const p = P[m * n + last]; m &= ~(1 << last); last = p; }
      return out.reverse();
    }
    const left = new Set(Array.from({ length: n }, (_, i) => i + 1)), t = [0];
    while (left.size) { let b = -1, bv = Infinity; for (const k of left) { const v = c(t[t.length - 1], k); if (v < bv) { bv = v; b = k; } } t.push(b); left.delete(b); }
    t.push(0);
    for (let again = true, it = 0; again && it < 200; it++) {
      again = false;
      for (let i = 1; i < t.length - 2; i++) for (let k = i + 1; k < t.length - 1; k++) {
        const d = c(t[i - 1], t[k]) + c(t[i], t[k + 1]) - c(t[i - 1], t[i]) - c(t[k], t[k + 1]);
        if (d < -1e-9) { t.splice(i, k - i + 1, ...t.slice(i, k + 1).reverse()); again = true; }
      }
    }
    return t.slice(1, -1);
  }

  /* ---------------------------------------------------------------- the chart's layer (app.js draw() calls it) */
  function mark(p, txt, col, square) {
    const [x, y, v] = proj(p.lat, p.lon); if (!v) return;
    const g = CH.g;
    g.fillStyle = col; g.strokeStyle = css("--card"); g.lineWidth = 2;
    g.beginPath(); square ? g.rect(x - 6, y - 6, 12, 12) : g.arc(x, y, 6, 0, 2 * Math.PI); g.fill(); g.stroke();
    if (txt && St().labels !== "off") { g.fillStyle = col; g.font = "bold 12px system-ui"; placeLabel(txt, x, y, true); }
  }
  function path(list, col, w) {
    const g = CH.g; g.strokeStyle = col; g.lineWidth = w;
    for (let i = 1; i < list.length; i++) polyline(geodLine(list[i - 1], list[i]));
  }
  function drawLayer(g) {
    if (S2.tab === "flight") flightLayer(g);
    else if (S2.tab === "heli") heliLayer(g);
    else if (S2.tab === "supply") supplyLayer(g);
    simLayer(g);
  }
  const S2 = { tab: "" };

  /* ================================================================ Airways
     one network from the free sources: the FAA's US airways (airways_us.js, built in, loaded with the Flight tab) and
     your own files in X-Plane's format (earth_awy.dat with earth_fix.dat and earth_nav.dat: a simulator's or
     Navigraph's, worldwide; kept in this browser's IndexedDB, never sent anywhere). A source: { src, pts "id|lat|lon",
     awy "name:i,j;k,l|..." }. A leg: the departure to an airway point within 100 NM, the airways by the shortest way
     (Dijkstra, A*), a point within 100 NM to the arrival; none: direct. Planning only, as an airline's planner would
     not: no SIDs and STARs, no directions of the one-way routes in the FAA's data, no levels. */
  const AW = { srcs: [], lat: null, lon: null, id: [], adj: null, names: [], grid: null, built: false, file: null };
  const RE = 3440.065, D2 = Math.PI / 180;
  const hav = (la1, lo1, la2, lo2) => { const a = Math.sin((la2 - la1) * D2 / 2) ** 2 + Math.cos(la1 * D2) * Math.cos(la2 * D2) * Math.sin((lo2 - lo1) * D2 / 2) ** 2; return 2 * RE * Math.asin(Math.min(1, Math.sqrt(a))); };
  function awBuild() {
    const lat = [], lon = [], id = [], edges = [], names = [], seen = new Map();
    for (const S of AW.srcs) {
      const base = lat.length, P = S.pts.split("\n");
      for (const l of P) { const f = l.split("|"); id.push(f[0]); lat.push(+f[1]); lon.push(+f[2]); }
      /* the same point in two sources (an X-Plane file over the FAA's): joined, by its name within 0.5 NM */
      for (let i = base; i < lat.length; i++) {
        const k = id[i] + "@" + Math.round(lat[i] * 20) + "," + Math.round(lon[i] * 20), j = seen.get(k);
        if (j !== undefined && hav(lat[i], lon[i], lat[j], lon[j]) < 0.5) edges.push([i, j, -1]); else seen.set(k, i);
      }
      for (const a of S.awy ? S.awy.split("|") : []) {
        const c = a.indexOf(":"), nm = a.slice(0, c), n = names.length; names.push(nm);
        for (const seq of a.slice(c + 1).split(";")) { const q = seq.split(",").map(Number); for (let i = 1; i < q.length; i++) edges.push([base + q[i - 1], base + q[i], n]); }
      }
    }
    const n = lat.length, adj = Array.from({ length: n }, () => []);
    for (const [a, b, k] of edges) { const w = k < 0 ? 0 : hav(lat[a], lon[a], lat[b], lon[b]); adj[a].push(b, w, k); adj[b].push(a, w, k); }
    const grid = new Map();
    for (let i = 0; i < n; i++) { const g = Math.floor(lat[i]) + "," + Math.floor(lon[i]); (grid.get(g) || grid.set(g, []).get(g)).push(i); }
    Object.assign(AW, { lat: Float64Array.from(lat), lon: Float64Array.from(lon), id, adj, names, grid, built: true });
  }
  function awNear(p, max) {                                  /* the airway points within max NM, nearest first (8) */
    const out = [], r = Math.ceil(max / 60) + 1, la0 = Math.floor(p.lat), lo0 = Math.floor(p.lon);
    for (let a = -r; a <= r; a++) for (let b = -r - 1; b <= r + 1; b++) {
      const c = AW.grid.get((la0 + a) + "," + (lo0 + b)); if (!c) continue;
      for (const i of c) { const d = hav(p.lat, p.lon, AW.lat[i], AW.lon[i]); if (d <= max) out.push([d, i]); }
    }
    return out.sort((x, y) => x[0] - y[0]).slice(0, 8);
  }
  function awRoute(A, B) {                                   /* { pts, nm, via } or null */
    if (!AW.built || !AW.lat.length) return null;
    const sa = awNear(A, 100), sb = awNear(B, 100); if (!sa.length || !sb.length) return null;
    const n = AW.lat.length, dist = new Float64Array(n).fill(Infinity), prev = new Int32Array(n).fill(-1), via = new Int32Array(n).fill(-1);
    const end = new Map(sb.map(([d, i]) => [i, d])), h = i => hav(AW.lat[i], AW.lon[i], B.lat, B.lon);
    const heap = [], push = (f, i) => { heap.push([f, i]); let k = heap.length - 1; while (k > 0) { const p = (k - 1) >> 1; if (heap[p][0] <= heap[k][0]) break; [heap[p], heap[k]] = [heap[k], heap[p]]; k = p; } };
    const pop = () => { const top = heap[0], last = heap.pop(); if (heap.length) { heap[0] = last; let k = 0; for (;;) { const l = 2 * k + 1, r = l + 1; let m = k; if (l < heap.length && heap[l][0] < heap[m][0]) m = l; if (r < heap.length && heap[r][0] < heap[m][0]) m = r; if (m === k) break; [heap[m], heap[k]] = [heap[k], heap[m]]; k = m; } } return top; };
    for (const [d, i] of sa) { dist[i] = d; push(d + h(i), i); }
    let best = Infinity, bestI = -1, steps = 0;
    while (heap.length && steps++ < 400000) {
      const [f, i] = pop(); if (f >= best) break; const d = dist[i]; if (f - h(i) > d + 1e-9) continue;
      if (end.has(i) && d + end.get(i) < best) { best = d + end.get(i); bestI = i; }
      const L = AW.adj[i];
      for (let k = 0; k < L.length; k += 3) { const j = L[k], nd = d + L[k + 1]; if (nd < dist[j]) { dist[j] = nd; prev[j] = i; via[j] = L[k + 2]; push(nd + h(j), j); } }
    }
    if (bestI < 0) return null;
    const pts = [], aw = [];
    for (let i = bestI; i >= 0; i = prev[i]) { pts.push({ lat: AW.lat[i], lon: AW.lon[i], name: AW.id[i] }); if (via[i] >= 0) aw.push(AW.names[via[i]]); }
    pts.reverse(); aw.reverse();
    const names = aw.filter((x, k) => k === 0 || x !== aw[k - 1]);
    const all = [A].concat(pts.filter((p, k) => k === 0 || p.lat !== pts[k - 1].lat || p.lon !== pts[k - 1].lon), [B]);
    let nm = 0; for (let k = 1; k < all.length; k++) nm += leg(all[k - 1], all[k]).nm;
    return { pts: all, nm, via: names, first: pts[0].name, last: pts[pts.length - 1].name };
  }
  /* the sources: the FAA's built in, your file from IndexedDB */
  function idb(op, val) {
    return new Promise(res => {
      const r = indexedDB.open("nav47web", 1);
      r.onupgradeneeded = () => r.result.createObjectStore("kv");
      r.onerror = () => res(null);
      r.onsuccess = () => { const t = r.result.transaction("kv", op === "get" ? "readonly" : "readwrite").objectStore("kv");
        const q = op === "get" ? t.get("airways") : op === "put" ? t.put(val, "airways") : t.delete("airways"); q.onsuccess = () => res(q.result); q.onerror = () => res(null); };
    });
  }
  let awLoading = null;
  function awLoad() {
    if (awLoading) return awLoading;
    awLoading = new Promise(res => {
      const sc = document.createElement("script"); sc.src = "airways_us.js?v=1.0";
      sc.onload = sc.onerror = () => res();
      document.head.append(sc);
    }).then(() => idb("get")).then(f => {
      AW.srcs = (typeof AWY_US !== "undefined" ? [AWY_US] : []).concat(f ? [f] : []); AW.file = f || null;
      awBuild();
    });
    return awLoading;
  }
  /* X-Plane's files (format 1100 / 1101 / 1150, and the older): the airways' ends found by ident, region, kind */
  function xplane(files) {
    const pos = new Map(), byId = new Map(), add = (k, id, la, lo) => { pos.set(k, [la, lo]); (byId.get(id) || byId.set(id, []).get(id)).push([la, lo]); };
    let awyText = "", name = "";
    for (const f of files) {
      const lines = f.text.split(/\r?\n/);
      const kind = /earth_awy|awy/i.test(f.name) ? "awy" : /earth_fix|fix/i.test(f.name) ? "fix" : /earth_nav|nav/i.test(f.name) ? "nav" : "";
      if (kind === "awy") { awyText = f.text; name = f.name; continue; }
      for (const l of lines) {
        const t = l.trim().split(/\s+/); if (t.length < 3 || t[0] === "99") continue;
        if (kind === "fix") { const la = +t[0], lo = +t[1]; if (!isFinite(la) || !isFinite(lo)) continue; add(t[2] + "|" + (t[4] || "") + "|11", t[2], la, lo); }
        else if (kind === "nav") { const ty = +t[0]; if (![2, 3, 12, 13].includes(ty)) continue; const la = +t[1], lo = +t[2]; if (!isFinite(la)) continue;
          const id = t[7], rg = t.length > 10 ? t[9] : "", k = ty === 2 ? 2 : 3; const key = id + "|" + rg + "|" + k; if (ty >= 12 && pos.has(key)) continue; add(key, id, la, lo); }
      }
    }
    if (!awyText) return { err: "no earth_awy.dat among the files" };
    const pts = [], idx = new Map(), seqs = new Map(); let lost = 0, segs = 0;
    const pt = (id, rg, ty) => {
      const k = id + "|" + rg + "|" + ty; let p = pos.get(k);
      if (!p) { const c = byId.get(id); if (c && c.length === 1) p = c[0]; }
      if (!p) return -1;
      const key = id + "@" + p[0].toFixed(4) + "," + p[1].toFixed(4);
      if (!idx.has(key)) { idx.set(key, pts.length); pts.push(id + "|" + p[0].toFixed(5) + "|" + p[1].toFixed(5)); }
      return idx.get(key);
    };
    for (const l of awyText.split(/\r?\n/)) {
      const t = l.trim().split(/\s+/); if (t.length < 10 || t[0] === "99") continue;
      const a = pt(t[0], t[1], +t[2]), b = pt(t[3], t[4], +t[5]); if (a < 0 || b < 0) { lost++; continue; }
      for (const nm of t[t.length - 1].split("-")) { (seqs.get(nm) || seqs.set(nm, []).get(nm)).push(a + "," + b); segs++; }
    }
    const awy = [...seqs].map(([n, l]) => n + ":" + l.join(";")).join("|");
    return { src: "your file " + name + " (" + segs + " segments" + (lost ? ", " + lost + " without a position" : "") + ")", pts: pts.join("\n"), awy, n: seqs.size, pn: pts.length };
  }

  /* ================================================================ Flight */
  const F = keep("flight", { ids: [], kn: 450, turn: 60, etd: null, awy: true });
  function apMatch(q) {
    q = q.trim().toLowerCase(); if (q.length < 2) return [];
    const u = q.toUpperCase(), score = a => a.id === u || a.iata === u ? 0 : a.name.toLowerCase().startsWith(q) || a.city.toLowerCase().startsWith(q) ? 1 : 2;
    return AP.filter(a => a.kind !== "H" && (a.id === u || a.iata === u || a.name.toLowerCase().includes(q) || a.city.toLowerCase().includes(q)))
      .sort((a, b) => score(a) - score(b) || (a.kind > b.kind ? 1 : -1)).slice(0, 8);
  }
  const apName = a => `${a.iata || a.id} ${a.name}${a.city && !a.name.includes(a.city) ? ", " + a.city : ""} ${a.cc}`;
  function flightCard() {
    const el = $("flcard"); if (!el) return;
    const h = location.hash.match(/fl=([\w,]+)/);              /* #fl=DXB,LHR,JFK: a flight from the address (demos, pictures) */
    if (h && !flightCard.done) {
      flightCard.done = true;
      const ids = h[1].split(",").map(c => { c = c.toUpperCase(); const a = AP.find(a => a.iata === c && a.kind !== "H") || apById.get(c); return a && a.id; }).filter(Boolean);
      if (ids.length) { F.ids = ids; put("flight", F); }
    }
    el.innerHTML = `<h2>Flight <span class="meta">airport to airport, great circles · OurAirports (public domain)</span></h2>
      <div class="controls"><input id="flq" placeholder="find an airport: Dubai, OMDB, DXB" style="width:20em" autocomplete="off"></div>
      <div id="flres" class="chips"></div>
      <div class="scroll" style="margin-top:6px"><table id="flroute"></table></div>
      <div class="controls" style="margin-top:6px"><label><input type="checkbox" id="flawy" ${F.awy ? "checked" : ""}> by airways</label>
        <span class="meta" id="flsrc"></span></div>
      <div class="controls"><label title="X-Plane's earth_awy.dat with earth_fix.dat and earth_nav.dat (a simulator's, or Navigraph's): kept in this browser only">Your airways <input type="file" id="flfile" multiple accept=".dat,.txt"></label>
        <button class="small" id="flforget" ${AW.file ? "" : "disabled"}>Forget my file</button></div>
      <div class="controls" style="margin-top:6px">
        <label>Cruise <input class="num" id="flkn" type="number" min="60" max="700" step="5" value="${F.kn}"> kn</label>
        <label>on the ground <input class="num" id="flturn" type="number" min="0" max="1440" step="5" value="${F.turn}"> min</label>
        ${depHtml("fl", F)}</div>
      ${simHtml()}
      <div class="controls" style="margin-top:6px"><button id="flfit">Fit the flight</button><button id="flrev">Reverse</button><button id="flclr">Clear</button></div>
      <div class="meta">Great circles at the cruise speed: no airways, winds or climb; for planning, not for navigation.</div>`;
    $("flq").oninput = () => {
      $("flres").innerHTML = apMatch($("flq").value).map(a => `<button class="small" data-fa="${a.id}" title="${esc(a.name)} ${a.lat.toFixed(2)}, ${a.lon.toFixed(2)}">＋ ${esc(apName(a))}</button>`).join("");
      $("flres").querySelectorAll("[data-fa]").forEach(b => b.onclick = () => { F.ids.push(b.dataset.fa); $("flq").value = ""; $("flres").innerHTML = ""; flightUpd(true); $("flq").focus(); });
    };
    $("flkn").onchange = e => { F.kn = Math.max(60, Math.min(700, +e.target.value || 450)); flightUpd(); };
    $("flturn").onchange = e => { F.turn = Math.max(0, +e.target.value || 0); flightUpd(); };
    depBind("fl", F, flightUpd);
    const srcLine = () => { if ($("flsrc")) $("flsrc").textContent = AW.built ? `${AW.srcs.map(s => s.src).join(" · ")}: ${AW.names.length} airways, ${AW.lat.length} points` : "the airways: loading…"; if ($("flforget")) $("flforget").disabled = !AW.file; };
    srcLine();
    awLoad().then(() => { srcLine(); flightTable(); draw(); });
    $("flawy").onchange = e => { F.awy = e.target.checked; flightUpd(); };
    $("flfile").onchange = e => Promise.all([...e.target.files].map(f => f.text().then(text => ({ name: f.name, text })))).then(fs => {
      const x = xplane(fs);
      if (x.err) { $("flsrc").textContent = "⛔ " + x.err + ": choose earth_awy.dat, earth_fix.dat and earth_nav.dat together"; return; }
      const keepIt = { src: x.src, pts: x.pts, awy: x.awy };
      idb("put", keepIt).then(() => { AW.file = keepIt; AW.srcs = (typeof AWY_US !== "undefined" ? [AWY_US] : []).concat([keepIt]); awBuild(); srcLine(); flightUpd(); });
    });
    $("flforget").onclick = () => idb("del").then(() => { AW.file = null; AW.srcs = typeof AWY_US !== "undefined" ? [AWY_US] : []; awBuild(); srcLine(); flightUpd(); });
    $("flfit").onclick = fitView;
    $("flrev").onclick = () => { F.ids.reverse(); flightUpd(); };
    $("flclr").onclick = () => { F.ids = []; flightUpd(); };
    simBind();
    flightTable();
  }
  function flightUpd(fit) { put("flight", F); flightTable(); if (fit && F.ids.length >= 2) fitView(); else draw(); }
  /* the flight's legs: by airways where there are (and AW loaded), else great circles */
  let legsKey = "", legsVal = null;
  function flightLegs() {                                    /* kept until the airports, the option or the sources change */
    const key = F.ids.join(",") + "|" + F.awy + "|" + (AW.built ? AW.lat.length + ":" + AW.srcs.length : "-");
    if (key === legsKey && legsVal) return legsVal;
    legsKey = key; legsVal = flightLegs0(); return legsVal;
  }
  function flightLegs0() {
    const A = F.ids.map(id => apById.get(id)).filter(Boolean), out = [];
    for (let i = 1; i < A.length; i++) {
      const gc = leg(A[i - 1], A[i]), r = F.awy ? awRoute(A[i - 1], A[i]) : null;
      out.push({ a: A[i - 1], b: A[i], gc: gc.nm, crs: gc.crs, nm: r ? r.nm : gc.nm, pts: r ? r.pts : [A[i - 1], A[i]], via: r ? r.via : null, r });
    }
    return out;
  }
  function flightTable() {
    const A = F.ids.map(id => apById.get(id)).filter(Boolean);
    $("flroute").innerHTML = `<tr><th>#</th><th>Airport</th><th></th></tr>` + A.map((a, i) => `<tr><td>${i + 1}</td><td title="${FMT.pos(a.lat, a.lon)}">${esc(apName(a))}</td>
      <td><button class="small" data-fu="${i}">↑</button><button class="small" data-fd="${i}">↓</button><button class="small" data-fx="${i}">✕</button></td></tr>`).join("");
    $("flroute").querySelectorAll("[data-fu]").forEach(b => b.onclick = () => { const i = +b.dataset.fu; if (i > 0) { [F.ids[i - 1], F.ids[i]] = [F.ids[i], F.ids[i - 1]]; flightUpd(); } });
    $("flroute").querySelectorAll("[data-fd]").forEach(b => b.onclick = () => { const i = +b.dataset.fd; if (i < F.ids.length - 1) { [F.ids[i + 1], F.ids[i]] = [F.ids[i], F.ids[i + 1]]; flightUpd(); } });
    $("flroute").querySelectorAll("[data-fx]").forEach(b => b.onclick = () => { F.ids.splice(+b.dataset.fx, 1); flightUpd(); });
    if (A.length < 2) { $("fltab").innerHTML = `<div class="meta">Two airports or more: find them above, or click them on the chart.</div>`; return; }
    let t = etdOf(F.etd), nm = 0, gcs = 0, air = 0, rows = "";
    const L = flightLegs();
    L.forEach((l, i) => {
      const h = l.nm / F.kn, dep = t; t += h / 24; nm += l.nm; gcs += l.gc; air += h;
      const how = !F.awy ? "great circle" : l.r ? `<span title="${esc(l.r.via.join(" "))}">${esc(l.r.first)} ${esc(l.r.via.slice(0, 6).join(" "))}${l.r.via.length > 6 ? " …" : ""} ${esc(l.r.last)}</span>` : `<span class="meta">direct: no airway within 100 NM</span>`;
      rows += `<tr><td>${esc(l.a.iata || l.a.id)} → ${esc(l.b.iata || l.b.id)}</td><td style="white-space:normal;max-width:22em">${how}</td><td class="num mono">${l.nm.toFixed(0)}</td><td class="num mono">${l.gc.toFixed(0)}</td>
        <td class="num mono">${FMT.dur(h)}</td><td class="num mono">${FMT.dt(dep, off())}</td><td class="num mono">${FMT.dt(t, off())}</td></tr>`;
      if (i < L.length - 1) t += F.turn / 1440;
    });
    $("fltab").innerHTML = `<table><tr><th>Leg</th><th>Route</th><th class="num">NM</th><th class="num">GC NM</th><th class="num">Flight</th><th class="num">Departs ${lab()}</th><th class="num">Arrives ${lab()}</th></tr>${rows}
      <tr><td><b>Total</b></td><td></td><td class="num mono"><b>${nm.toFixed(0)}</b></td><td class="num mono">${gcs.toFixed(0)}</td><td class="num mono"><b>${FMT.dur(air)}</b></td><td></td><td class="num mono"><b>${FMT.dt(t, off())}</b></td></tr></table>
      ${F.awy ? `<div class="meta">${nm > gcs + 0.5 ? `By airways ${(nm - gcs).toFixed(0)} NM (${((nm / gcs - 1) * 100).toFixed(1)} %) longer than the great circles. ` : ""}Airways: ${AW.srcs.map(s => esc(s.src)).join(" · ") || "loading…"}.</div>` : ""}`;
  }
  function flightLayer(g) {
    const V = St().view, many = V.k > 900;                    /* zoomed in: the medium airports too */
    g.fillStyle = css("--soft");
    for (const a of AP) {
      if (a.kind === "H" || (a.kind === "M" && !many)) continue;
      const [x, y, v] = proj(a.lat, a.lon); if (!v || x < 0 || y < 0 || x > CH.W || y > CH.H) continue;
      g.fillRect(x - 1.5, y - 1.5, 3, 3);
    }
    const A = F.ids.map(id => apById.get(id)).filter(Boolean), L = flightLegs();
    if (F.awy) { g.setLineDash([4, 5]); path(A, css("--soft"), 1.3); g.setLineDash([]); }   /* the great circles, to compare */
    for (const l of L) {
      path(l.pts, css("--track"), 3);
      if (l.r && St().labels === "all") for (const p of l.pts.slice(1, -1)) { const [x, y, v] = proj(p.lat, p.lon); if (v) { g.fillStyle = css("--track"); g.fillRect(x - 2, y - 2, 4, 4); } }
    }
    A.forEach((a, i) => mark(a, `${i + 1} ${a.iata || a.id}`, css("--track"), true));
  }
  function flightClick(x, y) {
    let best = null, bd = 10;
    for (const a of AP) {
      if (a.kind === "H") continue;
      const [ax, ay, v] = proj(a.lat, a.lon); if (!v) continue;
      const d = Math.hypot(ax - x, ay - y); if (d < bd) { bd = d; best = a; }
    }
    if (!best) return false;
    F.ids.push(best.id); flightUpd(); return true;
  }

  /* ================================================================ Helicopter */
  const BASES = () => AP.filter(a => a.cc === "SA" && /tanajib|tanura/i.test(a.name + " " + a.city));
  const H = keep("heli", { base: "OETN", stops: [], kn: 140, endur: 150, reserve: 30, deck: 10, refuel: 20, mine: false, etd: null, q: "" });
  const isAcp = o => o.type === "platform" && /ACP/i.test(o.name);
  const heliOk = o => isAcp(o) || o.type === "rig";
  function heliPlan() {
    const base = apById.get(H.base) || apById.get("OETN"), objs = St().objects, find = id => objs.find(o => o.id === id);
    const stops = H.stops.map(find).filter(Boolean);
    if (!base || !stops.length) return null;
    const N = [base, ...stops], tm = (a, b) => leg(a, b).nm / H.kn * 60;   /* minutes */
    const C = N.map(a => N.map(b => tm(a, b)));
    const order0 = H.mine ? stops.map((_, i) => i + 1) : bestOrder(stops.length, (i, j) => C[i][j]);
    const fuelPts = [{ p: base, base: true }].concat(objs.filter(isAcp).map(o => ({ p: o })));
    const usable = H.endur - H.reserve;
    const nearestFuel = q => Math.min(...fuelPts.map(f => tm(q, f.p)));
    function fly(order) {                                    /* the order flown: refuelled where it must be */
      const seq = [base, ...order.map(i => N[i]), base], legs = [];
      let fuel = usable, ok = true, refuels = 0;
      for (let i = 0; i < seq.length - 1; i++) {
        const a = seq[i], b = seq[i + 1], tb = tm(a, b), atBase = i === 0;
        const canFuel = atBase || (a !== base && isAcp(a));
        const need = tb + (i + 1 === seq.length - 1 ? 0 : nearestFuel(b));
        let fueled = false;
        if (fuel < need && canFuel && !atBase) { fuel = usable; fueled = true; refuels++; }
        if (fuel < need) {                                    /* a detour to the best ACP on the way */
          let best = null, bv = Infinity;
          for (const f of fuelPts) {
            if (f.base || f.p === a || f.p === b) continue;
            const t1 = tm(a, f.p), t2 = tm(f.p, b) + (i + 1 === seq.length - 1 ? 0 : nearestFuel(b));
            if (t1 <= fuel && t2 <= usable && t1 + tm(f.p, b) < bv) { bv = t1 + tm(f.p, b); best = f.p; }
          }
          if (best) {
            legs.push({ a, b: best, t: tm(a, best), nm: leg(a, best).nm, fueledAt: fueled, detour: true });
            fuel = usable - 0; refuels++;
            legs.push({ a: best, b, t: tm(best, b), nm: leg(best, b).nm, refuelStop: true });
            fuel -= tm(best, b); continue;
          }
          ok = false;
        }
        legs.push({ a, b, t: tb, nm: leg(a, b).nm, fueledAt: fueled });
        fuel -= tb;
        if (fuel < 0) ok = false;
      }
      const air = legs.reduce((s, l) => s + l.t, 0), nm = legs.reduce((s, l) => s + l.nm, 0);
      const total = air + stops.length * H.deck + refuels * H.refuel;
      return { legs, air, nm, total, refuels, ok };
    }
    let r = fly(order0);
    if (!H.mine) { const r2 = fly(order0.slice().reverse()); if (r2.ok && (!r.ok || r2.total < r.total - 1e-6)) r = r2; }
    r.base = base; r.stops = stops;
    return r;
  }
  function heliCard() {
    const el = $("hecard"); if (!el) return;
    const objs = St().objects, cand = objs.filter(heliOk), bases = BASES();
    if (!bases.some(b => b.id === H.base)) H.base = "OETN";
    el.innerHTML = `<h2>Helicopter <span class="meta">the field's helidecks: your ACP platforms (accommodation, a helideck) and rigs</span></h2>
      <div class="controls"><label>Base <select id="hebase">${bases.map(b => `<option value="${b.id}" ${b.id === H.base ? "selected" : ""}>${esc(b.name)} (${b.id})</option>`).join("")}</select></label>
        <label><input type="radio" name="heord" value="short" ${H.mine ? "" : "checked"}> shortest</label><label><input type="radio" name="heord" value="mine" ${H.mine ? "checked" : ""}> my order</label></div>
      ${cand.length ? `<div class="controls" style="margin-top:6px"><input id="heq" placeholder="filter: a name, rig…" value="${esc(H.q || "")}" style="width:12em">
        <button class="small" id="heall">all shown</button><button class="small" id="henone">none</button></div>
        <div id="hecand" class="chips" style="max-height:150px;overflow:auto"></div>`
      : `<div class="meta" style="margin-top:6px">No helideck yet: load your platforms (Route tab, Objects card: a CSV with the ACP platforms, the accommodation
        platforms, by name) or add your rigs as objects of the type Rig.</div>`}
      <div class="controls" style="margin-top:6px">
        <label>Cruise <input class="num" id="hekn" type="number" min="60" max="200" step="5" value="${H.kn}"> kn</label>
        <label>Endurance <input class="num" id="heend" type="number" min="30" max="400" step="5" value="${H.endur}"> min</label>
        <label>reserve <input class="num" id="heres" type="number" min="0" max="120" step="5" value="${H.reserve}"> min</label>
        <label>on deck <input class="num" id="hedeck" type="number" min="0" max="120" step="1" value="${H.deck}"> min</label>
        <label>refuel <input class="num" id="heref" type="number" min="0" max="120" step="1" value="${H.refuel}"> min</label>
        ${depHtml("he", H)}</div>
      ${simHtml()}
      <div class="controls" style="margin-top:6px"><button id="hefit">Fit</button><button id="heclr">Clear the stops</button></div>
      <div class="meta">The order of least flying (exact up to 13 stops), back to the base; every ACP refuels: a refuelling where the
        endurance less the reserve would not reach the next deck and then fuel. Great circles, no wind: for planning.</div>`;
    const chips = () => {
      const q = (H.q || "").toLowerCase(), list = cand.filter(o => !q || o.name.toLowerCase().includes(q) || OBJ[o.type].label.toLowerCase().includes(q));
      $("hecand").innerHTML = list.map(o => `<label class="chip" style="cursor:pointer"><input type="checkbox" data-hs="${o.id}" ${H.stops.includes(o.id) ? "checked" : ""}> ${esc(o.name)}${o.type === "rig" ? " (rig)" : ""}</label>`).join("") || `<span class="meta">none matches</span>`;
      $("hecand").querySelectorAll("[data-hs]").forEach(c => c.onchange = () => { const id = c.dataset.hs; if (c.checked) { if (!H.stops.includes(id)) H.stops.push(id); } else H.stops = H.stops.filter(s => s !== id); heliUpd(); });
      return list;
    };
    if (cand.length) {
      chips();
      $("heq").oninput = e => { H.q = e.target.value; chips(); };
      $("heall").onclick = () => { const l = chips(); for (const o of l) if (!H.stops.includes(o.id)) H.stops.push(o.id); chips(); heliUpd(); };
      $("henone").onclick = () => { H.stops = []; chips(); heliUpd(); };
    }
    $("hebase").onchange = e => { H.base = e.target.value; heliUpd(); };
    document.querySelectorAll("[name=heord]").forEach(r => r.onchange = () => { H.mine = r.value === "mine" && r.checked; heliUpd(); });
    const num = (id, k, lo, hi) => { $(id).onchange = e => { H[k] = Math.max(lo, Math.min(hi, +e.target.value || 0)); heliUpd(); }; };
    num("hekn", "kn", 60, 200); num("heend", "endur", 30, 400); num("heres", "reserve", 0, 120); num("hedeck", "deck", 0, 120); num("heref", "refuel", 0, 120);
    depBind("he", H, heliUpd);
    $("hefit").onclick = fitView;
    $("heclr").onclick = () => { H.stops = []; heliCard(); heliUpd(); };
    simBind();
    heliTable();
  }
  function heliUpd() { put("heli", Object.assign({}, H)); heliTable(); draw(); }
  function heliTable() {
    const el = $("hetab"); if (!el) return;
    const r = heliPlan();
    if (!r) { el.innerHTML = `<div class="meta">Choose the decks to visit (ticked: ${H.stops.length}).</div>`; return; }
    const nm = x => x.name || x.id;
    let t = etdOf(H.etd), rows = "", k = 0;
    for (const l of r.legs) {
      const dep = t; t += l.t / 1440;
      const at = l.b === r.base ? "base" : l.detour ? `refuel ${H.refuel} min` : `on deck ${H.deck} min${l.b && isAcp(l.b) && r.legs[r.legs.indexOf(l) + 1] && r.legs[r.legs.indexOf(l) + 1].fueledAt ? ` + refuel ${H.refuel} min` : ""}`;
      rows += `<tr${l.detour ? ' style="color:var(--sun)"' : ""}><td>${l.b === r.base ? "" : l.detour ? "⛽" : ++k}</td><td>${esc(nm(l.a))} → <b>${esc(nm(l.b))}</b></td>
        <td class="num mono">${l.nm.toFixed(1)}</td><td class="num mono">${hm(l.t)}</td><td class="num mono">${FMT.dt(dep, off())}</td><td class="num mono">${FMT.dt(t, off())}</td><td>${at}</td></tr>`;
      if (l.b !== r.base) t += (l.detour ? H.refuel : H.deck) / 1440;
      const nx = r.legs[r.legs.indexOf(l) + 1]; if (nx && nx.fueledAt) t += H.refuel / 1440;
    }
    el.innerHTML = `${r.ok ? "" : `<div class="chip warn">⛔ out of range: a leg longer than the endurance less the reserve, or no ACP to refuel on the way</div>`}
      <div class="scroll" style="max-height:360px"><table><tr><th>#</th><th>Leg</th><th class="num">NM</th><th class="num">Flight</th><th class="num">Lifts ${lab()}</th><th class="num">Lands ${lab()}</th><th></th></tr>${rows}</table></div>
      <div style="margin-top:6px"><b>${r.stops.length} decks · ${r.nm.toFixed(1)} NM · flying ${hm(r.air)}</b> · in all ${hm(r.total)}
        (on deck ${hm(r.stops.length * H.deck)}${r.refuels ? `, ${r.refuels} refuelling${r.refuels > 1 ? "s" : ""} ${hm(r.refuels * H.refuel)}` : ""}) · back ${FMT.dt(t, off())} ${lab()}</div>`;
    VIEWS.last = r;
  }
  function heliLayer(g) {
    const r = heliPlan(), base = apById.get(H.base);
    for (const o of St().objects) if (heliOk(o)) { const [x, y, v] = proj(o.lat, o.lon); if (v) { g.strokeStyle = css("--accent"); g.lineWidth = 1.5; g.beginPath(); g.arc(x, y, 9, 0, 2 * Math.PI); g.stroke(); } }
    if (base) mark(base, base.name.replace(/ Airport$/, ""), css("--now"), true);
    if (!r) return;
    for (const l of r.legs) path([l.a, l.b], l.detour || l.refuelStop ? css("--sun") : css("--track"), 3);
    let k = 0;
    r.legs.forEach((l, i) => {                                /* ⛽: refuelled there (a detour, or on a deck visited) */
      if (l.b === r.base) return;
      const fuel = l.detour || (r.legs[i + 1] && r.legs[i + 1].fueledAt);
      mark(l.b, l.detour ? `⛽ ${l.b.name}` : `${++k} ${l.b.name}${fuel ? " ⛽" : ""}`, fuel ? css("--sun") : css("--track"));
    });
  }
  function objClick(x, y, ok, toggle) {
    let best = null, bd = 12;
    for (const o of St().objects) { if (!ok(o)) continue; const [ox, oy, v] = proj(o.lat, o.lon); if (!v) continue; const d = Math.hypot(ox - x, oy - y); if (d < bd) { bd = d; best = o; } }
    if (!best) return false;
    toggle(best.id); return true;
  }

  /* ================================================================ Supply */
  const SBASES = [{ id: "tj", name: "Tanajib (marine port)", lat: 27.76995, lon: 48.87706 }, { id: "rt", name: "Ras Tanura", lat: 26.63333, lon: 50.16667 }];
  const U = keep("supply", { base: "tj", stops: [], hrs: {}, kn: 12, along: 2, mine: false, etd: null, max: 0 });
  const supOk = o => o.type === "platform" || o.type === "rig" || o.type === "spm" || o.type === "wellhead";
  function supplyPlan() {
    const base = SBASES.find(b => b.id === U.base) || SBASES[0], objs = St().objects;
    const stops = U.stops.map(id => objs.find(o => o.id === id)).filter(Boolean);
    if (!stops.length) return null;
    const N = [base, ...stops], C = N.map(a => N.map(b => leg(a, b).nm));
    const order = U.mine ? stops.map((_, i) => i + 1) : bestOrder(stops.length, (i, j) => C[i][j]);
    const alongOf = o => (U.hrs[o.id] > 0 || U.hrs[o.id] === 0) ? U.hrs[o.id] : U.along;
    const legs = []; let trip = 1, tt = 0, prev = 0;
    for (let k = 0; k < order.length; k++) {
      const j = order[k], go = C[prev][j] / U.kn, back = C[j][0] / U.kn;
      if (U.max > 0 && prev !== 0 && tt + go + alongOf(N[j]) + back > U.max) {   /* this trip is full: home, then a new one */
        legs.push({ a: N[prev], b: base, nm: C[prev][0], h: C[prev][0] / U.kn, trip }); trip++; tt = 0; prev = 0; k--; continue;
      }
      legs.push({ a: N[prev], b: N[j], nm: C[prev][j], h: go, trip, along: alongOf(N[j]) });
      tt += go + alongOf(N[j]); prev = j;
    }
    legs.push({ a: N[prev], b: base, nm: C[prev][0], h: C[prev][0] / U.kn, trip });
    const nm = legs.reduce((s, l) => s + l.nm, 0), sea = legs.reduce((s, l) => s + l.h, 0), al = legs.reduce((s, l) => s + (l.along || 0), 0);
    return { base, stops, legs, nm, sea, al, trips: trip };
  }
  function supplyCard() {
    const el = $("sucard"); if (!el) return;
    const n = St().objects.filter(supOk).length;
    el.innerHTML = `<h2>Supply <span class="meta">a supply vessel round your platforms: the shortest schedule</span></h2>
      <div class="controls"><label>Base <select id="subase">${SBASES.map(b => `<option value="${b.id}" ${b.id === U.base ? "selected" : ""}>${b.name}</option>`).join("")}</select></label>
        <label><input type="radio" name="suord" value="short" ${U.mine ? "" : "checked"}> shortest</label><label><input type="radio" name="suord" value="mine" ${U.mine ? "checked" : ""}> my order</label></div>
      ${n ? `<div class="controls" style="margin-top:6px"><input id="suq" placeholder="find a platform (${n})" style="width:14em" autocomplete="off"></div><div id="sures" class="chips"></div>`
          : `<div class="meta" style="margin-top:6px">No platform yet: load yours on the Route tab's Objects card (a CSV), or click them on the chart.</div>`}
      <div class="controls" style="margin-top:6px">
        <label>Speed <input class="num" id="sukn" type="number" min="1" max="30" step="0.5" value="${U.kn}"> kn</label>
        <label>alongside <input class="num" id="sualong" type="number" min="0" max="72" step="0.25" value="${U.along}"> h each</label>
        <label title="0: one trip, however long">a trip at most <input class="num" id="sumax" type="number" min="0" max="240" step="1" value="${U.max}"> h</label>
        ${depHtml("su", U)}</div>
      ${simHtml()}
      <div class="controls" style="margin-top:6px"><button id="sufit">Fit</button><button id="suroute" title="the schedule into the Route tab: its legs planned round the zones, to the calculator as ROUTE.TXT">To the route</button><button id="suclr">Clear the stops</button></div>
      <div class="meta">The order of least steaming (exact up to 13 stops), back to the base; the miles straight from platform to platform
        (To the route: round the zones, a little more). Time alongside per platform in the table.</div>`;
    if (n) $("suq").oninput = () => {
      const q = $("suq").value.trim().toLowerCase();
      const list = q.length < 2 ? [] : St().objects.filter(o => supOk(o) && !U.stops.includes(o.id) && o.name.toLowerCase().includes(q)).slice(0, 12);
      $("sures").innerHTML = list.map(o => `<button class="small" data-sa="${o.id}">＋ ${esc(o.name)}</button>`).join("");
      $("sures").querySelectorAll("[data-sa]").forEach(b => b.onclick = () => { U.stops.push(b.dataset.sa); b.remove(); supplyUpd(); });
    };
    $("subase").onchange = e => { U.base = e.target.value; supplyUpd(); };
    document.querySelectorAll("[name=suord]").forEach(r => r.onchange = () => { U.mine = r.value === "mine" && r.checked; supplyUpd(); });
    $("sukn").onchange = e => { U.kn = Math.max(1, Math.min(30, +e.target.value || 12)); supplyUpd(); };
    $("sualong").onchange = e => { U.along = Math.max(0, +e.target.value || 0); supplyUpd(); };
    $("sumax").onchange = e => { U.max = Math.max(0, +e.target.value || 0); supplyUpd(); };
    depBind("su", U, supplyUpd);
    $("sufit").onclick = fitView;
    $("suclr").onclick = () => { U.stops = []; supplyUpd(); };
    $("suroute").onclick = toRoute;
    simBind();
    supplyTable();
  }
  function supplyUpd() { put("supply", U); supplyTable(); draw(); }
  function supplyTable() {
    const el = $("sutab"); if (!el) return;
    const r = supplyPlan();
    if (!r) { el.innerHTML = `<div class="meta">Add the platforms to supply.</div>`; return; }
    let t = etdOf(U.etd), rows = "", k = 0, lastTrip = 1;
    for (const l of r.legs) {
      if (l.trip !== lastTrip) { rows += `<tr><td colspan="8" class="meta">trip ${l.trip}</td></tr>`; lastTrip = l.trip; }
      const dep = t; t += l.h / 24;
      const stop = l.b !== r.base, arr = t;
      if (stop) t += l.along / 24;
      rows += `<tr><td>${stop ? ++k : ""}</td><td>${esc(l.a.name)} → <b>${esc(l.b.name)}</b></td><td class="num mono">${l.nm.toFixed(1)}</td><td class="num mono">${FMT.dur(l.h)}</td>
        <td class="num mono">${FMT.dt(arr, off())}</td><td>${stop ? `<input class="num" style="width:4.5em" type="number" min="0" step="0.25" data-sh="${l.b.id}" value="${l.along}"> h` : ""}</td>
        <td class="num mono">${stop ? FMT.dt(t, off()) : ""}</td><td>${stop ? `<button class="small" data-sx="${l.b.id}">✕</button>` : ""}</td></tr>`;
    }
    el.innerHTML = `<div class="scroll" style="max-height:380px"><table><tr><th>#</th><th>Leg</th><th class="num">NM</th><th class="num">Steaming</th><th class="num">Arrives ${lab()}</th><th>Alongside</th><th class="num">Leaves ${lab()}</th><th></th></tr>${rows}</table></div>
      <div style="margin-top:6px"><b>${r.stops.length} platforms · ${r.nm.toFixed(1)} NM · steaming ${FMT.dur(r.sea)}</b> · alongside ${FMT.dur(r.al)} · in all
        ${FMT.dur(r.sea + r.al)}${r.trips > 1 ? ` · ${r.trips} trips` : ""} · back ${FMT.dt(t, off())} ${lab()}</div>`;
    el.querySelectorAll("[data-sh]").forEach(i => i.onchange = () => { U.hrs[i.dataset.sh] = Math.max(0, +i.value || 0); supplyUpd(); });
    el.querySelectorAll("[data-sx]").forEach(b => b.onclick = () => { U.stops = U.stops.filter(s => s !== b.dataset.sx); supplyUpd(); });
    VIEWS.last = r;
  }
  function supplyLayer(g) {
    const r = supplyPlan(), base = SBASES.find(b => b.id === U.base);
    if (base) mark(base, base.name, css("--now"), true);
    if (!r) return;
    path(r.legs.flatMap(l => [l.a, l.b]).filter((p, i, a) => i === 0 || p !== a[i - 1]), css("--track"), 3);
    let k = 0;
    for (const l of r.legs) if (l.b !== r.base) mark(l.b, `${++k} ${l.b.name}`, css("--track"));
  }
  function toRoute() {
    const r = supplyPlan(); if (!r) return;
    const R = St().route, wp = p => ({ lat: p.lat, lon: p.lon, name: (p.name || "").slice(0, 12), obj: p.id && p.type ? p.id : null });
    const pts = [r.base].concat(r.legs.map(l => l.b));
    R.wps = pts.map(wp); R.avoid = true; R.taken = null; R.name = ("Supply " + r.base.name).slice(0, 40);
    if (R.speed > 30 || R.speed < 1) R.speed = U.kn; else R.speed = U.kn;
    showTab("route"); refresh(true); fitRoute(); draw();
  }

  /* ---------------------------------------------------------------- the simulation
     each view's trip as a timeline (minutes from the departure): moving legs (great circles) and stays (the ground,
     a deck, alongside); played over SIM.dur seconds, the plane, the helicopter or the vessel on the chart, a box
     with the time, the leg, the position, what is left */
  function timeline() {
    const segs = []; let t = 0, kn = 0, icon = "", t0 = nowJd();
    const move = (a, b, nm, min, label) => { segs.push({ k: "move", a, b, nm, t0: t, t1: t + min, label }); t += min; };
    const stay = (p, min, label) => { if (min > 0) { segs.push({ k: "stay", a: p, b: p, t0: t, t1: t + min, label }); t += min; } };
    const nm = p => p.iata || p.name || p.id;
    if (S2.tab === "flight") {
      const A = F.ids.map(id => apById.get(id)).filter(Boolean); if (A.length < 2) return null;
      icon = "plane"; kn = F.kn; t0 = etdOf(F.etd);
      flightLegs().forEach((l, i, L) => {
        for (let k = 1; k < l.pts.length; k++) {
          const p0 = l.pts[k - 1], p1 = l.pts[k], d = leg(p0, p1).nm;
          move(p0, p1, d, d / F.kn * 60, `${nm(l.a)} → ${nm(l.b)}${l.r ? ` · ${p1.name || nm(p1)}` : ""}`);
        }
        if (i < L.length - 1) stay(l.b, F.turn, `on the ground at ${nm(l.b)}`);
      });
    } else if (S2.tab === "heli") {
      const r = heliPlan(); if (!r) return null;
      icon = "heli"; kn = H.kn; t0 = etdOf(H.etd);
      r.legs.forEach((l, i) => {
        if (l.fueledAt) stay(l.a, H.refuel, `refuelling on ${nm(l.a)}`);
        move(l.a, l.b, l.nm, l.t, `${nm(l.a)} → ${nm(l.b)}`);
        if (l.b !== r.base) stay(l.b, l.detour ? H.refuel : H.deck, l.detour ? `refuelling on ${nm(l.b)}` : `on deck at ${nm(l.b)}`);
      });
    } else if (S2.tab === "supply") {
      const r = supplyPlan(); if (!r) return null;
      icon = "ship"; kn = U.kn; t0 = etdOf(U.etd);
      for (const l of r.legs) { move(l.a, l.b, l.nm, l.h * 60, `${nm(l.a)} → ${nm(l.b)}`); if (l.b !== r.base) stay(l.b, l.along * 60, `alongside ${nm(l.b)}`); }
    }
    return { segs, total: t, kn, icon, t0 };
  }
  const SIM = { f: 0, playing: false, dur: 40, follow: true, last: 0, tab: "" };
  const sid = n => S2.tab + n;                              /* each tab its own player: no id shared with the others */
  function simHtml() {
    return `<div class="controls" style="margin-top:6px"><b>Simulation</b><button id="${sid("simplay")}">${SIM.playing ? "Pause" : "Play"}</button>
      <input type="range" id="${sid("simf")}" min="0" max="1000" value="${Math.round(SIM.f * 1000)}" style="flex:1;min-width:120px">
      <label title="the whole trip played in">in <select id="${sid("simdur")}">${[20, 40, 80, 160].map(d => `<option ${d === SIM.dur ? "selected" : ""}>${d}</option>`).join("")}</select> s</label>
      <label><input type="checkbox" id="${sid("simfollow")}" ${SIM.follow ? "checked" : ""}> follow</label></div>`;
  }
  function simBind() {
    if (SIM.tab !== S2.tab) { SIM.tab = S2.tab; SIM.f = 0; SIM.playing = false; }
    $(sid("simplay")).onclick = () => {
      if (!timeline()) {                                     /* nothing to play: say so, not a silent button */
        const b = $(sid("simplay")); b.textContent = S2.tab === "flight" ? "2 airports first" : S2.tab === "heli" ? "tick the decks first" : "add platforms first";
        setTimeout(() => { if (b.isConnected) b.textContent = "Play"; }, 2500); return;
      } if (SIM.f >= 1) SIM.f = 0; SIM.playing = !SIM.playing; $(sid("simplay")).textContent = SIM.playing ? "Pause" : "Play"; SIM.last = performance.now(); if (SIM.playing) requestAnimationFrame(simStep); };
    $(sid("simf")).oninput = e => { SIM.f = +e.target.value / 1000; draw(); };
    $(sid("simdur")).onchange = e => { SIM.dur = +e.target.value; };
    $(sid("simfollow")).onchange = e => { SIM.follow = e.target.checked; };
  }
  function simStep(now) {
    if (!SIM.playing || S2.tab !== SIM.tab || St().tab !== S2.tab) { SIM.playing = false; return; }
    SIM.f = Math.min(1, SIM.f + (now - SIM.last) / 1000 / SIM.dur); SIM.last = now;
    if ($(sid("simf"))) $(sid("simf")).value = Math.round(SIM.f * 1000);
    if (SIM.f >= 1) { SIM.playing = false; if ($(sid("simplay"))) $(sid("simplay")).textContent = "Play"; }
    draw();
    if (SIM.playing) requestAnimationFrame(simStep);
  }
  function simAt(T, m) {                                     /* where, the course, the segment, at m minutes */
    let s = T.segs.find(x => m <= x.t1) || T.segs[T.segs.length - 1];
    if (s.k === "stay") { const nx = T.segs[T.segs.indexOf(s) + 1], pv = T.segs[T.segs.indexOf(s) - 1];
      const crs = nx && nx.k === "move" ? leg(nx.a, nx.b).crs : pv && pv.k === "move" ? leg(pv.a, pv.b).crs : 0;
      return { lat: s.a.lat, lon: s.a.lon, crs, s }; }
    const q = APP.nav.inv(s.a.lat, s.a.lon, s.b.lat, s.b.lon), f = (m - s.t0) / Math.max(1e-9, s.t1 - s.t0);
    const p = APP.nav.dir(s.a.lat, s.a.lon, q.a1, q.s * Math.max(0, Math.min(1, f)));
    return { lat: p.lat, lon: p.lon, crs: (p.az + 360) % 360, s, f };
  }
  function heliIcon(g, x, y, crs) {
    const a = (crs - 90) * Math.PI / 180, rot = performance.now() / 60;
    g.save(); g.translate(x, y);
    g.fillStyle = "rgba(255,255,255,.85)"; g.strokeStyle = "#d63a2a"; g.lineWidth = 2;   /* a white disc: seen among the platforms */
    g.beginPath(); g.arc(0, 0, 30, 0, 2 * Math.PI); g.fill(); g.stroke();
    g.strokeStyle = "#20262c"; g.lineWidth = 4; g.beginPath(); g.moveTo(0, 0); g.lineTo(-Math.cos(a) * 26, -Math.sin(a) * 26); g.stroke();   /* the tail */
    g.lineWidth = 2; g.beginPath(); g.moveTo(-Math.cos(a) * 26 - Math.sin(a) * 6, -Math.sin(a) * 26 + Math.cos(a) * 6); g.lineTo(-Math.cos(a) * 26 + Math.sin(a) * 6, -Math.sin(a) * 26 - Math.cos(a) * 6); g.stroke();
    g.fillStyle = "#d63a2a"; g.beginPath(); g.ellipse(0, 0, 12, 8, a, 0, 2 * Math.PI); g.fill(); g.strokeStyle = "#20262c"; g.lineWidth = 1.5; g.stroke();
    g.strokeStyle = "rgba(20,25,30,.8)"; g.lineWidth = 3;                                       /* the rotor, turning */
    for (const r of [rot, rot + Math.PI / 2]) { g.beginPath(); g.moveTo(Math.cos(r) * 24, Math.sin(r) * 24); g.lineTo(-Math.cos(r) * 24, -Math.sin(r) * 24); g.stroke(); }
    g.restore();
  }
  function simLayer(g) {
    if (SIM.tab !== S2.tab || (!SIM.playing && SIM.f <= 0)) return;
    const T = timeline(); if (!T || !T.segs.length) return;
    const m = SIM.f * T.total, p = simAt(T, m);
    if (SIM.follow && SIM.playing) {
      const [x, y, v] = proj(p.lat, p.lon);
      if (!v || x < CH.W * 0.2 || x > CH.W * 0.8 || y < CH.H * 0.2 || y > CH.H * 0.8) { St().view.lat0 = p.lat; St().view.lon0 = p.lon; clampView(); requestAnimationFrame(draw); return; }
    }
    const [x, y, v] = proj(p.lat, p.lon);
    if (v) {
      if (T.icon === "heli") heliIcon(g, x, y, p.crs);
      else drawVessel(p.lat, p.lon, p.crs, T.icon === "plane", p.s.k === "stay");
    }
    /* the box */
    const left = T.total - m, now = T.t0 + m / 1440;
    let togo = 0; for (const s of T.segs) if (s.k === "move") togo += s.t1 <= m ? 0 : s.t0 >= m ? s.nm : s.nm * (1 - (m - s.t0) / (s.t1 - s.t0));
    const lines = [`${{ plane: "✈", heli: "🚁", ship: "🚢" }[T.icon]} ${FMT.dt(now, off())} ${lab()}`, p.s.label,
      p.s.k === "move" ? `${FMT.crs(p.crs)} · ${T.kn} kn` : `${hm(p.s.t1 - m)} left here`, FMT.pos(p.lat, p.lon),
      `to go ${togo.toFixed(togo < 100 ? 1 : 0)} NM · ${hm(left)}`, `${T.icon === "plane" ? "arrives" : "back at the base"} ${FMT.dt(T.t0 + T.total / 1440, off())} ${lab()}`];
    g.font = "13px system-ui"; const w = Math.max(...lines.map(t => g.measureText(t).width)) + 20;
    g.fillStyle = css("--card"); g.globalAlpha = 0.92; g.fillRect(10, 10, w, lines.length * 18 + 10); g.globalAlpha = 1;
    g.strokeStyle = css("--line"); g.lineWidth = 1; g.strokeRect(10, 10, w, lines.length * 18 + 10);
    g.fillStyle = css("--ink"); lines.forEach((t, i) => { g.font = i === 0 ? "bold 14px system-ui" : "13px system-ui"; g.fillText(t, 20, 30 + i * 18); });
  }

  /* ---------------------------------------------------------------- the tabs */
  function show(t) {
    S2.tab = t;
    const el = $("t-" + t);
    if (!el.dataset.built) {
      const k = { flight: ["flcard", "fltab", "The flight"], heli: ["hecard", "hetab", "The round trip"], supply: ["sucard", "sutab", "The schedule"] }[t];
      el.innerHTML = `<div class="split"><div><div id="chartslot-${t}"></div><div class="card"><h2>${k[2]}</h2><div id="${k[1]}"></div></div></div>
        <div><div class="card" id="${k[0]}"></div></div></div>`;   /* the schedule wide, under the chart; the settings beside */
      el.dataset.built = "1";
    }
  }
  function card(t) { ({ flight: flightCard, heli: heliCard, supply: supplyCard })[t](); fitView(); }   /* opened: its route in view */
  function table() {
    const a = document.activeElement;                       /* a field being typed in: not redrawn under the fingers */
    if (a && /INPUT|SELECT/.test(a.tagName) && a.closest("#t-" + S2.tab)) return;
    if (S2.tab === "flight") flightTable(); else if (S2.tab === "heli") heliTable(); else if (S2.tab === "supply") supplyTable(); }
  function click(x, y) {
    if (S2.tab === "flight") return flightClick(x, y);
    if (S2.tab === "heli") return objClick(x, y, heliOk, id => { H.stops = H.stops.includes(id) ? H.stops.filter(s => s !== id) : H.stops.concat(id); heliCard(); heliUpd(); });
    if (S2.tab === "supply") return objClick(x, y, supOk, id => { U.stops = U.stops.includes(id) ? U.stops.filter(s => s !== id) : U.stops.concat(id); supplyUpd(); });
    return false;
  }
  function fitView() {
    let pts = [];
    if (S2.tab === "flight") pts = F.ids.map(id => apById.get(id)).filter(Boolean);
    else if (S2.tab === "heli") { const r = heliPlan(), b = apById.get(H.base); pts = (r ? r.legs.map(l => l.b) : []).concat(b ? [b] : []); if (pts.length < 2) pts = pts.concat(St().objects.filter(heliOk)); }
    else if (S2.tab === "supply") { const r = supplyPlan(), b = SBASES.find(x => x.id === U.base); pts = (r ? r.stops : []).concat(b ? [b] : []); }
    if (pts.length >= 2) { fit(pts.map(p => ({ lat: p.lat, lon: p.lon }))); draw(); }   /* one point: the view kept */
  }
  const owns = t => t === "flight" || t === "heli" || t === "supply";
  return { owns, show, card, table, drawLayer, click, fitView, bestOrder, heliPlan, supplyPlan, timeline, sim: SIM, awRoute, awLoad, airways: AW, xplane, airports: () => AP, state: () => ({ F, H, U }), last: null };
})();
