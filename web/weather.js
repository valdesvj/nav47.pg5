/* weather.js - NAV47 on the web: the Weather tab. A chart (the web maps' spherical Mercator, so the radar tiles fit)
 * with the coastline and, when you switch it online:
 *   the forecast, hour by hour (Open-Meteo, free, no key, CC BY 4.0: wind barbs, rain, cloud cover, thunderstorm risk
 *   from the thunderstorm weather codes and CAPE), on a grid over the view;
 *   the observed rain radar of the last 2 hours and its nowcast, and the infrared satellite clouds (RainViewer, free
 *   with attribution), in 10-minute frames.
 * Only the view's grid points go out (never your route, objects or files); off until switched on. */
"use strict";
const WX = (() => {
  const D = Math.PI / 180;
  const st = Object.assign({ online: false, layers: { wind: true, rain: true, cloud: true, storm: true, radar: false, sat: false },
                             mode: "fc", lat0: 25, lon0: 30, k: 230 }, JSON.parse(localStorage.getItem("nav47web:wx") || "{}"));
  const save = () => localStorage.setItem("nav47web:wx", JSON.stringify(st));
  let cv, g, W = 0, H = 0, dpr = 1, built = false;
  let fc = null, fcKey = "", fcBusy = false, fcErr = "", hour = 0, playing = false, timer = null;
  let rv = null, rvErr = "", frame = -1;
  const tiles = new Map();
  const merc = la => Math.log(Math.tan(Math.PI / 4 + Math.max(-85, Math.min(85, la)) * D / 2));
  const latOf = m => (2 * Math.atan(Math.exp(m)) - Math.PI / 2) / D;
  const near = (lo, r) => { while (lo - r > 180) lo -= 360; while (lo - r < -180) lo += 360; return lo; };
  const P = (la, lo) => [W / 2 + st.k * (near(lo, st.lon0) - st.lon0) * D, H / 2 - st.k * (merc(la) - merc(st.lat0))];
  const U = (x, y) => [latOf(merc(st.lat0) + (H / 2 - y) / st.k), near(st.lon0 + (x - W / 2) / st.k / D, 0)];
  const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const clamp = () => { st.k = Math.max(W / (2 * Math.PI), Math.min(st.k, 3e5)); st.lat0 = Math.max(-80, Math.min(80, st.lat0)); st.lon0 = near(st.lon0, 0); };
  const span = () => W / st.k / D;

  /* ---------------------------------------------------------------- the forecast (Open-Meteo) */
  function gridOf() {
    const sp = span(), [n] = U(0, 0), [s] = U(0, H), w = st.lon0 - sp / 2, e = st.lon0 + sp / 2;
    /* the finest spacing that keeps the view within ~260 points (Open-Meteo counts each point as a call) */
    const count = c => (Math.floor(Math.min(80, n) / c) - Math.ceil(Math.max(-80, s) / c) + 1) * (Math.floor(e / c) - Math.ceil(w / c) + 1);
    const cell = [0.1, 0.2, 0.25, 0.5, 1, 2, 2.5, 4, 5, 7.5, 10, 12, 15, 20].find(c => count(c) <= 260) || 20;
    const pts = [];
    for (let la = Math.ceil(Math.max(-80, s) / cell) * cell; la <= Math.min(80, n); la += cell)
      for (let lo = Math.ceil(w / cell) * cell; lo <= e; lo += cell) pts.push([+la.toFixed(3), +near(lo, 0).toFixed(3)]);
    return { cell, pts: pts.slice(0, 300) };
  }
  async function fetchFc() {
    if (!st.online || fcBusy) return;
    const G = gridOf(), key = G.cell + ":" + G.pts.length + ":" + (G.pts[0] || []).join(",") + ":" + Math.floor(Date.now() / 36e5);
    if (key === fcKey) return;
    fcBusy = true; fcErr = ""; info();
    try {
      const out = [];
      for (let i = 0; i < G.pts.length; i += 150) {
        const b = G.pts.slice(i, i + 150);
        const url = "https://api.open-meteo.com/v1/forecast?latitude=" + b.map(p => p[0]).join(",") + "&longitude=" + b.map(p => p[1]).join(",") +
          "&hourly=wind_speed_10m,wind_direction_10m,wind_gusts_10m,precipitation,cloud_cover,cape,weather_code&wind_speed_unit=kn&forecast_hours=72&timezone=GMT";
        const r = await fetch(url);
        if (r.status === 429) throw new Error("Open-Meteo's free limit (600 points a minute, 10 000 a day) is reached: wait a minute, or zoom in");
        if (!r.ok) throw new Error("Open-Meteo " + r.status);
        let j = await r.json(); if (!Array.isArray(j)) j = [j];
        j.forEach((d, k) => out.push({ lat: b[k][0], lon: b[k][1], h: d.hourly }));
      }
      const lats = [...new Set(out.map(p => p.lat))].sort((a, b) => a - b), lons = [...new Set(out.map(p => p.lon))];
      const lon0 = Math.min(...out.map(p => near(p.lon, st.lon0)));
      const grid = new Map(out.map(p => [p.lat + "," + Math.round((near(p.lon, st.lon0) - lon0) / G.cell), p]));
      fc = { cell: G.cell, pts: out, lats, la0: lats[0], lon0, grid, times: out.length ? out[0].h.time.map(t => Date.parse(t + "Z")) : [] };
      fcKey = key;
      const now = Date.now(); hour = Math.max(0, fc.times.findIndex(t => t > now - 36e5));
    } catch (e) { fcErr = String(e.message || e); }
    fcBusy = false; info(); draw();
  }
  let fetchT = null;
  const later = () => { clearTimeout(fetchT); fetchT = setTimeout(() => { fetchFc(); fetchRv(); }, 700); };

  /* ---------------------------------------------------------------- radar and satellite (RainViewer) */
  async function fetchRv() {
    if (!st.online || !(st.layers.radar || st.layers.sat) || (rv && Date.now() - rv.at < 6e5)) return;
    try {
      const r = await fetch("https://api.rainviewer.com/public/weather-maps.json");
      if (!r.ok) throw new Error("RainViewer " + r.status);
      const j = await r.json();
      rv = { at: Date.now(), host: j.host, radar: (j.radar.past || []).concat(j.radar.nowcast || []), nowcast: (j.radar.nowcast || []).length,
             sat: (j.satellite && j.satellite.infrared) || [] };
      frame = (j.radar.past || []).length - 1; rvErr = "";
    } catch (e) { rvErr = String(e.message || e); }
    info(); draw();
  }
  function tile(url) {
    let t = tiles.get(url);
    if (!t) { t = new Image(); t.crossOrigin = "anonymous"; t.onload = () => draw(); t.onerror = () => { t.bad = true; }; t.src = url; tiles.set(url, t);
      if (tiles.size > 600) tiles.delete(tiles.keys().next().value); }
    return t;
  }
  function drawTiles(path, scheme) {
    const z = Math.max(0, Math.min(7, Math.round(Math.log2(st.k * 2 * Math.PI / 256)))), n = 2 ** z, sz = st.k * 2 * Math.PI / n;
    const [la0] = U(0, 0), [la1] = U(0, H), lo0 = st.lon0 - span() / 2;
    const ty0 = Math.max(0, Math.floor((1 - merc(la0) / Math.PI) / 2 * n)), ty1 = Math.min(n - 1, Math.floor((1 - merc(la1) / Math.PI) / 2 * n));
    const tx0 = Math.floor((lo0 + 180) / 360 * n), tx1 = Math.floor((lo0 + span() + 180) / 360 * n);
    g.globalAlpha = scheme === "sat" ? 0.55 : 0.75;
    for (let ty = ty0; ty <= ty1; ty++) for (let tx = tx0; tx <= tx1; tx++) {
      const x = ((tx % n) + n) % n, lo = tx / n * 360 - 180, mt = Math.PI * (1 - 2 * ty / n);
      const px = W / 2 + st.k * (lo - st.lon0) * D, py = H / 2 - st.k * (mt - merc(st.lat0));
      const im = tile(`${rv.host}${path}/256/${z}/${x}/${ty}/${scheme === "sat" ? "0/0_0" : "2/1_1"}.png`);
      if (im.complete && !im.bad && im.naturalWidth) g.drawImage(im, px, py, sz + 0.5, sz + 0.5);
    }
    g.globalAlpha = 1;
  }

  /* ---------------------------------------------------------------- drawing */
  const BFT = [1, 4, 7, 11, 17, 22, 28, 34, 41, 48, 56, 64];
  const bft = kn => BFT.findIndex(v => kn < v) < 0 ? 12 : BFT.findIndex(v => kn < v);
  const windCol = kn => ["#7aa6c2", "#5ba3c9", "#3fae8f", "#5fbf4a", "#a5c83b", "#e3c22d", "#f29d2a", "#ef6c2b", "#e04a2f", "#c2304a", "#9b2a7a", "#6e2aa0", "#4b1f8f"][bft(kn)];
  const rainCol = mm => mm < 0.1 ? null : mm < 0.5 ? "rgba(120,200,120,.45)" : mm < 2 ? "rgba(40,170,80,.55)" : mm < 5 ? "rgba(230,200,40,.6)" : mm < 10 ? "rgba(240,130,30,.65)" : "rgba(210,40,40,.7)";
  /* a wind barb: the shaft to where the wind comes from, 50 kn pennants, 10 kn barbs, 5 kn half barbs */
  function barb(x, y, dir, kn) {
    const a = dir * D, L = 18, ux = Math.sin(a), uy = -Math.cos(a), px = -uy, py = ux;
    g.strokeStyle = g.fillStyle = windCol(kn); g.lineWidth = 1.5;
    if (kn < 2) { g.beginPath(); g.arc(x, y, 3, 0, 2 * Math.PI); g.stroke(); return; }
    g.beginPath(); g.moveTo(x, y); g.lineTo(x + ux * L, y + uy * L); g.stroke();
    let v = Math.round(kn / 5) * 5, t = L;
    while (v >= 50) { g.beginPath(); g.moveTo(x + ux * t, y + uy * t); g.lineTo(x + ux * (t - 3) + px * 7, y + uy * (t - 3) + py * 7); g.lineTo(x + ux * (t - 6), y + uy * (t - 6)); g.fill(); t -= 7; v -= 50; }
    while (v >= 10) { g.beginPath(); g.moveTo(x + ux * t, y + uy * t); g.lineTo(x + ux * (t + 2) + px * 8, y + uy * (t + 2) + py * 8); g.stroke(); t -= 3.5; v -= 10; }
    if (v >= 5) { if (t === L) t -= 3.5; g.beginPath(); g.moveTo(x + ux * t, y + uy * t); g.lineTo(x + ux * (t + 1) + px * 4, y + uy * (t + 1) + py * 4); g.stroke(); }
  }
  /* the value of a field at (lat, lon): bilinear between the four grid points round it */
  function at(field, la, lo) {
    const c = fc.cell, fi = (la - fc.la0) / c, fj = (near(lo, st.lon0) - fc.lon0) / c, i = Math.floor(fi), j = Math.floor(fj);
    const v = (a, b) => { const p = fc.grid.get(+(fc.la0 + a * c).toFixed(3) + "," + b); return p ? p.h[field][hour] : null; };
    const q = [v(i, j), v(i, j + 1), v(i + 1, j), v(i + 1, j + 1)];
    if (q.some(x => x === null || x === undefined)) { const k = q.find(x => x !== null && x !== undefined); return k === undefined ? null : k; }
    const a = fi - i, b = fj - j;
    return (q[0] * (1 - b) + q[1] * b) * (1 - a) + (q[2] * (1 - b) + q[3] * b) * a;
  }
  let off = null;
  /* rain and clouds: smooth, every 6 px, drawn up */
  function fields() {
    const s = 6, w = Math.ceil(W / s), h = Math.ceil(H / s);
    if (!off || off.width !== w || off.height !== h) { off = document.createElement("canvas"); off.width = w; off.height = h; }
    const o = off.getContext("2d"), im = o.createImageData(w, h), d = im.data;
    for (let y = 0; y < h; y++) {
      const la = U(0, (y + 0.5) * s)[0];
      for (let x = 0; x < w; x++) {
        const lo = U((x + 0.5) * s, 0)[1], k = 4 * (y * w + x);
        let r = 0, gg = 0, b = 0, a = 0;
        if (st.layers.cloud) { const cc = at("cloud_cover", la, lo); if (cc > 5) { r = 150; gg = 155; b = 165; a = cc / 100 * 0.5; } }
        if (st.layers.rain) {
          const mm = at("precipitation", la, lo);
          if (mm >= 0.1) {
            const C = mm < 0.5 ? [120, 200, 120, .45] : mm < 2 ? [40, 170, 80, .55] : mm < 5 ? [230, 200, 40, .6] : mm < 10 ? [240, 130, 30, .65] : [210, 40, 40, .7];
            r = C[0]; gg = C[1]; b = C[2]; a = Math.max(a, C[3]);
          }
        }
        d[k] = r; d[k + 1] = gg; d[k + 2] = b; d[k + 3] = Math.round(a * 255);
      }
    }
    o.putImageData(im, 0, 0);
    g.imageSmoothingEnabled = true; g.imageSmoothingQuality = "high"; g.drawImage(off, 0, 0, w * s, h * s);
  }
  function draw() {
    if (!g || document.getElementById("t-weather").hidden) return;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = css("--sea"); g.fillRect(0, 0, W, H);
    /* the land */
    const polys = APP.loadCoast(span() < 25 ? "L" : "C"), [n0] = U(0, 0), [s0] = U(0, H), lw = st.lon0 - span() / 2 - 1, le = st.lon0 + span() / 2 + 1;
    g.fillStyle = css("--land"); g.strokeStyle = css("--coast"); g.lineWidth = 0.8;
    for (const Q of polys) {
      if (Q.n < s0 || Q.s > n0) continue;
      for (const off of [-360, 0, 360]) {
        if (Q.e + off < lw || Q.w + off > le) continue;
        g.beginPath();
        for (let j = 0; j < Q.lat.length; j++) { const x = W / 2 + st.k * (Q.lon[j] + off - st.lon0) * D, y = H / 2 - st.k * (merc(Q.lat[j]) - merc(st.lat0)); j ? g.lineTo(x, y) : g.moveTo(x, y); }
        g.closePath(); g.fillStyle = Q.lev === 2 ? css("--sea") : css("--land"); g.fill(); g.stroke();
      }
    }
    /* observed: satellite, radar */
    if (st.online && rv) {
      if (st.layers.sat && rv.sat.length) drawTiles(rv.sat[rv.sat.length - 1].path, "sat");
      if (st.mode === "radar" && st.layers.radar && rv.radar[frame]) drawTiles(rv.radar[frame].path, "radar");
    }
    /* the forecast at the hour */
    if (st.online && fc && st.mode === "fc") {
      if (st.layers.cloud || st.layers.rain) fields();
      g.font = "15px system-ui"; g.textAlign = "center";
      for (const p of fc.pts) {
        const [x, y] = P(p.lat, p.lon), h = p.h;
        if (x < -10 || y < -10 || x > W + 10 || y > H + 10) continue;
        if (st.layers.storm) {
          const wc = h.weather_code[hour], cape = h.cape[hour];
          if (wc >= 95) { g.fillStyle = "#7b2cbf"; g.fillText("⚡", x, y + 5); continue; }
          if (cape > 1500) { g.fillStyle = "rgba(123,44,191,.55)"; g.beginPath(); g.arc(x, y, 4, 0, 2 * Math.PI); g.fill(); }
        }
        if (st.layers.wind) barb(x, y, h.wind_direction_10m[hour], h.wind_speed_10m[hour]);
      }
      g.textAlign = "left";
    }
    /* the route */
    const rows = (APP.route() || []);
    if (rows.length > 1) {
      g.strokeStyle = css("--track"); g.lineWidth = 2.5; g.beginPath();
      let px = 0;
      rows.forEach((r, i) => { const [x, y] = P(r.lat, r.lon); if (i && Math.abs(x - px) > W * 0.7) g.moveTo(x, y); else i ? g.lineTo(x, y) : g.moveTo(x, y); px = x; });
      g.stroke();
    }
    { const [x, y] = P(APP.state.pos[0], APP.state.pos[1]); g.strokeStyle = css("--now"); g.lineWidth = 2; g.beginPath(); g.moveTo(x - 8, y); g.lineTo(x + 8, y); g.moveTo(x, y - 8); g.lineTo(x, y + 8); g.stroke(); }
    g.fillStyle = css("--soft"); g.font = "11px system-ui";
    g.fillText("Weather: Open-Meteo.com (CC BY 4.0) · radar, satellite: RainViewer · coast: Natural Earth", 8, H - 8);
    label();
  }
  function label() {
    const el = document.getElementById("wxtime"); if (!el) return;
    if (st.mode === "radar") {
      if (!rv || !rv.radar[frame]) { el.textContent = st.online ? "radar: loading…" : ""; return; }
      const t = rv.radar[frame].time * 1000, nc = frame >= rv.radar.length - rv.nowcast;
      el.textContent = `${new Date(t).toISOString().slice(0, 16).replace("T", " ")} UT · ${nc ? "nowcast" : "observed"} radar (${frame + 1}/${rv.radar.length})`;
      const s = document.getElementById("wxslide"); if (s) { s.max = rv.radar.length - 1; s.value = frame; }
    } else {
      if (!fc || !fc.times.length) { el.textContent = st.online ? (fcBusy ? "forecast: loading…" : "") : ""; return; }
      const t = fc.times[hour], dh = Math.round((t - Date.now()) / 36e5);
      el.textContent = `${new Date(t).toISOString().slice(0, 16).replace("T", " ")} UT · ${dh === 0 ? "now" : (dh > 0 ? "+" : "") + dh + " h"}`;
      const s = document.getElementById("wxslide"); if (s) { s.max = fc.times.length - 1; s.value = hour; }
    }
  }
  function info() {
    const el = document.getElementById("wxinfo"); if (!el) return;
    const bits = [];
    if (!st.online) bits.push("Offline: switch <b>Online</b> on to load the weather (only the map's grid points are sent).");
    if (fcBusy) bits.push("loading the forecast…");
    if (fcErr) bits.push(`<span style="color:var(--now)">forecast: ${fcErr}</span>`);
    if (rvErr) bits.push(`<span style="color:var(--now)">radar: ${rvErr}</span>`);
    if (fc && st.online) bits.push(`forecast grid ${fc.cell}°, ${fc.pts.length} points`);
    el.innerHTML = bits.join(" · ");
  }

  /* ---------------------------------------------------------------- the tab */
  function build() {
    const el = document.getElementById("t-weather");
    el.innerHTML = `<div class="card">
      <div class="tools">
        <button id="wxon" class="${st.online ? "on" : ""}" title="load the weather from Open-Meteo and RainViewer">🌐 Online</button>
        <span style="display:inline-flex;gap:4px"><button class="small" data-wm="fc">Forecast</button><button class="small" data-wm="radar">Radar (2 h)</button></span>
        ${[["wind", "Wind"], ["rain", "Rain"], ["cloud", "Clouds"], ["storm", "Thunderstorms"], ["radar", "Radar"], ["sat", "Satellite IR"]]
          .map(([k, t]) => `<label><input type="checkbox" data-wl="${k}" ${st.layers[k] ? "checked" : ""}> ${t}</label>`).join(" ")}
        <span style="flex:1"></span><button id="wxfit">Route</button><button id="wxdr">DR</button><button id="wxworld">World</button>
      </div>
      <div style="position:relative"><canvas id="wxcv" style="display:block;width:100%;height:640px;border-radius:10px;border:1px solid var(--line);touch-action:none"></canvas></div>
      <div class="controls" style="margin-top:8px"><button id="wxplay">▶ Play</button><input type="range" id="wxslide" min="0" max="71" value="0" style="flex:1">
        <span id="wxtime" class="mono" style="min-width:300px"></span></div>
      <div class="legend" style="margin-top:6px">
        <span>Wind (barbs, kn): ${[0, 4, 11, 22, 34, 48, 64].map(v => `<span style="color:${windCol(v + 1)}">■</span> ${v}`).join(" ")}</span>
        <span>Rain mm/h: <span style="color:rgb(120,200,120)">■</span> 0.1 <span style="color:rgb(40,170,80)">■</span> 0.5 <span style="color:rgb(230,200,40)">■</span> 2
          <span style="color:rgb(240,130,30)">■</span> 5 <span style="color:rgb(210,40,40)">■</span> 10</span>
        <span>Clouds: grey by cover</span><span>⚡ thunderstorm forecast · <span style="color:#7b2cbf">●</span> CAPE > 1500 J/kg</span></div>
      <div class="meta" id="wxinfo"></div>
      <div class="meta">The forecast: hourly, 72 h, on a grid over the view (finer as you zoom in), from Open-Meteo (the national weather
        services' models; free for non-commercial use). The radar: observed rain of the last 2 hours and a 30-minute nowcast, every 10 minutes
        (where radars are: over land and near the coasts); the satellite: infrared clouds. Lightning: no free global feed; the thunderstorm
        forecast instead. Drag to move, the wheel to zoom. Not for navigation: use the official forecasts and warnings.</div></div>`;
    cv = document.getElementById("wxcv"); g = cv.getContext("2d");
    document.getElementById("wxon").onclick = () => { st.online = !st.online; save(); document.getElementById("wxon").classList.toggle("on", st.online); info(); if (st.online) { fetchFc(); fetchRv(); } draw(); };
    el.querySelectorAll("[data-wm]").forEach(b => { b.classList.toggle("on", b.dataset.wm === st.mode);
      b.onclick = () => { st.mode = b.dataset.wm; if (st.mode === "radar") st.layers.radar = true; save(); build(); fetchRv(); draw(); }; });
    el.querySelectorAll("[data-wl]").forEach(i => i.onchange = () => { st.layers[i.dataset.wl] = i.checked; save(); fetchRv(); draw(); });
    document.getElementById("wxslide").oninput = e => { stop(); if (st.mode === "radar") frame = +e.target.value; else hour = +e.target.value; draw(); };
    document.getElementById("wxplay").onclick = () => playing ? stop() : play();
    const go = (la, lo, sp) => { st.lat0 = la; st.lon0 = lo; st.k = W / (sp * D); clamp(); save(); later(); draw(); };
    document.getElementById("wxworld").onclick = () => go(20, 0, 360);
    document.getElementById("wxdr").onclick = () => go(APP.state.pos[0], APP.state.pos[1], 12);
    document.getElementById("wxfit").onclick = () => {
      const r = APP.route(); if (!r || r.length < 2) return go(APP.state.pos[0], APP.state.pos[1], 12);
      let s = 90, n = -90, w = 1e9, e = -1e9, lo = r[0].lon;
      for (const p of r) { const l = near(p.lon, lo); s = Math.min(s, p.lat); n = Math.max(n, p.lat); w = Math.min(w, l); e = Math.max(e, l); }
      const sp = Math.max((e - w) * 1.3, (n - s) * 1.3 * W / H / Math.max(0.3, Math.cos((s + n) / 2 * D)), 2);
      go((s + n) / 2, (w + e) / 2, sp);
    };
    let down = null;
    cv.addEventListener("pointerdown", e => { cv.setPointerCapture(e.pointerId); down = { x: e.clientX, y: e.clientY, lo: st.lon0, m: merc(st.lat0) }; });
    cv.addEventListener("pointermove", e => { if (!down) return; st.lon0 = down.lo - (e.clientX - down.x) / st.k / D; st.lat0 = latOf(down.m + (e.clientY - down.y) / st.k); clamp(); draw(); });
    cv.addEventListener("pointerup", () => { down = null; save(); later(); });
    cv.addEventListener("wheel", e => { e.preventDefault(); const r = cv.getBoundingClientRect(), x = e.clientX - r.left, y = e.clientY - r.top, b = U(x, y);
      st.k *= Math.exp(-e.deltaY * 0.0015); clamp(); const a = U(x, y); st.lon0 += b[1] - a[1]; st.lat0 = latOf(merc(st.lat0) + merc(b[0]) - merc(a[0])); clamp(); draw(); save(); later(); }, { passive: false });
    new ResizeObserver(resize).observe(cv);
    built = true;
  }
  function play() {
    playing = true; document.getElementById("wxplay").textContent = "⏸ Pause";
    timer = setInterval(() => {
      if (st.mode === "radar") { if (!rv) return; frame = (frame + 1) % rv.radar.length; }
      else { if (!fc) return; hour = (hour + 1) % fc.times.length; }
      draw();
    }, st.mode === "radar" ? 600 : 700);
  }
  function stop() { playing = false; clearInterval(timer); const b = document.getElementById("wxplay"); if (b) b.textContent = "▶ Play"; }
  function resize() {
    if (!cv) return;
    const r = cv.getBoundingClientRect(); if (!r.width) return;
    dpr = window.devicePixelRatio || 1; W = r.width; H = r.height;
    cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); clamp(); draw(); later();
  }
  function show() {
    if (/wx=on/.test(location.hash)) { st.online = true; save(); }   /* #wx=on (tests, demos) */
    const v = location.hash.match(/wxv=(-?[\d.]+),(-?[\d.]+),([\d.]+)/);   /* #wxv=lat,lon,span */
    if (v) { st.lat0 = +v[1]; st.lon0 = +v[2]; st.k = (document.body.clientWidth - 60) / (+v[3] * D); }
    const m = location.hash.match(/wxmode=(fc|radar)/); if (m) { st.mode = m[1]; if (m[1] === "radar") { st.layers.radar = true; st.layers.sat = true; } }
    if (!built) build(); resize(); info(); if (st.online) { fetchFc(); fetchRv(); } }
  return { show, draw };
})();
