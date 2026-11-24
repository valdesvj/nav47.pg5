/* compass.js - NAV47 on the web: the Compass tab, the calculator's COMPASS and CARD pages: a body's true azimuth
 * against the gyro and the magnetic compass (the gyro error, the true heading, the compass error, the variation, the
 * deviation), and the deviation cards, the ship's original and the corrected (the checks saved into it), drawn over
 * each other, fitted with the five coefficients, the advice to compensate over 5 degrees. The same C as the
 * calculator (src/compass.c through api.c). DEVCARD.CSV and DEVCORR.CSV as on the calculator's disk.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
"use strict";
const COMPASS = (() => {
  const $ = id => document.getElementById(id), St = () => APP.state, KEY = "nav47web:";
  const load = (k, d) => { try { const v = localStorage.getItem(KEY + k); return v ? Object.assign(d, JSON.parse(v)) : d; } catch (e) { return d; } };
  const put = (k, v) => { try { localStorage.setItem(KEY + k, JSON.stringify(v)); } catch (e) { /* full */ } };
  const O = load("compassobs", { man: false, lat: null, lon: null, jd: null, body: 0, gb: null, gh: null, ch: null });
  const CARD = load("devcards", { o: [], c: [], k: "o" });  /* [heading, deviation (+ E)] each */
  const NAMES = ["original", "corrected"], FILE = { o: "DEVCARD.CSV", c: "DEVCORR.CSV" };
  const d3 = v => { const x = Math.round((((v % 360) + 360) % 360) * 10) / 10; return Number.isInteger(x) ? String(x).padStart(3, "0") : x.toFixed(1).padStart(5, "0"); };   /* 045, 041.5 */
  const deg = v => v === null || !isFinite(v) ? "–" : (((v % 360) + 360) % 360).toFixed(1).padStart(5, "0") + "°";
  const ew = v => v === null || !isFinite(v) ? "–" : Math.abs(v).toFixed(1) + "°" + (v >= 0 ? "E" : "W");
  const num = s => { if (s === null || s === undefined || String(s).trim() === "") return null; const v = parseFloat(String(s).replace(",", ".")); return isFinite(v) ? v : null; };
  /* a deviation typed: 1.5, 1.5E, -2, 2W, 2.0 W */
  const devOf = s => { const m = String(s).trim().toUpperCase().match(/^([+-]?\d+(?:[.,]\d+)?)\s*([EW])?$/); if (!m) return null; let v = parseFloat(m[1].replace(",", ".")); if (m[2] === "W") v = -Math.abs(v); if (m[2] === "E") v = Math.abs(v); return v; };

  /* ---------------------------------------------------------------- the engine (the calculator's C) */
  function bodies(jd, lat, lon) {
    const n = Module._api_alm(jd, lat, lon, 0), o = APP.nav.o(), list = [];
    const PL = ["Venus", "Mars", "Jupiter", "Saturn"];
    const add = (id, k, name) => { const hc = o[4 * k + 2], zn = o[4 * k + 3]; list.push({ id, name, hc, zn }); };
    add(0, 0, "Sun"); add(-1, 1, "Moon"); PL.forEach((p, i) => add(-(i + 2), 2 + i, p));
    for (let r = 0; r < 58; r++) { const s = Module._api_bright(r); add(s, 5 + s, Module.UTF8ToString(Module._api_star_name(s)) + " (" + s + ")"); }
    return list.filter(b => b.hc > (b.id >= -1 && b.id <= 0 ? -1 : 0));
  }
  function fit(card) {
    const n = Math.min(card.length, 36), IN = new Float64Array(Module.HEAPF64.buffer, Module._api_in(), 2 * n);
    card.slice(0, n).forEach(([h, d], i) => { IN[i] = h; IN[n + i] = d; });
    Module._api_devfit(n); const o = APP.nav.o();
    return { n, c: Array.from(o.subarray(0, 5)), fitted: !!o[5], max: o[6], at: o[7], rms: o[8], curve: Array.from(o.subarray(16, 376)),
             advice: n ? Module.UTF8ToString(Module._api_devadvice()) : "" };
  }
  const devAt = (f, h) => f.n ? f.curve[Math.round(((h % 360) + 360) % 360) % 360] : null;
  function observe() {
    const lat = O.man && O.lat !== null ? O.lat : St().pos[0], lon = O.man && O.lon !== null ? O.lon : St().pos[1];
    const jd = O.jd || nowJd(), list = bodies(jd, lat, lon), b = list.find(x => x.id === O.body) || list[0];
    const g = typeof GPS !== "undefined" && GPS.state ? GPS.state() : null;
    const gyro = O.gh !== null ? O.gh : g && g.hdgAge >= 0 && g.hdgAge < 5 ? g.hdg : null;
    const v = Module._api_var(lat, lon, jd);
    Module._api_cmp(b ? b.zn : NaN, O.gb ?? NaN, gyro ?? NaN, O.ch ?? NaN, v); const o = APP.nav.o();
    return { lat, lon, jd, list, b, gyro, gyroFromGps: O.gh === null && gyro !== null, v, err: o[0], th: o[1], ce: o[2], dev: o[3] };
  }

  /* ---------------------------------------------------------------- the observation */
  function obsCard() {
    const el = $("cpobs"); if (!el) return;
    const r = observe(), f = { o: fit(CARD.o), c: fit(CARD.c) };
    const iso = new Date(msOf(r.jd)).toISOString().slice(0, 19);
    el.innerHTML = `<h2>Compass <span class="meta">a body's azimuth against the gyro and the magnetic compass</span></h2>
      <div class="controls"><label><input type="radio" name="cppos" value="dr" ${O.man ? "" : "checked"}> the top bar's position (the GPS's when Position is Auto)</label>
        <label><input type="radio" name="cppos" value="man" ${O.man ? "checked" : ""}> typed <input class="pos" id="cppos" value="${FMT.pos(r.lat, r.lon)}" ${O.man ? "" : "disabled"}></label></div>
      <div class="controls" style="margin-top:6px"><label>Time UT <input type="datetime-local" id="cpt" step="1" value="${iso}"></label>
        <button class="small ${O.jd ? "" : "on"}" id="cpnow" title="the time follows the clock (the gyro bearing typed fixes it)">now</button>
        <label>Body <select id="cpbody">${r.list.map(b => `<option value="${b.id}" ${r.b && b.id === r.b.id ? "selected" : ""}>${b.name} · Zn ${b.zn.toFixed(1)}° · Hc ${b.hc.toFixed(1)}°</option>`).join("")}</select></label></div>
      <div class="controls" style="margin-top:6px">
        <label>Gyro bearing <input class="num" id="cpgb" value="${O.gb ?? ""}" placeholder="°"></label>
        <label>Gyro heading <input class="num" id="cpgh" value="${O.gh ?? ""}" placeholder="${r.gyroFromGps ? r.gyro.toFixed(1) + " GPS" : "°"}"></label>
        <label>Compass course <input class="num" id="cpch" value="${O.ch ?? ""}" placeholder="°"></label></div>
      <table style="margin-top:8px;max-width:520px">
        <tr><td>${r.b ? r.b.name : "–"} true azimuth Zn</td><td class="num mono">${r.b ? deg(r.b.zn) : "–"}</td><td>altitude Hc</td><td class="num mono">${r.b ? r.b.hc.toFixed(1) + "°" : "–"}</td></tr>
        <tr><td><b>Gyro error</b></td><td class="num mono"><b>${isFinite(r.err) ? Math.abs(r.err).toFixed(1) + "° " + (r.err > 0 ? "LOW" : r.err < 0 ? "HIGH" : "") : "–"}</b></td><td>true heading</td><td class="num mono">${deg(r.th)}</td></tr>
        <tr><td>Variation (WMM)</td><td class="num mono">${ew(r.v)}</td><td>compass error</td><td class="num mono">${ew(r.ce)}</td></tr>
        <tr style="background:var(--chip)"><td><b>Deviation</b></td><td class="num mono"><b>${ew(r.dev)}</b></td><td>at compass</td><td class="num mono">${deg(O.ch)}</td></tr>
        <tr><td>the original card there</td><td class="num mono">${O.ch !== null ? ew(devAt(f.o, O.ch)) : "–"}</td><td>the corrected card</td><td class="num mono">${O.ch !== null ? ew(devAt(f.c, O.ch)) : "–"}</td></tr>
      </table>
      <div class="controls" style="margin-top:8px"><button id="cpsave" ${isFinite(r.dev) ? "" : "disabled"}>Save to the corrected card</button><button id="cpnew">A new observation</button><span class="meta" id="cpmsg"></span></div>
      <div class="meta">Gyro error = Zn − the gyro bearing (+ LOW: add it); true heading = the gyro heading + the error; compass error = the true heading − the
        compass course; deviation = the compass error − the variation (east +). A low body gives the best bearing.</div>`;
    const save = () => { put("compassobs", O); obsCard(); };
    document.querySelectorAll("[name=cppos]").forEach(x => x.onchange = () => { O.man = x.value === "man" && x.checked; if (O.man) { O.lat = r.lat; O.lon = r.lon; } save(); });
    $("cppos").onchange = e => { const p = APP.parsePos(e.target.value); if (p) { O.lat = p[0]; O.lon = p[1]; } save(); };
    $("cpt").onchange = e => { const t = Date.parse(e.target.value + "Z"); O.jd = isFinite(t) ? jdOf(t) : null; save(); };
    $("cpnow").onclick = () => { O.jd = null; save(); };
    $("cpbody").onchange = e => { O.body = +e.target.value; save(); };
    $("cpgb").onchange = e => { O.gb = num(e.target.value); if (O.gb !== null && !O.jd) O.jd = nowJd(); save(); };   /* the bearing's time */
    $("cpgh").onchange = e => { O.gh = num(e.target.value); save(); };
    $("cpch").onchange = e => { O.ch = num(e.target.value); save(); };
    $("cpnew").onclick = () => { O.jd = null; O.gb = null; save(); };
    $("cpsave").onclick = () => {
      const h = ((O.ch % 360) + 360) % 360, i = CARD.c.findIndex(([x]) => Math.abs(((x - h + 540) % 360) - 180) < 2.5);
      if (i >= 0) CARD.c[i][1] = r.dev; else CARD.c.push([h, r.dev]);
      CARD.c.sort((a, b) => a[0] - b[0]); put("devcards", CARD); cardCard();
      $("cpmsg").textContent = `saved ${d3(h)} ${ew(r.dev)} into the corrected card`;
    };
  }

  /* ---------------------------------------------------------------- the cards */
  function plot(cv, fo, fc) {
    const dpr = window.devicePixelRatio || 1, W = cv.clientWidth, H = 260;
    cv.width = W * dpr; cv.height = H * dpr; cv.style.height = H + "px";
    const g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
    let m = 6; for (const [f, c] of [[fo, CARD.o], [fc, CARD.c]]) { if (f.n) m = Math.max(m, Math.abs(f.max) + 1); c.forEach(([, d]) => m = Math.max(m, Math.abs(d) + 1)); }
    m = Math.ceil(m);
    const L = 40, R = W - 10, T = 10, B = H - 26, x = h => L + (R - L) * h / 360, y = d => (T + B) / 2 - d / m * (B - T) / 2;
    g.fillStyle = css("--card"); g.fillRect(0, 0, W, H);
    g.strokeStyle = css("--line"); g.lineWidth = 1; g.strokeRect(L, T, R - L, B - T);
    g.font = "12px system-ui"; g.fillStyle = css("--soft");
    for (let h = 0; h <= 360; h += 45) { g.beginPath(); g.moveTo(x(h), T); g.lineTo(x(h), B); g.stroke(); g.fillText(String(h).padStart(3, "0"), x(h) - 10, H - 8); }
    for (let d = -m; d <= m; d += m > 10 ? 5 : 2) { g.fillText((d > 0 ? d + "E" : d < 0 ? -d + "W" : "0"), 4, y(d) + 4); }
    g.strokeStyle = css("--soft"); g.beginPath(); g.moveTo(L, y(0)); g.lineTo(R, y(0)); g.stroke();
    g.setLineDash([3, 4]); g.strokeStyle = css("--now");
    for (const s of [5, -5]) { g.beginPath(); g.moveTo(L, y(s)); g.lineTo(R, y(s)); g.stroke(); }
    const line = (f, col, dash) => { if (!f.n) return; g.setLineDash(dash); g.strokeStyle = col; g.lineWidth = 2.5; g.beginPath();
      for (let h = 0; h <= 360; h++) { const v = f.curve[h % 360], X = x(h), Y = y(v); h ? g.lineTo(X, Y) : g.moveTo(X, Y); } g.stroke(); g.setLineDash([]); };
    line(fo, css("--track"), []); line(fc, css("--sun"), [8, 5]);
    const pts = (c, col, box) => { g.fillStyle = col; for (const [h, d] of c) { const X = x(h), Y = y(d); if (box) g.fillRect(X - 4, Y - 4, 8, 8); else { g.beginPath(); g.arc(X, Y, 4.5, 0, 7); g.fill(); } } };
    pts(CARD.o, css("--track"), true); pts(CARD.c, css("--sun"), false);
    g.fillStyle = css("--ink"); g.font = "13px system-ui";
    g.fillText("■ original", L + 8, T + 16); g.fillStyle = css("--sun"); g.fillText("● corrected (dashed)", L + 90, T + 16); g.fillStyle = css("--now"); g.fillText("±5°", R - 34, y(5) - 4);
  }
  function cardCard() {
    const el = $("cpcard"); if (!el) return;
    const k = CARD.k, other = k === "o" ? "c" : "o", fo = fit(CARD.o), fc = fit(CARD.c), fk = k === "o" ? fo : fc, fx = k === "o" ? fc : fo;
    const coef = f => !f.n ? "empty" : f.fitted ? `A ${f.c[0].toFixed(1)} · B ${f.c[1].toFixed(1)} · C ${f.c[2].toFixed(1)} · D ${f.c[3].toFixed(1)} · E ${f.c[4].toFixed(1)} · max ${ew(f.max)} at ${d3(f.at)}° · rms ${f.rms.toFixed(2)}°`
      : `${f.n} heading${f.n > 1 ? "s" : ""}: 5 or more round the card for its coefficients · max ${ew(f.max)}`;
    const adv = f => f.n && f.advice ? `<div class="chip warn" style="display:block;white-space:pre-line;margin-top:4px;border-radius:8px">${f.advice.replace(/</g, "&lt;")}</div>` : f.n ? `<div class="chip ok" style="display:inline-block;margin-top:4px">within 5°: no compensation needed</div>` : "";
    el.innerHTML = `<h2>Deviation cards <span class="meta">the ship's original and the corrected (your checks), over each other</span></h2>
      <canvas id="cpplot" style="width:100%;display:block;border-radius:8px"></canvas>
      <div style="margin-top:6px"><b>Original</b> <span class="meta">${coef(fo)}</span>${adv(fo)}</div>
      <div style="margin-top:6px"><b>Corrected</b> <span class="meta">${coef(fc)}</span>${adv(fc)}</div>
      <div class="controls" style="margin-top:10px"><label><input type="radio" name="cpk" value="o" ${k === "o" ? "checked" : ""}> edit the original</label>
        <label><input type="radio" name="cpk" value="c" ${k === "c" ? "checked" : ""}> edit the corrected</label></div>
      <div class="scroll" style="max-height:320px"><table><tr><th>Compass heading</th><th>Deviation</th><th class="num">the ${NAMES[other === "o" ? 0 : 1]} there</th><th class="num">difference</th><th></th></tr>
        ${CARD[k].map(([h, d], i) => `<tr><td><input class="num" data-ch="${i}" value="${d3(h)}"></td><td><input class="num" data-cd="${i}" value="${ew(d).replace("°", "")}"></td>
          <td class="num mono">${fx.n ? ew(devAt(fx, h)) : "–"}</td><td class="num mono">${fx.n ? ew(d - devAt(fx, h)) : "–"}</td><td><button class="small" data-cx="${i}">✕</button></td></tr>`).join("")}
        <tr><td><input class="num" id="cpnh" placeholder="hdg"></td><td><input class="num" id="cpnd" placeholder="1.5E / 2W"></td><td colspan="3"><button class="small" id="cpadd">＋ add</button></td></tr></table></div>
      <div class="controls" style="margin-top:6px"><textarea id="cppaste" rows="2" style="flex:1;min-width:240px;font:13px ui-monospace,monospace" placeholder="paste a card: 000 1.5E, 045 3.0E, 090 5.5E, 135 4E …"></textarea><button id="cpread">Read into the ${NAMES[k === "o" ? 0 : 1]}</button></div>
      <div class="controls" style="margin-top:6px"><button id="cpdl">${FILE[k]}</button><label>Load <input type="file" id="cpload" accept=".csv,.txt"></label>
        <button id="cpadopt" title="after the compensation: the corrected card becomes the original">Adopt the corrected as the original</button><button id="cpclr">Clear the ${NAMES[k === "o" ? 0 : 1]}</button>
        <button id="cpcalc" title="DEVCARD.CSV and DEVCORR.CSV to the Calculator tab's disk (its COMPASS and CARD pages)">To the calculator</button></div>
      <div class="meta">The five coefficients: dev = A + B sin h + C cos h + D sin 2h + E cos 2h (h the compass heading), fitted with 5 headings or more round the card;
        fewer: between the card's points. Over 5° the advice, the largest coefficients first: by a compass adjuster, then a new card. Planning aid, not the adjuster's report.</div>`;
    plot($("cpplot"), fo, fc);
    const save = () => { CARD[k].sort((a, b) => a[0] - b[0]); put("devcards", CARD); cardCard(); obsCard(); };
    document.querySelectorAll("[name=cpk]").forEach(x => x.onchange = () => { CARD.k = x.value; put("devcards", CARD); cardCard(); });
    el.querySelectorAll("[data-ch]").forEach(i => i.onchange = () => { const v = num(i.value); if (v !== null && v >= 0 && v < 360) CARD[k][+i.dataset.ch][0] = v; save(); });
    el.querySelectorAll("[data-cd]").forEach(i => i.onchange = () => { const v = devOf(i.value); if (v !== null) CARD[k][+i.dataset.cd][1] = v; save(); });
    el.querySelectorAll("[data-cx]").forEach(b => b.onclick = () => { CARD[k].splice(+b.dataset.cx, 1); save(); });
    $("cpadd").onclick = () => { const h = num($("cpnh").value), d = devOf($("cpnd").value); if (h === null || d === null || h < 0 || h >= 360 || CARD[k].length >= 36) return; CARD[k].push([h, d]); save(); };
    $("cpread").onclick = () => { const t = $("cppaste").value, out = [];
      for (const m of t.matchAll(/(\d{1,3}(?:[.,]\d+)?)\s*[:=]?\s*([+-]?\d+(?:[.,]\d+)?\s*[EW]?)/gi)) { const h = num(m[1]), d = devOf(m[2]); if (h !== null && h < 360 && d !== null) out.push([h, d]); }
      if (out.length) { CARD[k] = out.slice(0, 36); save(); } };
    $("cpdl").onclick = () => APP.download(FILE[k], csv(k));
    $("cpload").onchange = e => { const f = e.target.files[0]; if (f) f.text().then(t => { CARD[k] = parseCsv(t); save(); }); };
    $("cpadopt").onclick = () => { if (!CARD.c.length || !confirm("The corrected card becomes the original (after the compensation)?")) return; CARD.o = CARD.c.map(x => x.slice()); CARD.k = "o"; save(); };
    $("cpclr").onclick = () => { if (confirm(`Clear the ${NAMES[k === "o" ? 0 : 1]} card?`)) { CARD[k] = []; save(); } };
    $("cpcalc").onclick = () => { localStorage.setItem("nav47fs:/simdisk/NAV47/DEVCARD.CSV", csv("o"));   /* the Calculator tab's disk (calc.js) */ CALC.putAndRestart("DEVCORR.CSV", csv("c")); };
  }
  const csv = k => `# NAV47 deviation card, ${k === "o" ? "ORIGINAL" : "CORRECTED"}: compass heading, deviation (+ east)\n` + CARD[k].map(([h, d]) => `${h.toFixed(1)},${d.toFixed(2)}`).join("\n") + "\n";
  const parseCsv = t => t.split(/\r?\n/).filter(l => l && l[0] !== "#").map(l => l.split(/[,;\t ]+/)).map(([h, d]) => [num(h), devOf(d || "")]).filter(([h, d]) => h !== null && d !== null && h >= 0 && h < 360).slice(0, 36).sort((a, b) => a[0] - b[0]);

  /* #cpdemo: the manual's picture, a made-up card and checks, the Sun observed */
  if (/cpdemo/.test(location.hash)) {
    CARD.o = [[0, 1.5], [45, 3], [90, 5.5], [135, 4], [180, 0.5], [225, -3.5], [270, -6], [315, -2.5]]; CARD.c = [[0, 0.5], [90, 2], [180, -0.5], [270, -2.5]]; CARD.k = "c";
    Object.assign(O, { man: true, lat: 26.5, lon: 50 + 10 / 60, jd: jdOf(Date.UTC(2026, 10, 24, 6, 0, 0)), body: 0, gb: 140.8, gh: 45, ch: 41 });
  }
  function show() {
    const el = $("t-compass");
    if (!el.dataset.built) { el.innerHTML = `<div class="two"><div class="card" id="cpobs"></div><div class="card" id="cpcard"></div></div>`; el.dataset.built = "1"; }
    obsCard(); cardCard();
  }
  /* the time following the clock: the azimuth again every 5 s (not while a field is typed in) */
  setInterval(() => { if (St && typeof APP !== "undefined" && St().tab === "compass" && !O.jd) { const a = document.activeElement; if (!(a && /INPUT|SELECT|TEXTAREA/.test(a.tagName) && a.closest("#t-compass"))) obsCard(); } }, 5000);
  return { show, fit, observe, state: () => ({ O, CARD }) };
})();
