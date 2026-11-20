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
      return { i, name: N().str(Module._api_tide_name(i)), region: N().str(Module._api_tide_region(i)), lat: o[0], lon: o[1], tz: o[2], kind: o[3] };
    });
    if (tideSt === null) {                                    /* the nearest to the DR */
      const [la, lo] = S().pos;
      let best = 1e18;
      for (const s of list) { const d = N().inv(la, lo, s.lat, s.lon).s; if (d < best) { best = d; tideSt = s.i; } }
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
    el.innerHTML = `<div class="card"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px"><h2>Tide · ${st.name}</h2>
      <span class="meta">${String(d).padStart(2, "0")}-${String(m).padStart(2, "0")}-${y} · zone time UTC${tz >= 0 ? "+" : ""}${tz} · ${F().pos(st.lat, st.lon)}</span></div>
      <div class="controls"><select id="tst">${regions.map(r => `<optgroup label="${r}">${list.filter(s => s.region === r).map(s => `<option value="${s.i}" ${s.i === st.i ? "selected" : ""}>${s.name}</option>`).join("")}</optgroup>`).join("")}</select>
        <button id="tprev">← day</button><button id="ttoday">today</button><button id="tnext">day →</button></div>
      <div class="two" style="margin-top:8px"><svg viewBox="0 0 ${W} ${H}">${g}</svg>
      <div><table><tr><th></th><th class="num">Time</th><th class="num">${stream ? "Rate" : "Height"}</th>${stream ? '<th class="num">Set</th>' : ""}</tr>
      ${ev.map(e => `<tr><td>${stream ? (e.kind > 0 ? "Flood max" : e.kind < 0 ? "Ebb max" : "Slack") : (e.kind > 0 ? "HW" : "LW")}</td><td class="num mono">${F().hm(e.t)}</td>
        <td class="num mono">${e.v.toFixed(2)} ${unit}</td>${stream ? `<td class="num mono">${Math.round(e.set)}°</td>` : ""}</tr>`).join("")}</table>
      <div class="meta">The calculator's own tide engine (Tide 47, the Schureman harmonic method), the public stations (NOAA, TICON-3).
        ${stream ? "Rates in knots." : "Heights above the chart datum, metres."} Not for navigation: check against the official tide tables.</div></div></div></div>`;
    document.getElementById("tst").onchange = e => { tideSt = +e.target.value; tide(); };
    document.getElementById("tprev").onclick = () => { tideDay--; tide(); };
    document.getElementById("tnext").onclick = () => { tideDay++; tide(); };
    document.getElementById("ttoday").onclick = () => { tideDay = 0; tide(); };
  }

  /* ---------------------------------------------------------------- Zones */
  function zones() {
    const el = document.getElementById("t-zones"), n = Module._api_zone_count(), J = jd(), here = -new Date().getTimezoneOffset() / 60;
    const rows = [];
    for (let i = 0; i < n; i++) {
      const base = Module._api_zone_off(i), nc = Module._api_zone_cities(i), cities = [];
      for (let j = 0; j < nc; j++) {
        Module._api_city_off(i, j, J); const o = N().o();
        cities.push({ name: N().str(Module._api_city_name(i, j)), cc: N().str(Module._api_city_cc(i, j)), off: o[0], on: !!o[1] });
      }
      rows.push({ base, cities });
    }
    const hm = o => { const t = new Date((J - 2440587.5) * 864e5 + o * 36e5); return String(t.getUTCHours()).padStart(2, "0") + ":" + String(t.getUTCMinutes()).padStart(2, "0"); };
    const fo = o => "UTC" + (o === 0 ? "" : (o > 0 ? "+" : "−") + (Math.floor(Math.abs(o)) + (Math.abs(o) % 1 ? ":" + String(Math.round(Math.abs(o) % 1 * 60)).padStart(2, "0") : "")));
    el.innerHTML = `<div class="card"><div style="display:flex;justify-content:space-between;flex-wrap:wrap;gap:6px"><h2>Zones</h2>
      <span class="meta">${F().dt(J, 0)} UT · tzdata ${N().str(Module._api_tzdata())} · summer time computed as on the calculator</span></div>
      <div class="scroll"><table><tr><th>Zone</th><th class="num">Now</th><th>Places (★ summer time now)</th></tr>
      ${rows.map(r => `<tr class="${r.base === here ? "sel" : ""}"><td class="mono">${fo(r.base)}</td><td class="num mono">${hm(r.base)}</td>
        <td style="white-space:normal">${r.cities.map(c => `${c.name}${c.cc ? " " + c.cc : ""}${c.on ? ` ★ ${fo(c.off)} ${hm(c.off)}` : ""}`).join(", ")}</td></tr>`).join("")}</table></div>
      <div class="meta">ZONE47: the official offsets (−12 at sea); this computer's zone highlighted. The calculator's own zones engine.</div></div>`;
  }
  return { almanac, sky, tide, zones };
})();
