/* tabs.js - NAV47 on the web: the Almanac, Sky, Tide and Zones tabs, from the calculator's own engines (web/api.c). */
"use strict";
const TABS2 = (() => {
  const F = () => APP.fmt, S = () => APP.state, N = () => APP.nav;
  const PL = ["SUN", "MOON", "VENUS", "MARS", "JUPITER", "SATURN"];
  const SYM = { SUN: "☉", MOON: "☾", VENUS: "♀", MARS: "♂", JUPITER: "♃", SATURN: "♄" };
  const jd = () => S().live ? Date.now() / 864e5 + 2440587.5 : S().jd;
  const off = () => S().times === "zt" ? Math.round(S().pos[1] / 15) : 0;
  const lab = () => off() ? "ZT" : "UT";
  const ns = v => (v < 0 ? "S " : "N ") + F().dm(v, 2);
  const daylight = hc => hc > 0 ? "DAY" : hc > -12 ? "TWILIGHT" : "NIGHT";
  let starNames = null, bright = null;
  function stars() {
    if (!starNames) {
      starNames = Array.from({ length: 58 }, (_, i) => N().str(Module._api_star_name(i + 1)));
      bright = Array.from({ length: 58 }, (_, i) => Module._api_bright(i));
    }
  }
  /* all the bodies now: {n, name, gha, dec, hc, zn, rank} (n 0 the Sun, -1 the Moon, -2..-5 the planets, 1..58 stars) */
  function bodies() {
    stars();
    const [lat, lon] = S().pos;
    Module._api_alm(jd(), lat, lon, off());
    const o = N().o(), out = [];
    for (let k = 0; k < 64; k++) {
      const n = k < 6 ? -k : k - 5, name = k < 6 ? PL[k] : starNames[k - 6];
      out.push({ n, name, gha: o[4 * k], dec: o[4 * k + 1], hc: o[4 * k + 2], zn: o[4 * k + 3], rank: k < 6 ? -1 : bright.indexOf(k - 5) });
    }
    const t = Array.from(o.subarray(256, 267));
    return { b: out, times: t.slice(0, 5), illum: t[5], age: t[6], sunSD: t[7], moonHP: t[8], moonSD: t[9], aries: t[10] };
  }
  function headCard(title, extra) {
    const [lat, lon] = S().pos;
    return `<div class="title" style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px"><h2>${title}</h2>
      <span class="meta">${F().dt(jd(), off())} ${lab()} · DR ${F().pos(lat, lon)}${extra || ""}</span></div>`;
  }

  /* ---------------------------------------------------------------- Almanac */
  function almanac() {
    const A = bodies(), b = A.b, sun = b[0], el = document.getElementById("t-almanac");
    const row = x => `<tr class="${x.hc < 0 ? "dim" : ""}"><td>${SYM[x.name] || "★"} ${x.name}${x.n > 0 ? ` <span class="meta">${x.n === 58 ? "" : x.n}</span>` : ""}</td>
      <td class="num mono">${F().dm(x.gha, 3)}</td><td class="num mono">${ns(x.dec)}</td><td class="num mono">${(x.hc < 0 ? "−" : "") + F().dm(x.hc)}</td>
      <td class="num mono">${x.zn.toFixed(1).padStart(5, "0")}°</td></tr>`;
    const solar = b.slice(0, 6), st = b.slice(6).filter(x => x.hc > 10).sort((p, q) => p.rank - q.rank);
    Module._api_moon(jd());
    const m = N().o(), names = ["Twilight (morning)", "Sunrise", "Meridian passage", "Sunset", "Twilight (evening)"];
    const phase = N().str(Module._api_moon_name(m[4])), next = [0, 1, 2, 3].map(i => [m[5 + 2 * i], ["New Moon", "First quarter", "Full Moon", "Last quarter"][m[6 + 2 * i]]]);
    el.innerHTML = `<div class="card">${headCard("Almanac", ` · GHA ♈ ${F().dm(A.aries, 3)}`)}
      <div class="chips"><span class="chip">${daylight(sun.hc)}</span><span class="chip">Sun SD ${A.sunSD.toFixed(1)}′</span>
      <span class="chip">Moon HP ${A.moonHP.toFixed(1)}′ SD ${A.moonSD.toFixed(1)}′</span><span class="chip">${st.length} stars above 10°</span></div>
      <div class="two"><div><h3>The Sun, the Moon, the planets</h3><table><tr><th>Body</th><th class="num">GHA</th><th class="num">Dec</th><th class="num">Hc</th><th class="num">Zn</th></tr>
      ${solar.map(row).join("")}</table>
      <h3>The Sun's day (${lab()})</h3><table>${names.map((t, i) => `<tr><td>${t}</td><td class="num mono">${F().hm(A.times[i])}</td></tr>`).join("")}</table>
      <h3>The Moon</h3><table><tr><td>${phase}</td><td class="num">${m[0].toFixed(0)}% lit, ${m[1].toFixed(1)} d</td></tr>
      ${next.map(([t, k]) => `<tr><td>${k}</td><td class="num mono">${F().dt(t, off())} ${lab()}</td></tr>`).join("")}</table></div>
      <div><h3>Stars above 10°, the brightest first</h3><div class="scroll" style="max-height:640px"><table><tr><th>Star</th><th class="num">GHA</th><th class="num">Dec</th><th class="num">Hc</th><th class="num">Zn</th></tr>
      ${st.map(row).join("")}</table></div></div></div>
      <div class="meta">The calculator's own almanac (Almanac 47: VSOP87D, IAU1980, Meeus + DE421 fit). Not for navigation: check against the Nautical Almanac.</div></div>`;
  }

  /* ---------------------------------------------------------------- Sky: the dome */
  function sky() {
    const A = bodies(), b = A.b, sun = b[0], el = document.getElementById("t-sky");
    const dl = daylight(sun.hc), W = 640, R = W / 2 - 34, c = W / 2;
    const P = (hc, zn) => { const r = R * (90 - hc) / 90, a = zn * Math.PI / 180; return [c + r * Math.sin(a), c - r * Math.cos(a)]; };
    const bg = dl === "DAY" ? "var(--sky-day)" : dl === "TWILIGHT" ? "var(--sky-twi)" : "var(--sky-night)", fg = dl === "DAY" ? "#1b2129" : "#e7ebf0";
    let g = `<circle cx="${c}" cy="${c}" r="${R}" fill="${bg}"/>`;
    for (const a of [30, 60]) g += `<circle cx="${c}" cy="${c}" r="${R * (90 - a) / 90}" fill="none" stroke="${fg}" stroke-opacity=".25"/>`;
    for (let z = 0; z < 360; z += 30) { const [x, y] = P(0, z); g += `<line x1="${c}" y1="${c}" x2="${x}" y2="${y}" stroke="${fg}" stroke-opacity=".12"/>`; }
    [["N", 0], ["E", 90], ["S", 180], ["W", 270]].forEach(([t, z]) => { const a = z * Math.PI / 180;
      g += `<text x="${c + (R + 17) * Math.sin(a)}" y="${c - (R + 17) * Math.cos(a) + 5}" text-anchor="middle" font-size="14" font-weight="700" fill="var(--soft)">${t}</text>`; });
    for (const s of b.slice(6).filter(x => x.hc > 0).sort((p, q) => q.rank - p.rank)) {
      const [x, y] = P(s.hc, s.zn), rr = Math.max(1.6, 5 - s.rank / 13), right = x > c + R * 0.55;
      g += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="${rr.toFixed(1)}" fill="${fg}"><title>${s.n} ${s.name}  Hc ${F().dm(s.hc)}  Zn ${s.zn.toFixed(1)}°</title></circle>`;
      if (s.rank < 24) g += `<text x="${(right ? x - rr - 3 : x + rr + 3).toFixed(1)}" y="${(y + 4).toFixed(1)}" text-anchor="${right ? "end" : "start"}" font-size="11" fill="${fg}" fill-opacity=".85">${s.name}</text>`;
    }
    if (sun.hc > -1) { const [x, y] = P(Math.max(sun.hc, 0), sun.zn); g += `<circle cx="${x}" cy="${y}" r="9" fill="var(--sun)"/><text x="${x + 12}" y="${y + 4}" font-size="12" font-weight="700" fill="${fg}">SUN</text>`; }
    for (const p of b.slice(1, 6)) {
      if (p.hc <= 0) continue;
      const [x, y] = P(p.hc, p.zn), col = p.name === "MOON" ? (dl === "DAY" ? "#6b6f78" : "#f2efe4") : (dl === "DAY" ? "var(--accent)" : "#9fd0ff");
      g += `<text x="${x.toFixed(1)}" y="${(y + 7).toFixed(1)}" text-anchor="middle" font-size="${p.name === "MOON" ? 22 : 18}" font-weight="700" fill="${col}">${SYM[p.name]}<title>${p.name} Hc ${F().dm(p.hc)} Zn ${p.zn.toFixed(1)}°</title></text>
        <text x="${(x + 12).toFixed(1)}" y="${(y + 4).toFixed(1)}" font-size="11" font-weight="600" fill="${col}">${p.name}</text>`;
    }
    g += `<circle cx="${c}" cy="${c}" r="${R}" fill="none" stroke="var(--soft)"/>`;
    const best = b.slice(6).filter(x => x.hc > 15 && x.hc < 75).sort((p, q) => p.rank - q.rank).slice(0, 8);
    el.innerHTML = `<div class="card">${headCard("Sky")}<div class="chips"><span class="chip">${dl}</span>
      <span class="chip">Sun Hc ${(sun.hc < 0 ? "−" : "") + F().dm(sun.hc)} Zn ${sun.zn.toFixed(1)}°</span></div>
      <div class="two"><svg viewBox="0 0 ${W} ${W}" style="max-width:${W}px">${g}</svg>
      <div><h3>For sights: the brightest between 15° and 75°</h3><table><tr><th>Star</th><th class="num">Hc</th><th class="num">Zn</th></tr>
      ${best.map(s => `<tr><td>★ ${s.name} <span class="meta">${s.n}</span></td><td class="num mono">${F().dm(s.hc)}</td><td class="num mono">${s.zn.toFixed(1)}°</td></tr>`).join("")}</table>
      <div class="meta">The sky from the DR: the zenith in the centre, the horizon round the edge, north up, rings at 30° and 60°
        (the calculator's SKY2). Hover a body for its Hc and Zn.</div></div></div></div>`;
  }

  /* ---------------------------------------------------------------- Tide */
  let tideSt = null, tideDay = 0;
  function tide() {
    const el = document.getElementById("t-tide"), n = Module._api_tide_count();
    const list = Array.from({ length: n }, (_, i) => {
      Module._api_tide_info(i); const o = N().o();
      return { i, name: N().str(Module._api_tide_name(i)), region: N().str(Module._api_tide_region(i)), lat: o[0], lon: o[1], tz: o[2], kind: o[3],
               src: Module._api_tide_src(i) };
    });
    if (tideSt === null) {                                    /* the nearest to the DR */
      const [la, lo] = S().pos;
      let best = 1e18;
      for (const s of list) { if (s.kind !== 0) continue; const d = N().inv(la, lo, s.lat, s.lon).s; if (d < best) { best = d; tideSt = s.i; } }
    }
    const st = list[tideSt], t0 = new Date((jd() - 2440587.5) * 864e5 + st.tz * 36e5 + tideDay * 864e5);
    const y = t0.getUTCFullYear(), m = t0.getUTCMonth() + 1, d = t0.getUTCDate();
    Module._api_tide_day(st.i, y, m, d);
    const o = N().o(), tz = o[0], nev = o[1], ev = [], curve = Array.from(o.subarray(34, 107));
    for (let k = 0; k < nev; k++) ev.push({ t: o[2 + 4 * k], v: o[3 + 4 * k], set: o[4 + 4 * k], kind: o[5 + 4 * k] });
    const stream = st.kind !== 0, unit = stream ? "kn" : "m";
    const W = 720, H = 260, x0 = 40, x1 = W - 10, y0 = 14, y1 = H - 26, mn = Math.min(...curve), mx = Math.max(...curve);
    const lo = Math.floor(Math.min(mn, stream ? mn : 0) * 2) / 2, hi = Math.ceil(mx * 2) / 2 || 1;
    const X = h => x0 + (x1 - x0) * h / 24, Y = v => y1 - (y1 - y0) * (v - lo) / (hi - lo || 1);
    let g = "";
    for (let h = 0; h <= 24; h += 3) g += `<line x1="${X(h)}" y1="${y0}" x2="${X(h)}" y2="${y1}" stroke="var(--grid)"/><text x="${X(h)}" y="${H - 8}" text-anchor="middle" font-size="11" fill="var(--soft)">${String(h).padStart(2, "0")}</text>`;
    for (let v = lo; v <= hi + 1e-9; v += (hi - lo) > 4 ? 1 : 0.5) g += `<line x1="${x0}" y1="${Y(v)}" x2="${x1}" y2="${Y(v)}" stroke="var(--grid)"/><text x="${x0 - 4}" y="${Y(v) + 4}" text-anchor="end" font-size="11" fill="var(--soft)">${v.toFixed(1)}</text>`;
    g += `<polyline fill="none" stroke="var(--accent)" stroke-width="2.5" points="${curve.map((v, k) => `${X(k / 3).toFixed(1)},${Y(v).toFixed(1)}`).join(" ")}"/>`;
    for (const e of ev) g += `<circle cx="${X(e.t)}" cy="${Y(e.v)}" r="4" fill="var(--ink)"/><text x="${X(e.t)}" y="${Y(e.v) + (e.kind > 0 ? -9 : 17)}" text-anchor="middle" font-size="11" fill="var(--ink)">${F().hm(e.t)}</text>`;
    if (tideDay === 0) {
      const nowH = ((jd() - 2440587.5) * 24 + tz) % 24;
      g += `<line x1="${X(nowH)}" y1="${y0}" x2="${X(nowH)}" y2="${y1}" stroke="var(--now)" stroke-dasharray="4 3"/>`;
    }
    const regions = [...new Set(list.map(s => s.region))];
    const srcTag = r => { const s0 = list.find(s => s.region === r); return s0 && s0.src === 2 ? " (your file)" : ""; };
    el.innerHTML = `<div class="card"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px"><h2>Tide · ${st.name}</h2>
      <span class="meta">${String(d).padStart(2, "0")}-${String(m).padStart(2, "0")}-${y} · zone time UTC${tz >= 0 ? "+" : ""}${tz} · ${F().pos(st.lat, st.lon)}</span></div>
      <div class="controls"><select id="tst">${regions.map(r => `<optgroup label="${r}${srcTag(r)}">${list.filter(s => s.region === r).map(s => `<option value="${s.i}" ${s.i === st.i ? "selected" : ""}>${s.name}</option>`).join("")}</optgroup>`).join("")}</select>
        <button id="tprev">← day</button><button id="ttoday">today</button><button id="tnext">day →</button></div>
      <div class="two" style="margin-top:8px"><svg viewBox="0 0 ${W} ${H}">${g}</svg>
      <div><table><tr><th></th><th class="num">Time</th><th class="num">${stream ? "Rate" : "Height"}</th>${stream ? '<th class="num">Set</th>' : ""}</tr>
      ${ev.map(e => `<tr><td>${stream ? (e.kind > 0 ? "Flood max" : e.kind < 0 ? "Ebb max" : "Slack") : (e.kind > 0 ? "HW" : "LW")}</td><td class="num mono">${F().hm(e.t)}</td>
        <td class="num mono">${e.v.toFixed(2)} ${unit}</td>${stream ? `<td class="num mono">${Math.round(e.set)}°</td>` : ""}</tr>`).join("")}</table>
      <div class="meta">The calculator's own tide engine (Tide 47, the Schureman harmonic method); ${st.src === 2 ? "a station of your file" : st.src === 1 ? "NOAA's current stations (public domain)" : "the public stations (NOAA, TICON-3)"}.
        ${stream ? "Rates in knots." : "Heights above the chart datum, metres."} Not for navigation: check against the official tide tables.</div></div></div></div>
      ${APP.tfiles.card()}`;
    APP.tfiles.bind(el, () => { tideSt = null; tide(); });
    document.getElementById("tst").onchange = e => { tideSt = +e.target.value; tide(); };
    document.getElementById("tprev").onclick = () => { tideDay--; tide(); };
    document.getElementById("tnext").onclick = () => { tideDay++; tide(); };
    document.getElementById("ttoday").onclick = () => { tideDay = 0; tide(); };
  }

  /* ---------------------------------------------------------------- Zones: as ZONE47 (and Almanac 47 web) */
  let zcur = null, zcity = 0, zkeys = false;
  const zfo = o => o === 0 ? "UTC±0" : "UTC" + (o > 0 ? "+" : "−") + Math.floor(Math.abs(o)) +
    (Math.abs(o) % 1 ? ":" + String(Math.round(Math.abs(o) % 1 * 60)).padStart(2, "0") : "");
  const WD = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"], MO = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
  const zlocal = (J, o) => { const t = new Date((J - 2440587.5) * 864e5 + o * 36e5);
    return `${WD[t.getUTCDay()]} ${String(t.getUTCDate()).padStart(2, "0")} ${MO[t.getUTCMonth()]} ${String(t.getUTCHours()).padStart(2, "0")}:${String(t.getUTCMinutes()).padStart(2, "0")}`; };
  function zdata(J) {
    const n = Module._api_zone_count(), stops = [];
    for (let i = 0; i < n; i++) {
      const nc = Module._api_zone_cities(i), cities = [];
      for (let j = 0; j < nc; j++) {
        Module._api_city_off(i, j, J); const o = N().o();
        cities.push({ name: N().str(Module._api_city_name(i, j)), cc: N().str(Module._api_city_cc(i, j)), off: o[0], on: !!o[1],
                      kind: Module._api_city_kind(i, j) });
      }
      stops.push({ i, off: Module._api_zone_off(i), col: Module._api_zone_col(i), cities });
    }
    return stops.sort((a, b) => a.off - b.off);
  }
  function zones() {
    const el = document.getElementById("t-zones"), J = jd(), stops = zdata(J), here = -new Date().getTimezoneOffset() / 60;
    let mine = 0;
    stops.forEach((z, k) => { if (Math.abs(z.off - here) < Math.abs(stops[mine].off - here)) mine = k; });
    if (zcur === null) { const v = localStorage.getItem("nav47web:zone"); zcur = v !== null && +v < stops.length ? +v : mine; }
    const Z = stops[zcur]; zcity = Math.min(zcity, Z.cities.length - 1);
    const hv = location.hash.match(/zview=(map|list)/);           /* #zview=list (demos) */
    if (hv) { localStorage.setItem("nav47web:zview", hv[1]); history.replaceState(null, "", location.href.replace(/&?zview=(map|list)/, "")); }
    const view = localStorage.getItem("nav47web:zview") || "map";
    const sw = `<span style="display:inline-flex;gap:4px"><button class="small ${view === "map" ? "on" : ""}" data-zv="map">Map</button>` +
               `<button class="small ${view === "list" ? "on" : ""}" data-zv="list">List</button></span>`;
    const bindSw = () => el.querySelectorAll("[data-zv]").forEach(b => b.onclick = () => { localStorage.setItem("nav47web:zview", b.dataset.zv); zones(); });
    if (view === "list") {                                    /* every zone, every place, the time now */
      el.innerHTML = `<div class="card"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px;align-items:baseline">
        <h2>Zones ${sw}</h2><span class="meta">${F().dt(J, 0)} UT · tzdata ${N().str(Module._api_tzdata())} · summer time computed as on the calculator</span></div>
        <div class="scroll"><table><tr><th>Zone</th><th class="num">Now</th><th>Places (★ summer time now)</th></tr>
        ${stops.map((r, k) => `<tr class="click${k === mine ? " sel" : ""}" data-z="${k}"><td class="mono">${zfo(r.off)}${r.off === -12 ? " at sea" : ""}</td>
          <td class="num mono">${zlocal(J, r.off).slice(-5)}</td>
          <td style="white-space:normal">${r.cities.map(c => `${c.name}${c.cc ? " " + c.cc : ""}${c.on ? ` ★ ${zfo(c.off)} ${zlocal(J, c.off).slice(-5)}` : ""}`).join(", ")}</td></tr>`).join("")}</table></div>
        <div class="meta">ZONE47: the official offsets (−12 at sea); your clock's zone highlighted. Click a zone for it on the map.</div></div>`;
      bindSw();
      el.querySelectorAll("tr[data-z]").forEach(tr => tr.onclick = () => { zcur = +tr.dataset.z; zcity = 0; localStorage.setItem("nav47web:zone", zcur);
        localStorage.setItem("nav47web:zview", "map"); zones(); });
      return;
    }
    const c0 = Z.cities[zcity];
    /* the dot map */
    const ncol = Module._api_map_ncol(), nrow = Module._api_map_nrow();
    const W = Math.round(Math.min(1100, (el.getBoundingClientRect().width || 1000) - 34)), dx = W / ncol, dy = dx, H = Math.round(nrow * dy + 34);
    let g = "";
    for (let j = 0; j < ncol; j++) {
      Module._api_map_col(j); const o = N().o(), lo = o[0], hi32 = o[1];
      const hi = Math.floor(j / 5) === Z.col;
      for (let i = 0; i < nrow; i++) {
        const land = i < 32 ? Math.floor(lo / 2 ** i) % 2 : Math.floor(hi32 / 2 ** (i - 32)) % 2;
        const x = (j + 0.5) * dx, y = (i + 0.5) * dy;
        if (land) g += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="${(dx * 0.38).toFixed(2)}" fill="${hi ? "var(--accent)" : "var(--land)"}"/>`;
        else if (hi) g += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="${(dx * 0.16).toFixed(2)}" fill="var(--accent)" opacity=".45"/>`;
      }
    }
    for (let c = 0; c < 24; c++) {
      const x = c * 5 * dx, z = c - 11;
      g += `<line x1="${x}" x2="${x}" y1="0" y2="${nrow * dy}" stroke="var(--grid)"/>`;
      g += `<text x="${x + 2.5 * dx}" y="${nrow * dy + 14}" text-anchor="middle" font-size="${dx > 6 ? 11 : 9}" fill="${c === Z.col ? "var(--accent)" : "var(--soft)"}" font-weight="${c === Z.col ? 700 : 400}">${z > 0 ? "+" + z : z}</text>`;
      g += `<rect x="${x}" y="0" width="${5 * dx}" height="${nrow * dy + 18}" fill="transparent" data-c="${c}" style="cursor:pointer"><title>UTC${z > 0 ? "+" + z : z}</title></rect>`;
    }
    const mc = stops[mine].col;
    g += `<path d="M${(mc * 5 + 2.5) * dx} ${nrow * dy + 18}l6 9h-12z" fill="var(--now)"><title>your clock's zone</title></path>`;
    const dst = c => c.kind === 2 ? "varies" : c.kind === 1 ? (c.on ? "summer now" : "winter now") : "";
    el.innerHTML = `<div class="card">
      <div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px;align-items:baseline">
        <h2 style="font-size:22px">${zfo(Z.off)}${Z.off === -12 ? " at sea" : ""} ${sw}</h2>
        <div style="font-size:26px;font-weight:700;font-variant-numeric:tabular-nums">${zlocal(J, c0.off)}</div></div>
      <div class="meta">${Z.cities.length} ${Z.cities.length === 1 ? "place" : "places"} · stop ${zcur + 1} of ${stops.length} ·
        your clock ${zfo(here)} (▲) · IANA tzdata ${N().str(Module._api_tzdata())} · summer time computed as on the calculator</div>
      <div class="chips"><button id="zw">◀ west</button><button id="ze">east ▶</button><button id="zm">your zone</button>
        <span class="chip" style="color:var(--now);font-weight:600">${c0.name}${c0.cc ? " " + c0.cc : ""}${c0.on ? " · summer time " + zfo(c0.off) : c0.kind === 2 ? " · DST varies" : c0.kind === 1 ? " · standard time now" : ""}</span></div>
      <svg id="zmap" viewBox="0 0 ${W} ${H}" style="margin-top:8px">${g}</svg>
      <div class="scroll" style="max-height:420px"><table><tr><th>Place</th><th></th><th>Local time</th><th class="num">Offset now</th><th>Summer time</th></tr>
      ${Z.cities.map((c, k) => `<tr class="click${k === zcity ? " sel" : ""}" data-k="${k}"><td>${c.name}</td><td class="meta">${c.cc}</td>
        <td class="mono">${zlocal(J, c.off)}</td><td class="num mono">${zfo(c.off)}</td><td>${dst(c)}</td></tr>`).join("")}</table></div>
      <div class="meta">Keys: ← → the stop west / east, ↑ ↓ the place. Click a column of the map for its zone. The stops: every official
        standard offset plus −12 at sea; the highlighted column is the offset's own 15°. The calculator's ZONES page, in the browser.</div>
</div>`;
    const go = k => { zcur = ((k % stops.length) + stops.length) % stops.length; zcity = 0; localStorage.setItem("nav47web:zone", zcur); zones(); };
    el.querySelector("#zw").onclick = () => go(zcur - 1);
    el.querySelector("#ze").onclick = () => go(zcur + 1);
    el.querySelector("#zm").onclick = () => go(mine);
    el.querySelectorAll("tr[data-k]").forEach(tr => tr.onclick = () => { zcity = +tr.dataset.k; zones(); });
    bindSw();
    el.querySelectorAll("rect[data-c]").forEach(r => r.onclick = () => {
      const c = +r.dataset.c, k = stops.findIndex(z => z.col === c && Number.isInteger(z.off));
      go(k >= 0 ? k : stops.findIndex(z => z.col === c));
    });
    if (!zkeys) {
      zkeys = true;
      document.addEventListener("keydown", e => {
        if (document.getElementById("t-zones").hidden || /INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
        if ((localStorage.getItem("nav47web:zview") || "map") !== "map") return;
        const n = zdata(jd()).length;
        if (e.key === "ArrowLeft") { zcur = (zcur - 1 + n) % n; zcity = 0; }
        else if (e.key === "ArrowRight") { zcur = (zcur + 1) % n; zcity = 0; }
        else if (e.key === "ArrowDown") zcity++;
        else if (e.key === "ArrowUp") zcity = Math.max(0, zcity - 1);
        else return;
        e.preventDefault(); localStorage.setItem("nav47web:zone", zcur); zones();
      });
    }
  }
  /* ---------------------------------------------------------------- Flow: your FLOW atlas (as T&F), the stream stations */
  let fStep = 0, fHour = -1, fWind = { dir: 0, kn: 0 };
  function flow() {
    const el = document.getElementById("t-flow"), [lat, lon] = S().pos, J = jd(), have = Module._api_flw_have();
    let h = "";
    if (have) {
      Module._api_flow(lat, lon, J, fWind.dir, fWind.kn, fStep);
      const o = Array.from(N().o().subarray(0, 40)), ref = N().str(Module._api_flw_ref()), src = N().str(Module._api_flw_source());
      if (!o[36]) h += `<div class="card"><h2>Flow</h2><div class="meta" style="color:var(--now)">The atlas's reference station, ${esc(ref)}, is not loaded: load its .TID file below.</div></div>`;
      else {
        const tz = o[1], t0 = o[2], day = o[3], off = o[7], inAtlas = !!o[0];
        const hwJd = Math.floor(J + tz / 24 + 0.5) - 0.5 + day + t0 / 24 - tz / 24;   /* the HW, UT */
        const zl = `UTC${tz >= 0 ? "+" : ""}${tz}`;
        const now = off >= -6 && off <= 6 ? Math.round(off) + 6 : -1;
        const rows = Array.from({ length: 13 }, (_, k) => ({ k, set: o[8 + 2 * k], kn: o[9 + 2 * k], t: F().hm(((t0 + k - 6) % 24 + 24) % 24) }));
        const svgArrow = (set, kn) => { const L = 8 + 9 * Math.min(kn, 2) / 2, a = set * Math.PI / 180;
          return `<svg width="26" height="26" viewBox="-13 -13 26 26" style="display:inline-block;width:26px;vertical-align:middle"><g transform="rotate(${set})"><line x1="0" y1="${L / 2}" x2="0" y2="${-L / 2}" stroke="${APP.rateCol(kn)}" stroke-width="2.4"/><path d="M0 ${-L / 2 - 1}l-4 6h8z" fill="${APP.rateCol(kn)}"/></g></svg>`; };
        h += `<div class="card"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px"><h2>Flow · ${esc(src || "FLOW atlas")}</h2>
          <span class="meta">DR ${F().pos(lat, lon)} · reference ${esc(ref)}: HW ${F().dt(hwJd, tz)} ${zl} · ${o[4].toFixed(2)} m · range ${o[5].toFixed(2)} m · springs × ${o[6].toFixed(2)}</span></div>
          <div class="controls"><button id="fprev">← HW</button><button id="fnow">this tide</button><button id="fnext">HW →</button>
            <label>Wind from <input class="num" id="fwd" type="number" min="0" max="360" step="10" value="${fWind.dir}">°</label>
            <label><input class="num" id="fwk" type="number" min="0" max="80" step="1" value="${fWind.kn}"> kn</label></div>
          ${inAtlas ? `<div class="two" style="margin-top:8px"><div><table><tr><th></th><th>${zl}</th><th></th><th class="num">Set</th><th class="num">Rate</th></tr>
            ${rows.map(r => `<tr class="click${r.k === now ? " sel" : ""}${r.k === fHour ? " sel" : ""}" data-fh="${r.k}"><td>${r.k === 6 ? "HW" : (r.k < 6 ? "" : "+") + (r.k - 6)}</td>
              <td class="mono">${r.t}</td><td>${svgArrow(r.set, r.kn)}</td><td class="num mono">${String(Math.round(r.set) % 360).padStart(3, "0")}°</td>
              <td class="num mono">${r.kn.toFixed(2)} kn</td></tr>`).join("")}</table>
            <div class="chips">${o[35] >= 0 ? `<span class="chip" style="color:var(--now);font-weight:600">now ${String(Math.round(o[34]) % 360).padStart(3, "0")}° ${o[35].toFixed(2)} kn</span>` : `<span class="chip">now: outside this tide (← →)</span>`}</div></div>
            <div><canvas id="fmap" style="width:100%;height:440px;border:1px solid var(--line);border-radius:10px;background:var(--sea)"></canvas>
            <div class="controls" style="margin-top:4px"><label>Hour <select id="fhour"><option value="-1">now</option>${rows.map(r => `<option value="${r.k}" ${r.k === fHour ? "selected" : ""}>${r.k === 6 ? "HW" : (r.k < 6 ? "HW" : "HW+") + (r.k - 6)} ${r.t}</option>`).join("")}</select></label>
              <span class="meta">the atlas round the DR (+), the selected hour</span></div></div></div>`
          : `<div class="meta" style="color:var(--now);margin-top:6px">The DR is outside the atlas (or on land): set the DR at the top inside it.</div>`}
          <div class="meta">The calculator's own T&amp;F (Tide 47's FLOW47): the atlas's hourly streams from the reference port's High Water, scaled
            for springs and neaps, the wind added. Not for navigation.</div></div>`;
      }
    } else h += `<div class="card"><h2>Flow</h2><div class="meta">No FLOW atlas loaded: load your <span class="mono">FLOW.FLW</span> below
      (with the .TID of its reference port). Meanwhile, the stream stations near the DR.</div></div>`;
    /* the stream stations near the DR */
    const near = APP.streams.all().map(s => ({ s, d: N().inv(lat, lon, s.lat, s.lon) })).filter(x => x.d.s < 500 * 1852)
      .sort((a, b) => a.d.s - b.d.s).slice(0, 14);
    h += `<div class="card"><h2>Stream stations near the DR</h2><table><tr><th>Station</th><th></th><th class="num">Distance</th><th class="num">Bearing</th><th class="num">Now</th><th></th></tr>
      ${near.map(({ s, d }) => { const q = APP.streams.now(s.i, J) || { kn: 0, set: 0 };
        return `<tr class="click" data-st="${s.i}"><td>${esc(s.name)}</td><td class="meta">${s.src === 2 ? "yours" : s.src === 1 ? "NOAA" : "NOAA (Tide 47)"}${s.kind === 2 ? ", rotary" : ""}</td>
        <td class="num mono">${(d.s / 1852).toFixed(0)} NM</td><td class="num mono">${String(Math.round(((d.a1 % 360) + 360) % 360)).padStart(3, "0")}°</td>
        <td class="num mono">${String(Math.round(((q.set % 360) + 360) % 360)).padStart(3, "0")}° ${Math.abs(q.kn).toFixed(2)} kn</td>
        <td>${s.kind === 1 ? (q.kn >= 0 ? "flood" : "ebb") : ""}</td></tr>`; }).join("")}</table>
      ${near.length ? "" : `<div class="meta">None within 500 NM of the DR.</div>`}
      <div class="meta">Click a station for its day (Tide tab). NOAA's harmonic current stations (public domain); the chart shows them with 〰 Streams.</div></div>`;
    h += APP.tfiles.card();
    el.innerHTML = h;
    APP.tfiles.bind(el, flow);
    el.querySelectorAll("tr[data-st]").forEach(tr => tr.onclick = () => { tideSt = +tr.dataset.st; tideDay = 0; APP.showTab("tide"); });
    const b = id => document.getElementById(id);
    if (b("fprev")) {
      b("fprev").onclick = () => { fStep--; flow(); }; b("fnext").onclick = () => { fStep++; flow(); }; b("fnow").onclick = () => { fStep = 0; flow(); };
      b("fwd").onchange = e => { fWind.dir = +e.target.value || 0; flow(); }; b("fwk").onchange = e => { fWind.kn = +e.target.value || 0; flow(); };
      el.querySelectorAll("tr[data-fh]").forEach(tr => tr.onclick = () => { fHour = +tr.dataset.fh; flow(); });
    }
    if (b("fhour")) { b("fhour").value = fHour; b("fhour").onchange = e => { fHour = +e.target.value; flowMap(); }; }
    flowMap();
  }
  /* the atlas round the DR: the coast, the cells' arrows at the selected hour */
  function flowMap() {
    const cv = document.getElementById("fmap"); if (!cv) return;
    const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    cv.width = r.width * dpr; cv.height = r.height * dpr;
    const g = cv.getContext("2d"); g.scale(dpr, dpr);
    Module._api_flw_grid(); const o = N().o(), la0 = o[0], lo0 = o[1], st = o[2] / 60, nla = o[3], nlo = o[4];
    const [lat, lon] = S().pos, half = Math.max(0.3, Math.min(1.2, 14 * st));
    const s0 = lat - half, n0 = lat + half, c = Math.cos(lat * Math.PI / 180), w0 = lon - half * r.width / r.height / c, e0 = lon + half * r.width / r.height / c;
    const K = r.height / (n0 - s0), X = lo => (lo - w0) * c * K, Y = la => (n0 - la) * K;
    const polys = APP.loadCoast("L"), css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
    g.fillStyle = css("--land"); g.strokeStyle = css("--coast"); g.lineWidth = 0.8;
    for (const P of polys) {
      if (P.n < s0 || P.s > n0 || P.e < w0 || P.w > e0) continue;
      g.beginPath(); for (let j = 0; j < P.lat.length; j++) j ? g.lineTo(X(P.lon[j]), Y(P.lat[j])) : g.moveTo(X(P.lon[j]), Y(P.lat[j]));
      g.closePath(); g.fill(); g.stroke();
    }
    for (let a = 0; a < nla; a++) for (let b = 0; b < nlo; b++) {
      const la = la0 + (a + 0.5) * st, lo = lo0 + (b + 0.5) * st;
      if (la < s0 || la > n0 || lo < w0 || lo > e0) continue;
      if (!Module._api_flow_at(la, lo, fHour)) continue;
      const q = N().o(), kn = q[1];
      g.strokeStyle = APP.rateCol(kn); g.lineWidth = 1.8; APP.arrowPath(g, X(lo), Y(la), q[0], 5 + 7 * Math.min(kn, 2.5));
    }
    g.strokeStyle = css("--now"); g.lineWidth = 2.5; const x = X(lon), y = Y(lat);
    g.beginPath(); g.moveTo(x - 9, y); g.lineTo(x + 9, y); g.moveTo(x, y - 9); g.lineTo(x, y + 9); g.stroke();
    g.fillStyle = css("--soft"); g.font = "11px system-ui";
    g.fillText("< 0.5 kn blue · 1.5 green · 3 amber · more red", 8, r.height - 8);
  }
  return { almanac, sky, tide, zones, flow };
})();
