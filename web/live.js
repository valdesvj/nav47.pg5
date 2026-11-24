/* live.js - NAV47 on the web: the route monitored with the GPS, on the Route tab, as the calculator's NAV, BOW and live
 * CHART. The GPS tab's state every second (a GPS, a log, the demo); DR when the fix is lost; the passage by the
 * calculator's own C (src/passage.c: the active leg, XTE, BRG DTG CTS, the next leg at the arrival circle, ETA to the
 * next waypoint and the final one); your objects' CPA; the alarms (off track, waypoint, arrived, zone ahead, in a zone,
 * no fix, MOB). On the chart: the Mermaid Asiana and her wake, her track, the XTE limits, the next waypoint ringed. The
 * Bow view: over the bow from the helideck, the route a lane to the horizon, the platforms as jackets and decks. */
"use strict";
const LIVE = (() => {
  const $ = id => document.getElementById(id), D2R = Math.PI / 180;
  const cfg = Object.assign({ xte: 0.25, arr: 0.2, auto: true, follow: true, drKn: 0, alarms: true }, store.get("live", {}));
  const L = { pos: null, psg: null, trail: [], trailT: 0, alarm: 0, ack: 0, event: 0, eventT: 0, mob: null, near: [],
              routeKey: "", started: false, last: null, dr: null, cur: { e: 0, n: 0, ok: false }, frame: 0, sun: null, sunT: 0 };
  const A = { XTE: 1, ARR: 2, FINAL: 4, ZONE: 8, CPA: 16, LOST: 32, MOB: 64 };

  /* your vessel's pictures (the calculator's BOW.BMP and SHIP.BMP, or a PNG / JPG): kept in this browser. As on the
     calculator, light pixels joined to the picture's edge and pure magenta are see-through */
  const PICS = { bow: null, ship: null };
  function prep(img) {
    const c = document.createElement("canvas"); c.width = img.naturalWidth; c.height = img.naturalHeight;
    const g = c.getContext("2d"); g.drawImage(img, 0, 0);
    const d = g.getImageData(0, 0, c.width, c.height), p = d.data, W = c.width, H = c.height;
    const light = i => p[4 * i + 3] > 0 && (p[4 * i] * 30 + p[4 * i + 1] * 59 + p[4 * i + 2] * 11) / 100 >= 128;
    const seen = new Uint8Array(W * H), st = [];
    for (let x = 0; x < W; x++) st.push(x, (H - 1) * W + x);
    for (let y = 0; y < H; y++) st.push(y * W, y * W + W - 1);
    while (st.length) {
      const i = st.pop(); if (seen[i] || !light(i)) continue;
      seen[i] = 1; p[4 * i + 3] = 0;
      const x = i % W, y = (i / W) | 0;
      if (x > 0) st.push(i - 1); if (x < W - 1) st.push(i + 1); if (y > 0) st.push(i - W); if (y < H - 1) st.push(i + W);
    }
    for (let i = 0; i < W * H; i++) if (p[4 * i] === 255 && p[4 * i + 1] === 0 && p[4 * i + 2] === 255) p[4 * i + 3] = 0;
    g.putImageData(d, 0, 0);
    return c;
  }
  function loadPic(kind, url) {
    if (!url) { PICS[kind] = null; return; }
    const im = new Image();
    im.onload = () => { PICS[kind] = prep(im); bow(); draw(); };
    im.src = url;
  }
  const saved = store.get("livepics", {});
  loadPic("bow", saved.bow); loadPic("ship", saved.ship);
  if (/vesselpics/.test(location.hash)) { loadPic("bow", "../docs/vessel/BOW.BMP"); loadPic("ship", "../docs/vessel/SHIP.BMP"); }   /* tests: the templates */
  function picsUI() {
    const el = $("bowpics"); if (!el || el.dataset.on) return;
    el.dataset.on = 1;
    el.innerHTML = `<label class="meta">Bow picture <input type="file" id="picbow" accept=".bmp,.BMP,.png,.jpg,.jpeg"></label>
      <label class="meta">Ship icon (from above, bow up) <input type="file" id="picship" accept=".bmp,.BMP,.png,.jpg,.jpeg"></label>
      <button class="small" id="picdef">NAV47's own</button>
      <span class="meta">as the calculator's /NAV47/BOW.BMP and SHIP.BMP (docs/vessel: the templates)</span>`;
    const take = kind => e => { const f = e.target.files[0]; if (!f) return; const r = new FileReader();
      r.onload = () => { const v = store.get("livepics", {}); v[kind] = r.result; store.set("livepics", v); loadPic(kind, r.result); };
      r.readAsDataURL(f); };
    $("picbow").onchange = take("bow"); $("picship").onchange = take("ship");
    $("picdef").onclick = () => { store.set("livepics", {}); PICS.bow = PICS.ship = null; bow(); draw(); };
  }
  const save = () => store.set("live", cfg);
  const nm = (a, b, c, d) => { const q = NAV.inv(a, b, c, d); return { d: q.s / NM, brg: (q.a1 + 360) % 360 }; };
  const deg = v => isFinite(v) ? String(Math.round(((v % 360) + 360) % 360)).padStart(3, "0") + "°" : "–";
  const n1 = v => isFinite(v) ? v.toFixed(1) : "–";

  /* ---------------------------------------------------------------- a beep (the alarms) */
  let actx = null;
  function beep() {
    if (!cfg.alarms) return;
    try {
      actx = actx || new (window.AudioContext || window.webkitAudioContext)();
      const o = actx.createOscillator(), g = actx.createGain();
      o.frequency.value = 2400; g.gain.value = 0.08; o.connect(g); g.connect(actx.destination);
      o.start(); o.stop(actx.currentTime + 0.15);
    } catch (e) { /* no sound */ }
  }

  /* ---------------------------------------------------------------- the plan monitored: the Route tab's */
  function syncPlan() {
    const R = S.route, pts = ROUTE.pts;
    const key = JSON.stringify([pts.map(p => [p.lat.toFixed(6), p.lon.toFixed(6), p.sail]), R.sail, R.lim, R.split, R.val]);
    if (key === L.routeKey) return pts.length >= 2;
    L.routeKey = key;
    if (pts.length < 2) return false;
    const p = Module._api_in() >> 3, IN = Module.HEAPF64.subarray(p, p + 4096);
    pts.slice(0, 2000).forEach((q, i) => { IN[2 * i] = q.lat; IN[2 * i + 1] = q.lon; });
    NAV.setLegs(pts);                                       /* each leg's own sailing */
    Module._api_psg_plan(Math.min(pts.length, 2000), R.sail, R.lim, [0, 1, 2, 3][R.split], R.val);
    L.started = false;
    return true;
  }
  /* the user's waypoints (ROUTE.pts with user) by the plan's points (WPT_USER: every point given) */
  function wpOf(userNo) {                                  /* userNo: 1-based index in ROUTE.pts */
    const pts = ROUTE.pts;
    let j = userNo - 1;
    while (j < pts.length - 1 && !pts[j].user) j++;
    return { j, name: pts[j] ? (pts[j].name || `WP${pts.filter((p, k) => p.user && k <= j).length}`) : "" };
  }
  function ptCum(k) {                                      /* the plan's distance at ROUTE.pts[k] */
    const r = ROUTE.plan.rows;
    let n = -1;
    for (const w of r) if (w.flags & 1) { n++; if (n === k) return w.cum; }
    return r.length ? r[r.length - 1].cum : 0;
  }

  /* ---------------------------------------------------------------- every second */
  function tick(g, sats) {
    if (!g) return;
    const now = performance.now(), dt = L.last ? (now - L.last) / 1000 : 0;
    L.last = now;
    const fresh = g.fixAge >= 0 && g.fixAge < 10;
    const hdg = g.hdgAge >= 0 && g.hdgAge < 5 ? g.hdg : NaN, stw = g.stwAge >= 0 && g.stwAge < 5 ? g.stw : NaN;
    let pos;
    if (fresh) {
      pos = { lat: g.lat, lon: g.lon, sog: g.sog, cog: g.cog, hdg, stw, src: Module.UTF8ToString(Module._api_qual_name(g.q)) };
      if (L.dr) { const e = nm(L.dr.lat, L.dr.lon, g.lat, g.lon); pos.drErr = e; }
      L.dr = null;
    } else if (L.pos && g.fixAge >= 0) {                   /* dead reckoning from the last position */
      const p = L.dr || { lat: L.pos.lat, lon: L.pos.lon };
      const crs = isFinite(hdg) ? hdg : L.pos.cog, kn = cfg.drKn > 0 ? cfg.drKn : isFinite(stw) ? stw : L.pos.sog;
      if (isFinite(crs) && isFinite(kn) && dt > 0) { const q = NAV.dir(p.lat, p.lon, crs, kn * NM * dt / 3600); p.lat = q.lat; p.lon = q.lon; }
      L.dr = p;
      pos = { lat: p.lat, lon: p.lon, sog: kn, cog: crs, hdg, stw, src: "DR", since: g.fixAge };
    } else return;
    /* the current: ground less water */
    if (fresh && isFinite(hdg) && isFinite(g.cog)) {
      const w = isFinite(stw) ? stw : cfg.drKn;
      if (w > 0) {
        const e = g.sog * Math.sin(g.cog * D2R) - w * Math.sin(hdg * D2R), n = g.sog * Math.cos(g.cog * D2R) - w * Math.cos(hdg * D2R);
        if (!L.cur.ok) L.cur = { e, n, ok: true }; else { const k = 1 - Math.exp(-(dt > 0 ? dt : 1) / 30); L.cur.e += k * (e - L.cur.e); L.cur.n += k * (n - L.cur.n);   /* 30 s, as the calculator */ }
        pos.set = (Math.atan2(L.cur.e, L.cur.n) / D2R + 360) % 360; pos.drift = Math.hypot(L.cur.e, L.cur.n);
      }
    }
    if (!L.pos && typeof VOY !== "undefined") { VOY.on = false; VOY.playing = false; }   /* the GPS live: the voyage player steps aside */
    L.pos = pos;
    L.posT = now;
    if (!L.trail.length || now - L.trailT > 10000) { L.trail.push([pos.lat, pos.lon]); L.trailT = now; if (L.trail.length > 3000) L.trail.shift(); }
    /* the passage */
    L.psg = null;
    if (syncPlan()) {
      Module._api_psg_cfg(cfg.xte, cfg.arr, cfg.auto ? 1 : 0);
      if (!L.started) { Module._api_psg_start(pos.lat, pos.lon); L.started = true; }
      Module._api_psg_update(pos.lat, pos.lon, pos.sog || 0, S.route.speed, 0);
      const o = NAV.o();
      L.psg = { active: o[0], leg: o[1], done: o[2], xte: o[3], atd: o[4], legNm: o[5], brg: o[6], dtg: o[7], cts: o[8], next: o[9],
                toUser: o[10], userNo: o[11], dtgUser: o[12], dtgFinal: o[13], kn: o[14], ttgUser: o[15], ttgFinal: o[16], alarm: o[17],
                event: o[18], n: o[19] };
    }
    /* the nearest objects, their CPA */
    const ve = isFinite(pos.cog) && pos.sog > 0.3 ? pos.sog * Math.sin(pos.cog * D2R) : 0, vn = isFinite(pos.cog) && pos.sog > 0.3 ? pos.sog * Math.cos(pos.cog * D2R) : 0;
    const own = new Set(S.route.wps.map(w => w.obj).filter(Boolean));   /* the route's own platforms: worked at, no CPA alarm */
    L.near = S.objects.map(o => {
      const e = (((o.lon - pos.lon + 540) % 360) - 180) * Math.cos(pos.lat * D2R) * 60, n = (o.lat - pos.lat) * 60, d = Math.hypot(e, n);
      const v2 = ve * ve + vn * vn, t = v2 ? (e * ve + n * vn) / v2 : -1, cpa = t > 0 ? Math.hypot(e - ve * t, n - vn * t) : d;
      return { o, d, brg: (Math.atan2(e, n) / D2R + 360) % 360, cpa, tcpa: t, in: o.r > 0 && d * NM < o.r,
               warn: o.r > 0 && !own.has(o.id) && (d * NM < o.r || (t > 0 && t < 0.5 && cpa * NM < o.r)) };
    }).sort((a, b) => a.d - b.d).slice(0, 4);
    /* the alarms */
    let a = 0;
    if (L.psg && L.psg.active && !L.psg.done && (L.psg.alarm & 1)) a |= A.XTE;
    if (L.psg && L.psg.event) { L.event = L.psg.event === 4 ? A.FINAL : A.ARR; L.eventT = now; L.ack &= ~L.event; }
    if (L.event && now - L.eventT > 30000) L.event = 0;
    a |= L.event;
    if (L.near.some(z => z.in && !own.has(z.o.id))) a |= A.ZONE; else if (L.near.some(z => z.warn)) a |= A.CPA;
    if (pos.src === "DR") a |= A.LOST;
    if (L.mob) a |= A.MOB;
    L.ack &= a;
    if (a & ~L.ack & ~L.alarm) beep();
    L.alarm = a;
    /* follow */
    if (cfg.follow && (S.tab === "route" || S.tab === "live") && CH.W) {
      const [x, y, v] = proj(pos.lat, pos.lon);
      if (!v || x < CH.W * 0.25 || x > CH.W * 0.75 || y < CH.H * 0.25 || y > CH.H * 0.75) { S.view.lat0 = pos.lat; S.view.lon0 = pos.lon; clampView(); }
    }
    if (S.tab === "route" || S.tab === "live") { card(); draw(); }
    if (S.tab === "live") { wpList(); gpsBox(); }
    if (now - L.sunT > 30000) sunNow();
  }

  function alarmText() {
    const a = L.alarm & ~L.ack, p = L.psg, pos = L.pos;
    if (!a) return "";
    if (a & A.MOB) { const m = nm(pos.lat, pos.lon, L.mob.lat, L.mob.lon); return `MOB ${m.d.toFixed(2)} NM ${deg(m.brg)}`; }
    if (a & A.ZONE) return `IN THE ZONE: ${L.near.find(z => z.in && z.warn).o.name}`;
    if (a & A.CPA) { const z = L.near.find(z => z.warn); return `ZONE AHEAD: ${z.o.name} in ${Math.round(z.tcpa * 60)} min`; }
    if (a & A.XTE) return `OFF TRACK ${Math.abs(p.xte).toFixed(2)} NM ${p.xte >= 0 ? "R" : "L"}`;
    if (a & A.FINAL) return "ARRIVED: THE FINAL WAYPOINT";
    if (a & A.ARR) return `WAYPOINT: NOW TO ${wpOf(p.userNo).name}`;
    if (a & A.LOST) return "NO FIX: DEAD RECKONING";
    return "";
  }

  function sunNow() {                                      /* the Sun and the Moon's Hc and Zn, for the Bow view */
    if (!L.pos) return;
    L.sunT = performance.now();
    Module._api_alm(Date.now() / 864e5 + 2440587.5, L.pos.lat, L.pos.lon, 0);
    const o = NAV.o();
    L.sun = { hc: o[2], zn: o[3], mhc: o[6], mzn: o[7] };
  }

  /* ---------------------------------------------------------------- the chart: called by draw() */
  function drawChart(g) {
    const pos = L.pos;
    if (!pos) return;
    const rows = ROUTE.plan.rows, p = L.psg;
    g.save();
    /* the XTE limits either side of the active leg, the next point ringed */
    if (p && p.active && !p.done && rows[p.leg]) {
      const a = rows[p.leg - 1], b = rows[p.leg], q = NAV.inv(a.lat, a.lon, b.lat, b.lon);
      g.strokeStyle = css("--now"); g.globalAlpha = 0.55; g.lineWidth = 1.2; g.setLineDash([5, 5]);
      for (const sd of [-1, 1]) {
        const line = [];
        for (let k = 0; k <= 24; k++) {
          const c = NAV.dir(a.lat, a.lon, q.a1, q.s * k / 24), e = NAV.dir(c.lat, c.lon, c.az + sd * 90, cfg.xte * NM);
          line.push([e.lat, e.lon]);
        }
        polyline(line);
      }
      g.setLineDash([]); g.globalAlpha = 1;
      const [x, y, v] = proj(b.lat, b.lon);
      if (v) { const r = Math.max(10, cfg.arr * NM * pxPerM(b.lat)); g.strokeStyle = css("--now"); g.lineWidth = 2; g.beginPath(); g.arc(x, y, r, 0, 2 * Math.PI); g.stroke(); }
    }
    /* the track */
    g.fillStyle = css("--now");
    for (const [la, lo] of L.trail) { const [x, y, v] = proj(la, lo); if (v) g.fillRect(x - 1.5, y - 1.5, 3, 3); }
    /* MOB */
    if (L.mob) {
      const [x, y, v] = proj(L.mob.lat, L.mob.lon), [vx, vy] = proj(pos.lat, pos.lon);
      if (v) {
        g.strokeStyle = "#d63a2a"; g.lineWidth = 2.5; g.beginPath(); g.arc(x, y, 9, 0, 2 * Math.PI); g.moveTo(x - 6, y - 6); g.lineTo(x + 6, y + 6); g.moveTo(x + 6, y - 6); g.lineTo(x - 6, y + 6); g.stroke();
        g.setLineDash([6, 4]); g.beginPath(); g.moveTo(vx, vy); g.lineTo(x, y); g.stroke(); g.setLineDash([]);
        g.fillStyle = "#d63a2a"; g.font = "bold 13px system-ui"; g.fillText("MOB", x + 12, y - 8);
      }
    }
    /* her wake, then her (drawVessel: true scale when it shows, her course vector at her speed) */
    const h = isFinite(pos.hdg) ? pos.hdg : isFinite(pos.cog) ? pos.cog : 0;
    const [x, y, v] = proj(pos.lat, pos.lon);
    if (v) {
      const [ax, ay] = proj(pos.lat + 0.001 * Math.cos(h * D2R), pos.lon + 0.001 * Math.sin(h * D2R) / Math.max(0.01, Math.cos(pos.lat * D2R)));
      let fx = ax - x, fy = ay - y; const l = Math.hypot(fx, fy) || 1; fx /= l; fy /= l;
      const Lm = S.route.len || 95, sc = Math.max(pxPerM(pos.lat), 60 / Lm), Lp = Lm * sc, B = Lp * 22 / 95;
      if (pos.sog > 0.5) {
        const sx = x - fx * Lp / 2, sy = y - fy * Lp / 2;
        g.strokeStyle = "rgba(255,255,255,0.9)"; g.lineWidth = 2;
        for (const sd of [-1, 1]) for (let j = 0; j < 7; j++) {
          const s = ((j * 14 + L.frame * 5) % 98) + 4, r = sd * (B / 2 + s * 0.354);
          const px = sx - fx * s - fy * r, py = sy - fy * s + fx * r;
          g.beginPath(); g.moveTo(px, py); g.lineTo(px - fx * 5 - fy * sd * 2, py - fy * 5 + fx * sd * 2); g.stroke();
        }
        g.strokeStyle = "rgba(255,255,255,0.7)"; g.setLineDash([2, 5]); g.lineWidth = 3;
        g.beginPath(); g.moveTo(sx, sy); g.lineTo(sx - fx * 50, sy - fy * 50); g.stroke(); g.setLineDash([]);
      }
      if (PICS.ship) {                                     /* your icon: turned to her heading, her true length (60 px at least) */
        const im = PICS.ship, k = Lp / im.height;
        g.save(); g.translate(x, y); g.rotate(Math.atan2(fx, -fy)); g.imageSmoothingEnabled = im.height < Lp / 2 ? false : true;
        g.drawImage(im, -im.width * k / 2, -Lp / 2, im.width * k, Lp); g.restore();
        g.strokeStyle = css("--now"); g.lineWidth = 2; g.beginPath(); g.moveTo(x + fx * Lp / 2, y + fy * Lp / 2);
        const run = Math.max(40, (pos.sog || 0) * NM / 10 * pxPerM(pos.lat)); g.lineTo(x + fx * (Lp / 2 + run), y + fy * (Lp / 2 + run)); g.stroke();
      } else {
        const sp = S.route.speed; S.route.speed = pos.sog > 0.3 ? pos.sog : 0.1;
        drawVessel(pos.lat, pos.lon, h, false, false);
        S.route.speed = sp;
      }
    }
    /* the alarm */
    const t = alarmText();
    if (t) {
      g.font = "bold 15px system-ui"; const w = g.measureText(t).width + 28;
      g.fillStyle = "#c62828"; g.beginPath(); g.roundRect(CH.W / 2 - w / 2, 48, w, 30, 15); g.fill();
      g.fillStyle = "#fff"; g.textAlign = "center"; g.fillText(t, CH.W / 2, 68); g.textAlign = "left";
    }
    g.restore();
  }

  /* ---------------------------------------------------------------- the Live card */
  function card() {
    const el = $("livecard"); if (!el) return;
    const pos = L.pos, p = L.psg, off = zd(), lab = off ? "ZT" : "UT";
    const eta = h => isFinite(h) && h < 999 ? FMT.dt(nowJd() + h / 24, off) + " " + lab : "–";
    if (!pos) {
      el.innerHTML = `<h2>Live <span class="meta">the route with the GPS</span></h2>
        <div class="meta">No GPS yet: the <b>GPS</b> tab (a GPS, a log, or the <b>Demo</b>, which sails this route).</div>
        <div class="controls" style="margin-top:6px"><button id="livedemo">Demo along this route</button>
        <select id="livex"><option>1</option><option>5</option><option selected>10</option><option>30</option><option>60</option></select>×</div>`;
      $("livedemo").onclick = () => { const s = $("gpsx"); location.hash = location.hash.replace(/&?gpsx=\d+/, "") + "&gpsx=" + $("livex").value; if (s) s.value = $("livex").value; GPS.demo(); };
      return;
    }
    const chip = `<span class="chip ${pos.src === "DR" ? "warn" : /DGPS|RTK/.test(pos.src) ? "ok" : ""}">${pos.src}${pos.src === "DR" ? " " + Math.round(pos.since) + " s" : ""}</span>`;
    const at = alarmText();
    let body = "";
    if (p && p.active) {
      const to = wpOf(p.userNo), next = p.done ? null : to;
      const user = ROUTE.pts[p.userNo - 1] && ROUTE.pts[p.userNo - 1].user;
      const dWp = next ? p.dtg + (ptCum(next.j) - ptCum(p.userNo - 1)) / NM : 0;
      const tWp = p.kn > 0.1 ? dWp / p.kn : NaN;
      const f = Math.max(-1, Math.min(1, p.xte / (2 * cfg.xte)));
      body = `
        <div style="display:flex;justify-content:space-between;align-items:baseline;gap:8px;flex-wrap:wrap">
          <div class="big" style="font-size:22px;font-weight:700">${p.done ? "ARRIVED" : "TO " + (user ? to.name : `pt ${p.userNo} → ${to.name}`)}</div>
          <span class="meta">leg ${p.leg} / ${p.n - 1}</span></div>
        <table>
          <tr><td>BRG · DTG</td><td class="num mono"><b>${deg(p.brg)}</b> · ${p.dtg.toFixed(2)} NM</td><td>CTS</td><td class="num mono">${deg(p.cts)}</td></tr>
          <tr><td>${next ? next.name : "–"}</td><td class="num mono">${dWp.toFixed(1)} NM · ${FMT.dur(tWp || 0)}</td><td>ETA</td><td class="num mono"><b>${eta(tWp)}</b></td></tr>
          <tr><td>Final</td><td class="num mono">${p.dtgFinal.toFixed(1)} NM · ${FMT.dur(p.ttgFinal || 0)}</td><td>ETA</td><td class="num mono"><b>${eta(p.ttgFinal)}</b></td></tr>
          <tr><td>Next leg</td><td class="num mono">${isFinite(p.next) ? deg(p.next) : "the last"}</td><td>ETA at</td><td class="num mono">${p.kn.toFixed(1)} kn</td></tr>
        </table>
        <div style="margin:8px 0 2px;display:flex;justify-content:space-between"><b class="mono" style="${p.alarm & 1 ? "color:var(--now)" : ""}">XTE ${Math.abs(p.xte).toFixed(3)} NM ${p.xte >= 0 ? "R" : "L"}</b>
          <span>${Math.abs(p.xte) > 0.01 ? (p.xte > 0 ? "◀ steer to port" : "steer to starboard ▶") : "on track"}</span></div>
        <div style="position:relative;height:22px;border:1px solid var(--line);border-radius:6px;background:var(--chip)">
          <div style="position:absolute;left:25%;top:0;bottom:0;border-left:2px solid var(--now)"></div>
          <div style="position:absolute;left:75%;top:0;bottom:0;border-left:2px solid var(--now)"></div>
          <div style="position:absolute;left:50%;top:0;bottom:0;border-left:1px solid var(--soft)"></div>
          <div style="position:absolute;left:calc(${50 + 46 * f}% - 7px);top:3px;width:14px;height:14px;background:var(--track);clip-path:polygon(50% 0,100% 100%,0 100%)"></div></div>
        <div class="meta">the limits ±${cfg.xte} NM</div>`;
    } else body = `<div class="meta">${ROUTE.pts.length < 2 ? "Plan a route: two waypoints or more." : "Start: the leg nearest the vessel."}</div>`;
    const near = L.near.length ? `<h3>The nearest objects</h3><table>${L.near.slice(0, 3).map(z => `<tr style="${z.warn ? "color:var(--now);font-weight:700" : ""}">
      <td>${z.o.name}</td><td class="num mono">${deg(z.brg)}</td><td class="num mono">${z.d.toFixed(2)} NM</td>
      <td class="num mono">${z.tcpa > 0 && z.tcpa < 24 ? `CPA ${z.cpa.toFixed(2)} in ${Math.round(z.tcpa * 60)}′` : "opening"}</td></tr>`).join("")}</table>` : "";
    el.innerHTML = `<h2>Live ${chip} <span class="meta mono">${FMT.pos(pos.lat, pos.lon)}</span></h2>
      ${at ? `<div style="background:#c62828;color:#fff;font-weight:700;border-radius:8px;padding:6px 10px;margin:6px 0;display:flex;justify-content:space-between">${at}<button id="liveack" class="small">OK</button></div>` : ""}
      ${body}
      <table style="margin-top:6px"><tr><td>SOG · COG</td><td class="num mono"><b>${n1(pos.sog)}</b> kn · ${deg(pos.cog)}</td><td>HDG</td><td class="num mono">${deg(pos.hdg)}</td></tr>
        <tr><td>Set · drift</td><td class="num mono">${isFinite(pos.set) ? deg(pos.set) + " · " + pos.drift.toFixed(1) + " kn" : "–"}</td><td>STW</td><td class="num mono">${isFinite(pos.stw) ? n1(pos.stw) + " kn" : "–"}</td></tr>
        ${pos.drErr ? `<tr><td colspan="4" class="meta">the DR was ${pos.drErr.d.toFixed(2)} NM ${deg(pos.drErr.brg)} off</td></tr>` : ""}</table>
      ${near}
      <div class="controls" style="margin-top:8px"><button id="livestart" title="the leg nearest the vessel">Start</button>
        <button id="liveprev">◀ Prev</button><button id="livenext">Next ▶</button>
        <label><input type="checkbox" id="livefollow" ${cfg.follow ? "checked" : ""}> follow</label>
        <button id="livemob" style="background:#c62828;color:#fff;font-weight:700">MOB</button>${L.mob ? '<button id="livemobx" class="small">clear MOB</button>' : ""}</div>
      <div class="controls" style="margin-top:6px"><label>XTE limit <input class="num" id="livexte" type="number" step="0.05" min="0.02" max="5" value="${cfg.xte}"> NM</label>
        <label>arrival <input class="num" id="livearr" type="number" step="0.05" min="0.02" max="5" value="${cfg.arr}"> NM</label>
        <label><input type="checkbox" id="liveauto" ${cfg.auto ? "checked" : ""}> next leg by itself</label>
        <label>DR speed <input class="num" id="livedr" type="number" step="0.5" min="0" max="40" value="${cfg.drKn}"> kn</label>
        <label><input type="checkbox" id="livebeep" ${cfg.alarms ? "checked" : ""}> beeps</label></div>`;
    const on = (id, f) => { const e = $(id); if (e) e.onclick = f; };
    on("liveack", () => { L.ack |= L.alarm; L.event = 0; card(); draw(); });
    on("livestart", () => { if (L.pos && syncPlan()) { Module._api_psg_start(L.pos.lat, L.pos.lon); L.started = true; } });
    on("liveprev", () => step(-1));
    on("livenext", () => step(1));
    on("livemob", () => { L.mob = { lat: L.pos.lat, lon: L.pos.lon, t: Date.now() }; L.ack &= ~A.MOB; beep(); card(); draw(); });
    on("livemobx", () => { if (confirm("Clear the MOB?")) { L.mob = null; card(); draw(); } });
    $("livefollow").onchange = e => { cfg.follow = e.target.checked; save(); };
    $("livexte").onchange = e => { cfg.xte = Math.max(0.02, +e.target.value || 0.25); save(); };
    $("livearr").onchange = e => { cfg.arr = Math.max(0.02, +e.target.value || 0.2); save(); };
    $("liveauto").onchange = e => { cfg.auto = e.target.checked; save(); };
    $("livedr").onchange = e => { cfg.drKn = Math.max(0, +e.target.value || 0); save(); };
    $("livebeep").onchange = e => { cfg.alarms = e.target.checked; save(); };
  }
  function step(d) {
    if (!L.pos || !syncPlan()) return;
    Module._api_psg_step(d);
    Module._api_psg_update(L.pos.lat, L.pos.lon, L.pos.sog || 0, S.route.speed, 1);
    L.started = true;
  }

  /* the bow view's bottom strip, as the calculator's top bar: the time (UT and the DR's zone), the fix's quality */
  function strip(g, pos) {
    const d = new Date(), z = Math.round(pos.lon / 15), p2 = n => String(n).padStart(2, "0");
    const hms = t => `${p2(t.getUTCHours())}:${p2(t.getUTCMinutes())}:${p2(t.getUTCSeconds())}`;
    const t = `${hms(d)} UT · ${hms(new Date(d.getTime() + z * 36e5))} ZT (${z >= 0 ? "+" : ""}${z})`;
    const st = typeof GPS !== "undefined" && GPS.state ? GPS.state() : null;
    let q, col;
    if (pos.src === "DR") { q = `DR · no fix for ${Math.round(pos.since || 0)} s`; col = "#f0b43c"; }
    else {
      q = pos.src + (st ? ` ${st.dim >= 2 ? st.dim + "D" : ""} · ${st.nused} sats · HDOP ${isFinite(st.hdop) ? st.hdop.toFixed(1) : "–"} · ${Math.max(0, Math.round(st.fixAge))} s` : "");
      col = /RTK|DGPS|GPS|SBAS|PPS/.test(pos.src) ? "#5fd39a" : "#ff7b6b";
    }
    g.fillStyle = "rgba(10,20,30,0.82)"; g.fillRect(0, BV.H - 30, BV.W, 30);
    g.font = "bold 16px ui-monospace, monospace"; g.fillStyle = "#fff"; g.textAlign = "left"; g.fillText(t, 12, BV.H - 10);
    g.fillStyle = col; g.textAlign = "right"; g.fillText(q, BV.W - 12, BV.H - 10); g.textAlign = "left";
    g.beginPath(); g.arc(BV.W - 22 - g.measureText(q).width, BV.H - 15, 6, 0, 2 * Math.PI); g.fill();   /* the quality's dot */
  }

  /* Live tab: the plan's points from the vessel (BRG, DTG along the plan, ETA at the speed made good); a click: next */
  function wpList() {
    const el = $("livewps"); if (!el) return;
    const pts = ROUTE.pts, pos = L.pos, p = L.psg, off = zd(), lab = off ? "ZT" : "UT";
    if (pts.length < 2) { el.innerHTML = `<h2>Plan waypoints</h2><div class="meta">No route: plan one on the Route tab.</div>`; return; }
    const cur = p && p.active ? (p.done ? pts.length : p.leg) : -1;
    let n = 0;
    const rows = pts.map((q, k) => {
      if (q.user) n++;
      const name = q.name || (q.user ? `WP${n}` : `pt ${k + 1}`);
      let brg = "–", dtg = "–", eta = "–";
      if (pos) {
        const b = nm(pos.lat, pos.lon, q.lat, q.lon); brg = deg(b.brg);
        const along = cur >= 0 && k >= cur ? p.dtg + (ptCum(k) - ptCum(cur)) / NM : k < cur ? NaN : b.d;
        if (isFinite(along)) { dtg = along.toFixed(along < 10 ? 2 : 1); const kn = p && p.kn > 0.1 ? p.kn : pos.sog; if (kn > 0.1) eta = FMT.dt(nowJd() + along / kn / 24, off); }
      }
      const cls = k === cur ? "sel" : k < cur ? "dim" : "";
      return `<tr class="${cls} click" data-lk="${k}"><td>${k + 1}</td><td>${q.user ? "<b>" + esc(name) + "</b>" : esc(name)}</td><td class="num mono">${brg}</td><td class="num mono">${dtg}</td><td class="num mono">${eta}</td></tr>`;
    }).join("");
    const top = el.querySelector(".scroll") ? el.querySelector(".scroll").scrollTop : null;
    el.innerHTML = `<h2>Plan waypoints <span class="meta">${pts.length} · from the vessel; click one: steer for it</span></h2>
      <div class="scroll" style="max-height:330px"><table><tr><th>#</th><th>Name</th><th class="num">BRG</th><th class="num">DTG NM</th><th class="num">ETA ${lab}</th></tr>${rows}</table></div>`;
    const sc = el.querySelector(".scroll");
    if (top !== null) sc.scrollTop = top; else { const r = el.querySelector("tr.sel"); if (r) sc.scrollTop = r.offsetTop - 60; }
    el.querySelectorAll("[data-lk]").forEach(r => r.onclick = () => {
      if (!L.pos || !L.psg || !L.psg.active) return;
      const d = +r.dataset.lk - L.psg.leg;
      for (let i = 0; i < Math.abs(d); i++) step(Math.sign(d));
      tick(GPS.state(), GPS.sats());
    });
  }

  /* Live tab: the GPS in short (the GPS tab has the rest) */
  function gpsBox() {
    const el = $("livegps"); if (!el) return;
    const st = typeof GPS !== "undefined" && GPS.state ? GPS.state() : null, pos = L.pos;
    if (!el.dataset.built) {
      el.innerHTML = `<h2>GPS <span class="meta" id="lgsrc"></span></h2><div style="display:flex;gap:12px;align-items:flex-start">
        <canvas id="lgsky" width="200" height="200" style="width:150px;height:150px"></canvas><div id="lgtxt" class="mono" style="font-size:13px"></div></div>
        <div class="controls" style="margin-top:6px"><button id="lggps">The GPS tab: connect, replay, demo</button></div>`;
      el.dataset.built = "1";
      $("lggps").onclick = () => showTab("gps");
    }
    $("lgsrc").textContent = pos ? pos.src : "no position";
    const n1 = v => isFinite(v) ? v.toFixed(1) : "–";
    $("lgtxt").innerHTML = st && pos ? `${FMT.dm(pos.lat, 2, 3)}${pos.lat < 0 ? "S" : "N"}<br>${(() => { const lo = ((pos.lon + 540) % 360) - 180; return FMT.dm(lo, 3, 3) + (lo < 0 ? "W" : "E"); })()}<br>
      ${st.dim >= 2 ? st.dim + "D · " : ""}${st.nused} sats<br>HDOP ${n1(st.hdop)} · PDOP ${n1(st.pdop)}<br>fix ${Math.max(0, Math.round(st.fixAge))} s ago${st.age > 0 ? `<br>DGPS age ${Math.round(st.age)} s` : ""}`
      : "No GPS: connect one, replay a log, or the Demo.";
    if (GPS.sky) GPS.sky($("lgsky"));
  }

  /* ---------------------------------------------------------------- the Bow view */
  const BV = { W: 900, H: 420, HOR: 120, FOC: 470, K: 48, CAM: 20 };
  function bproj(la, lo, h, hdg, height) {               /* metres up (height): drawn as seen from 20 m */
    const pos = L.pos, e = (((lo - pos.lon + 540) % 360) - 180) * Math.cos(pos.lat * D2R) * 60, n = (la - pos.lat) * 60;
    const fw = e * Math.sin(hdg * D2R) + n * Math.cos(hdg * D2R), rt = e * Math.cos(hdg * D2R) - n * Math.sin(hdg * D2R);
    if (fw < 0.012) return null;
    return [BV.W / 2 + BV.FOC * rt / fw, BV.HOR + BV.K / fw * (1 - (height || 0) / BV.CAM), fw];
  }
  function planAt(rows, s) {
    let i = 1; while (i < rows.length - 1 && rows[i].cum < s) i++;
    const a = rows[i - 1], b = rows[i], ds = Math.max(0, Math.min(s - a.cum, b.cum - a.cum));
    if (b.kind === "G") { const q = NAV.inv(a.lat, a.lon, b.lat, b.lon), p = NAV.dir(a.lat, a.lon, q.a1, ds); return { lat: p.lat, lon: p.lon, crs: p.az }; }
    const p = NAV.rdir(a.lat, a.lon, a.rl, ds) || b; return { lat: p.lat, lon: p.lon, crs: a.rl };
  }
  function bow() {
    const cv = $("bowcv"); if (!cv || !L.pos || cv.offsetParent === null) return;
    const dpr = window.devicePixelRatio || 1, cw = cv.clientWidth;
    if (cv.width !== Math.round(cw * dpr)) { cv.width = Math.round(cw * dpr); cv.height = Math.round(cw * BV.H / BV.W * dpr); }
    const g = cv.getContext("2d"), k = cv.width / BV.W;
    g.setTransform(k, 0, 0, k, 0, 0);
    const pos = L.pos, p = L.psg, rows = ROUTE.plan.rows, hdg = isFinite(pos.hdg) ? pos.hdg : isFinite(pos.cog) ? pos.cog : 0;
    /* the sky, the sea */
    let gr = g.createLinearGradient(0, 0, 0, BV.HOR); gr.addColorStop(0, "#7fa7d0"); gr.addColorStop(1, "#d9e6f0");
    g.fillStyle = gr; g.fillRect(0, 0, BV.W, BV.HOR);
    gr = g.createLinearGradient(0, BV.HOR, 0, BV.H); gr.addColorStop(0, "#5f7f94"); gr.addColorStop(1, "#1f3b52");
    g.fillStyle = gr; g.fillRect(0, BV.HOR, BV.W, BV.H - BV.HOR);
    /* the Sun, the Moon at their bearings */
    if (L.sun) for (const [hc, zn, moon] of [[L.sun.hc, L.sun.zn, 0], [L.sun.mhc, L.sun.mzn, 1]]) {
      const rel = ((zn - hdg + 540) % 360) - 180;
      if (hc < -1 || hc > 40 || Math.abs(rel) > 60) continue;
      const x = BV.W / 2 + BV.FOC * Math.tan(rel * D2R) * 0.9, y = BV.HOR - 10 - hc * 2.2;
      g.fillStyle = moon ? "#f4f1e6" : "#ffd75a"; g.beginPath(); g.arc(x, y, moon ? 10 : 14, 0, 2 * Math.PI); g.fill();
    }
    /* the sea moving: dashes coming in at her speed */
    L.frame++;
    const sp = (pos.sog || 0) * 0.012 + 0.004;
    g.strokeStyle = "rgba(255,255,255,0.35)"; g.lineWidth = 1.2;
    for (let r = 0; r < 22; r++) {
      const z = ((r / 22 + L.frame * sp) % 1), y = BV.HOR + 4 + z * z * (BV.H - BV.HOR);
      const step = 40 + z * 120, off = (r * 53) % step, w = 4 + z * 22;
      g.beginPath(); for (let x = -off; x < BV.W; x += step) { g.moveTo(x, y); g.lineTo(x + w, y); } g.stroke();
    }
    /* the lane: the plan ahead +- the XTE limit, 8 nm */
    if (p && p.active && !p.done && rows.length > 1) {
      /* the waypoints are points of the lane, its corners mitred (the edges offset along the bisector); near a corner
         the inner edge would fold back, so its mitre point stands for the points there; lines are cut at the bow */
      const s0 = rows[p.leg - 1].cum + p.atd * NM, end = rows[rows.length - 1].cum, far = Math.min(end, s0 + 8 * NM);
      const lim = cfg.xte * NM, NEAR = 0.012, sd = [];
      const corners = [];
      for (let j = p.leg; j < rows.length - 1; j++) {
        if (rows[j].cum <= s0 - 3 * lim || rows[j].cum > far) continue;
        const cin = planAt(rows, rows[j].cum - 1).crs, cout = planAt(rows, rows[j].cum + 1).crs;
        corners.push({ s: rows[j].cum, half: ((((cout - cin + 540) % 360) - 180) / 2), cin });
      }
      for (let i = 0; i < 90; i++) {
        const s = Math.min(far, s0 + 0.05 * NM * Math.pow(1.075, i));
        for (const c of corners) if (c.s > s0 && c.s <= s && (!sd.length || c.s > sd[sd.length - 1] + 1) && !sd.includes(c.s)) sd.push(c.s);
        if (!sd.length || s > sd[sd.length - 1] + 1) sd.push(s);
        if (s >= far) break;
      }
      sd.sort((x, y) => x - y);
      const loc = (la, lo) => { const e = (((lo - pos.lon + 540) % 360) - 180) * Math.cos(pos.lat * D2R) * 60, n = (la - pos.lat) * 60;
        return [e * Math.sin(hdg * D2R) + n * Math.cos(hdg * D2R), e * Math.cos(hdg * D2R) - n * Math.sin(hdg * D2R)]; };
      const xy = q => [BV.W / 2 + BV.FOC * q[1] / q[0], BV.HOR + BV.K / q[0]];
      const edge = [[], []], mid = [];
      for (const s of sd) {
        let c = planAt(rows, s), crs = c.crs, l = lim;
        const use = [true, true];
        for (const k of corners) {
          const dc = Math.abs(k.s - s);
          if (dc < 0.5) { crs = k.cin + k.half; l = lim / Math.max(0.35, Math.cos(k.half * D2R)); }
          else if (dc < lim * Math.abs(Math.tan(k.half * D2R)) * 1.05 + 1) use[k.half > 0 ? 1 : 0] = false;
        }
        for (const side of [0, 1]) if (use[side]) { const e = NAV.dir(c.lat, c.lon, crs + (side ? 90 : -90), l); edge[side].push(loc(e.lat, e.lon)); }
        mid.push(loc(c.lat, c.lon));
      }
      const path = (L2, close) => {                     /* a polyline in the vessel's frame, cut at the bow, into the path */
        let on = false;
        for (let i = 0; i + 1 < L2.length; i++) {
          let a = L2[i], b = L2[i + 1];
          if (a[0] < NEAR && b[0] < NEAR) { on = false; continue; }
          if (a[0] < NEAR) { const t = (NEAR - a[0]) / (b[0] - a[0]); a = [NEAR, a[1] + t * (b[1] - a[1])]; on = false; }
          if (b[0] < NEAR) { const t = (NEAR - b[0]) / (a[0] - b[0]); b = [NEAR, b[1] + t * (a[1] - b[1])]; }
          const A = xy(a), B = xy(b);
          if (!on) g.moveTo(A[0], A[1]);
          g.lineTo(B[0], B[1]); on = true;
          if (L2[i + 1][0] < NEAR) on = false;
        }
      };
      g.fillStyle = "rgba(255,255,255,0.10)"; g.beginPath();
      const A2 = edge[0].filter(q => q[0] >= NEAR).map(xy), B2 = edge[1].filter(q => q[0] >= NEAR).map(xy).reverse();
      if (A2.length > 1 && B2.length > 1) { A2.concat(B2).forEach((q, i) => i ? g.lineTo(q[0], q[1]) : g.moveTo(q[0], q[1])); g.closePath(); g.fill(); }
      g.strokeStyle = "#ffcf33"; g.lineWidth = 2.2;
      for (const E of edge) { g.beginPath(); path(E); g.stroke(); }
      g.setLineDash([3, 6]); g.strokeStyle = "rgba(255,255,255,0.7)"; g.lineWidth = 1.5; g.beginPath(); path(mid); g.stroke(); g.setLineDash([]);
      /* the waypoints' flags */
      ROUTE.pts.forEach((w, j) => {
        if (!w.user) return;
        const c = ptCum(j); if (c < s0 || c - s0 > 9 * NM) return;
        const q = bproj(w.lat, w.lon, 0, hdg); if (!q || q[0] < -20 || q[0] > BV.W + 20) return;
        const h = 18 + (q[1] - BV.HOR) * 0.5;
        g.strokeStyle = "#fff"; g.lineWidth = 2; g.beginPath(); g.moveTo(q[0], q[1]); g.lineTo(q[0], q[1] - h); g.stroke();
        g.fillStyle = "#e8453c"; g.beginPath(); g.moveTo(q[0], q[1] - h); g.lineTo(q[0] + 18, q[1] - h + 6); g.lineTo(q[0], q[1] - h + 12); g.fill();
        g.fillStyle = "#fff"; g.font = "bold 13px system-ui"; g.fillText(wpOf(j + 1).name, q[0] + 22, q[1] - h + 8);
      });
    }
    /* the objects: the farthest first */
    const objs = S.objects.map(o => ({ o, d: nm(pos.lat, pos.lon, o.lat, o.lon).d })).filter(z => z.d < 8 && z.o.type !== "port").sort((a, b) => b.d - a.d);
    for (const { o, d } of objs) {
      const e0 = (((o.lon - pos.lon + 540) % 360) - 180) * Math.cos(pos.lat * D2R) * 111320, n0 = (o.lat - pos.lat) * 110574;
      const rel = ((Math.atan2(e0, n0) / D2R - hdg + 540) % 360) - 180;
      if (Math.abs(rel) > 55) continue;
      const big = Math.max(1, Math.min(3, d / 0.5));
      let a = 50, b = 50, rot = 0, legs = 22, top = 42, mast = 0;
      if (o.type === "rig") { a = 70; b = 60; legs = 25; top = 40; mast = 95; }
      else if (o.type === "wellhead") { a = b = 14; legs = 14; top = 20; }
      else if (o.type === "spm") { a = b = 12; legs = 0; top = 5; }
      else if (o.type === "buoy") { a = b = 3; legs = 0; top = 4; mast = 7; }
      else if (o.type === "light") { a = b = 5; legs = 0; top = 14; mast = 18; }
      else if (o.type === "wreck") { a = b = 30; legs = 0; top = 0; }
      if (o.orient !== null && o.orient !== undefined && o.a > 0) { a = o.a; b = o.b || o.a; rot = o.orient; }
      a *= big; b *= big;
      const toLL = (e, n) => [pos.lat + n / 110574, pos.lon + e / (111320 * Math.cos(pos.lat * D2R))];
      const corner = j => { const fw = (j === 0 || j === 3) ? b / 2 : -b / 2, sd = j < 2 ? -a / 2 : a / 2;
        return [e0 + fw * Math.sin(rot * D2R) + sd * Math.cos(rot * D2R), n0 + fw * Math.cos(rot * D2R) - sd * Math.sin(rot * D2R)]; };
      const P = (e, n, h) => { const [la, lo] = toLL(e, n); return bproj(la, lo, 0, hdg, h); };
      /* the zone: dots on the sea */
      if (o.r > 0) { g.fillStyle = "rgba(255,120,100,0.8)"; for (let j = 0; j < 90; j++) { const q = P(e0 + o.r * Math.sin(j * 4 * D2R), n0 + o.r * Math.cos(j * 4 * D2R), 0); if (q) g.fillRect(q[0] - 1, q[1] - 1, 2, 2); } }
      const c0 = P(e0, n0, 0); if (!c0) continue;
      let ytop = c0[1];
      g.strokeStyle = "#20262c"; g.lineWidth = 2;
      if (legs > 0) for (let j = 0; j < 4; j++) {        /* the jacket's legs */
        const [ce, cn] = corner(j), q0 = P(e0 + (ce - e0) * 0.8, n0 + (cn - n0) * 0.8, 0), q1 = P(e0 + (ce - e0) * 0.8, n0 + (cn - n0) * 0.8, legs);
        if (q0 && q1) { g.beginPath(); g.moveTo(q0[0], q0[1]); g.lineTo(q1[0], q1[1]); g.stroke(); }
      }
      if (top > legs) {                                    /* the deck module: the faces she sees, lit from the side */
        const cs = [0, 1, 2, 3].map(corner);
        for (let j = 0; j < 4; j++) {
          const i2 = (j + 1) % 4, me = (cs[j][0] + cs[i2][0]) / 2, mn = (cs[j][1] + cs[i2][1]) / 2;
          if ((me - e0) * -me + (mn - n0) * -mn <= 0) continue;
          const q = [P(cs[j][0], cs[j][1], legs), P(cs[i2][0], cs[i2][1], legs), P(cs[i2][0], cs[i2][1], top), P(cs[j][0], cs[j][1], top)];
          if (q.some(z => !z)) continue;
          g.fillStyle = j % 2 ? "#c9ced3" : "#e7eaec"; g.beginPath(); q.forEach((z, i) => i ? g.lineTo(z[0], z[1]) : g.moveTo(z[0], z[1])); g.closePath(); g.fill();
          g.lineWidth = 1.2; g.stroke();
          ytop = Math.min(ytop, q[2][1], q[3][1]);
        }
        if (o.type === "platform") {                        /* the helideck's edge and a crane */
          const q = P(e0, n0, top), q2 = P(cs[0][0], cs[0][1], top + 12);
          if (q && q2) { g.strokeStyle = "#f0a020"; g.lineWidth = 2; g.beginPath(); g.moveTo(q[0], q[1]); g.lineTo(q2[0], q2[1]); g.stroke(); ytop = Math.min(ytop, q2[1]); }
        }
      } else if (top > 0) {
        const q = P(e0, n0, top); if (q) { g.fillStyle = o.type === "spm" ? "#f0a020" : "#e8453c"; g.beginPath(); g.arc(q[0], (q[1] + c0[1]) / 2, Math.max(3, c0[1] - q[1]), 0, 2 * Math.PI); g.fill(); }
      }
      if (mast > 0) { const q = P(e0, n0, mast), q0 = P(e0, n0, top); if (q && q0) { g.strokeStyle = "#20262c"; g.lineWidth = 2; g.beginPath(); g.moveTo(q0[0], q0[1]); g.lineTo(q[0], q[1]); g.stroke(); ytop = Math.min(ytop, q[1]); } }
      g.font = "12px system-ui"; const lab = `${o.name} ${d.toFixed(1)}`, w = g.measureText(lab).width;
      g.fillStyle = "rgba(0,0,0,0.55)"; g.fillRect(c0[0] - w / 2 - 4, ytop - 22, w + 8, 17);
      g.fillStyle = "#fff"; g.fillText(lab, c0[0] - w / 2, ytop - 9);
    }
    /* the bow from the helideck: your picture (the calculator's 400 px wide: here 900), else NAV47's */
    if (PICS.bow) {
      const im = PICS.bow, k = BV.W / 400;
      g.imageSmoothingEnabled = true;
      g.drawImage(im, BV.W / 2 - im.width * k / 2, BV.H - im.height * k, im.width * k, im.height * k);
    } else {
    const S2 = 2.25, tip = [450, 262];
    const P2 = (x, y) => [tip[0] + (x - 200) * S2, tip[1] + (y - 122) * S2];
    g.fillStyle = "#9aa3ab"; g.strokeStyle = "#20262c"; g.lineWidth = 2;
    g.beginPath(); [[112, 200], [172, 144], [200, 122], [228, 144], [288, 200]].forEach(([x, y], i) => { const [X, Y] = P2(x, y); i ? g.lineTo(X, Y) : g.moveTo(X, Y); }); g.lineTo(...P2(288, 210)); g.lineTo(...P2(112, 210)); g.closePath(); g.fill(); g.stroke();
    g.fillStyle = "#d63a2a"; g.beginPath(); [[112, 200], [172, 144], [200, 122], [228, 144], [288, 200], [280, 200], [226, 148], [200, 128], [174, 148], [120, 200]].forEach(([x, y], i) => { const [X, Y] = P2(x, y); i ? g.lineTo(X, Y) : g.moveTo(X, Y); }); g.closePath(); g.fill();
    g.strokeStyle = "rgba(0,0,0,0.25)"; g.lineWidth = 1;
    for (let j = 0; j < 4; j++) { const y = 152 + j * 12, w = (y - 122) * 0.95; const [x1, y1] = P2(200 - w, y), [x2] = P2(200 + w, y); g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y1); g.stroke(); }
    { const [X, Y] = P2(200, 134); g.fillStyle = "#20262c"; g.beginPath(); g.arc(X, Y, 8, 0, 2 * Math.PI); g.fill(); }
    { const [X, Y] = P2(188, 176); g.strokeStyle = "#20262c"; g.lineWidth = 2; g.strokeRect(X, Y, 25 * S2, 11 * S2); }
    }
    /* the numbers */
    const box = (x, y, lines) => {
      g.fillStyle = "rgba(10,20,30,0.7)"; g.fillRect(x, y, 178, 22 * lines.length + 10);
      g.fillStyle = "#fff"; g.font = "bold 17px ui-monospace, monospace"; lines.forEach((t, i) => g.fillText(t, x + 10, y + 24 + 22 * i));
    };
    box(12, BV.H - 150, p && p.active && !p.done ? [`XTE ${Math.abs(p.xte).toFixed(2)} ${p.xte >= 0 ? "R" : "L"}`, `CTS ${deg(p.cts)}`, `DTG ${p.dtg.toFixed(2)}`].concat([FMT.dm(pos.lat, 2, 3) + (pos.lat < 0 ? "S" : "N")]) : ["no route", "", "", FMT.dm(pos.lat, 2, 3) + (pos.lat < 0 ? "S" : "N")]);
    box(BV.W - 190, BV.H - 150, [`SOG ${n1(pos.sog)}`, `${isFinite(pos.hdg) ? "HDG" : "COG"} ${deg(hdg)}`, `COG ${deg(pos.cog)}`, (() => { const lo = ((pos.lon + 540) % 360) - 180; return FMT.dm(lo, 3, 3) + (lo < 0 ? "W" : "E"); })()]);
    strip(g, pos);
    if (p && p.active && !p.done && isFinite(p.next) && p.dtg < 3 * cfg.arr + 0.5) {
      g.fillStyle = "#ffcf33"; g.fillRect(12, 12, 230, 32); g.fillStyle = "#20262c"; g.font = "bold 18px system-ui"; g.fillText(`TURN AHEAD ${deg(p.next)}`, 22, 34);
    }
    const t = alarmText();
    if (t) { g.font = "bold 16px system-ui"; const w = g.measureText(t).width + 30; g.fillStyle = "#c62828"; g.beginPath(); g.roundRect(BV.W / 2 - w / 2, 12, w, 32, 16); g.fill(); g.fillStyle = "#fff"; g.textAlign = "center"; g.fillText(t, BV.W / 2, 34); g.textAlign = "left"; }
  }

  /* the animation: the bow view 4 times a second, the chart's wake twice */
  let tk = 0, shown = "";
  setInterval(() => {
    if (!window.NAV47_UP || (S.tab !== "route" && S.tab !== "live")) return;
    const bc = $("bowcard");
    if (bc) { bc.hidden = !L.pos; picsUI(); }                            /* the Bow view: with a position */
    if (!L.pos) { if (shown !== "none" && $("livecard")) { card(); shown = "none"; } return; }
    shown = "live";
    bow();
    if (++tk % 2 === 0) { L.frame++; draw(); }
  }, 250);

  /* the GPS live: a position in the last 15 s (the voyage player and its box step aside) */
  const active = () => !!L.pos && performance.now() - (L.posT || 0) < 15000;
  return { tick, drawChart, card, bow, wpList, gpsBox, state: () => L, cfg, active };
})();
