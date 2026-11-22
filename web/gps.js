/* gps.js - NAV47 on the web: the GPS tab. A GPS (or the ship's NMEA through a USB serial adapter) read by Chrome's Web
 * Serial, a log replayed, or a made-up demo round the DR; parsed by the calculator's own C (src/nmea.c, web/api.c):
 * the fix and its quality, the satellites in the sky and their signals, SOG COG HDG, set and drift. The page can be
 * the bridge to the DM42n (its USB serial console, channel 2, paced as tools/nmea_bridge.py) and feeds the Calculator
 * tab's program, whose FIX SATS NAV BOW pages then run live. The DR can follow the GPS. Nothing leaves this computer. */
"use strict";
const GPS = (() => {
  const $ = id => document.getElementById(id), S = () => APP.state, F = () => APP.fmt;
  const D2R = Math.PI / 180, SYS = ["GPS", "GLO", "GAL", "BDS", "QZS", "SBAS", "NAVIC"];
  const KEEP = /^(RMC|GGA|GNS|GSA|VTG|ZDA|GST|HDT|THS|HDG|VHW|GSV)$/;
  let built = false, src = null, stopSrc = null, t0 = performance.now(), last = [], st = null, sats = [];
  let calcPort = null, calcWriter = null, queue = new Map(), gsvLast = {}, gsvOpen = {}, toCalcTab = true, follow = false, followT = 0;
  let cur = { e: 0, n: 0, ok: false };
  const ms = () => performance.now() - t0;

  /* ---------------------------------------------------------------- a sentence in */
  function line(s) {
    s = s.trim();
    if (!s || (s[0] !== "$" && s[0] !== "!")) return;
    Module.ccall("api_gps_feed", "number", ["string", "number"], [s, ms()]);
    last.push(s); if (last.length > 14) last.shift();
    toCalc(s);
    if (toCalcTab && window.Module && Module.ready) Module.ccall("api_nmea_calc", null, ["string"], [s]);
  }

  /* to the DM42n: the latest of each kind, a GSV group a talker every 5 s, 200 bytes every 0.2 s (its buffer: 256) */
  function toCalc(s) {
    if (!calcWriter) return;
    const kind = s.slice(3, 6), talker = s.slice(1, 3);
    if (!KEEP.test(kind)) return;
    let key = s.slice(1, 6);
    if (kind === "GSV") {
      const part = +s.split(",")[2];
      if (part === 1) { gsvOpen[talker] = ms() - (gsvLast[talker] ?? -1e9) >= 5000; if (gsvOpen[talker]) gsvLast[talker] = ms(); }
      if (!gsvOpen[talker]) return;
      key += part;
    }
    if (kind === "GSA") { const f = s.split("*")[0].split(","); key += f.length > 18 ? f[18] : f.slice(3, 6).join(","); }
    queue.delete(key); queue.set(key, s);
  }
  setInterval(async () => {
    if (!calcWriter || !queue.size) return;
    let n = 0, out = "";
    for (const [k, s] of queue) {
      if (n && n + s.length + 2 > 200) break;
      out += "\x12" + s + "\n"; n += s.length + 2; queue.delete(k);
    }
    try { await calcWriter.write(new TextEncoder().encode(out)); } catch (e) { status("the calculator: " + e.message); calcWriter = null; }
  }, 200);

  /* ---------------------------------------------------------------- the sources */
  function status(t) { const e = $("gpsstat"); if (e) e.textContent = t; }
  function stop() { if (stopSrc) { try { stopSrc(); } catch (e) { /* closed */ } } stopSrc = null; src = null; status("stopped"); }

  async function serialGps() {
    if (!("serial" in navigator)) { status("Web Serial: Chrome or Edge on a computer"); return; }
    stop();
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: +$("gpsbaud").value });
    const dec = new TextDecoderStream(), closed = port.readable.pipeTo(dec.writable).catch(() => {});
    const reader = dec.readable.getReader();
    let buf = "", on = true;
    src = "serial"; status("reading the GPS at " + $("gpsbaud").value + " baud");
    stopSrc = () => { on = false; reader.cancel(); closed.then(() => port.close()); };
    while (on) {
      const { value, done } = await reader.read().catch(() => ({ done: true }));
      if (done) break;
      buf += value;
      const ls = buf.split("\n"); buf = ls.pop();
      ls.forEach(line);
    }
  }

  function replay(file) {
    stop();
    file.text().then(t => {
      const secs = t.replace(/\r/g, "").split(/\n\s*\n/);   /* a blank line a second (make_log.py) */
      const blocks = secs.length > 1 ? secs : (() => {        /* else by the RMC times: a block each */
        const out = []; let b = [];
        t.replace(/\r/g, "").split("\n").forEach(l => { if (/^\$..RMC/.test(l) && b.length) { out.push(b.join("\n")); b = []; } b.push(l); });
        if (b.length) out.push(b.join("\n")); return out;
      })();
      let i = 0;
      const id = setInterval(() => {
        if (i >= blocks.length) { clearInterval(id); status("the log is over"); return; }
        blocks[i++].split("\n").forEach(line);
        status(`replaying ${file.name}: ${i} / ${blocks.length} s`);
      }, 1000 / +$("gpsspeed").value);
      src = "file"; stopSrc = () => clearInterval(id);
    });
  }

  /* a made-up track round the DR: 10 kn, a course of 045 then 090, a current of 1.2 kn to 135, a gyro and a log,
     a DGPS fix, made-up satellites */
  function demo() {
    stop();
    const ck = b => { let x = 0; for (const c of b) x ^= c.charCodeAt(0); return "$" + b + "*" + x.toString(16).toUpperCase().padStart(2, "0"); };
    const dm = (v, lat) => { const a = Math.abs(v), d = Math.floor(a), m = (a - d) * 60;
      return [String(d).padStart(lat ? 2 : 3, "0") + m.toFixed(4).padStart(7, "0"), lat ? (v < 0 ? "S" : "N") : (v < 0 ? "W" : "E")]; };
    const SAT = [["GP", 2, 67, 40, 46], ["GP", 5, 22, 310, 38], ["GP", 7, 48, 120, 44], ["GP", 13, 80, 280, 47], ["GP", 15, 34, 60, 41],
                 ["GP", 20, 55, 225, 43], ["GP", 29, 27, 345, 39], ["GP", 18, 8, 165, 26], ["GL", 65, 30, 20, 37], ["GL", 66, 72, 100, 44],
                 ["GL", 81, 58, 300, 42], ["GA", 3, 60, 75, 45], ["GA", 8, 25, 150, 39], ["GA", 26, 70, 10, 46], ["GA", 31, 9, 270, 25],
                 ["GB", 6, 52, 130, 42], ["GB", 23, 64, 290, 44], ["GB", 28, 14, 80, 30]];
    let [lat, lon] = S().pos, t = 0, run = 0;
    const rows = () => (typeof ROUTE !== "undefined" && ROUTE.plan && ROUTE.plan.rows) || [];
    const along = rows().length >= 2;                    /* a route: she sails it (a little off, as a vessel does) */
    if (along) { lat = rows()[0].lat; lon = rows()[0].lon; }
    const x = () => +(($("gpsx") || {}).value || (location.hash.match(/gpsx=(\d+)/) || [0, 1])[1]);   /* faster: 1 to 60 */
    const id = setInterval(() => {
      let hdg = Math.floor(t / 300) % 2 ? 90 : 45, stw = 10;
      if (along) {                                        /* the course to a point 0.3 nm ahead on the plan, off by a swing */
        const r = rows(), tot = r[r.length - 1].cum;
        const ahead = (s) => { let i = 1; while (i < r.length - 1 && r[i].cum < s) i++; const a = r[i - 1], b = r[i], ds = Math.max(0, Math.min(s - a.cum, b.cum - a.cum));
          const q = APP.nav.inv(a.lat, a.lon, b.lat, b.lon), p = APP.nav.dir(a.lat, a.lon, q.a1, ds); return p; };
        const tgt = ahead(Math.min(tot, run + 0.3 * 1852)), q = APP.nav.inv(lat, lon, tgt.lat, tgt.lon);
        hdg = (q.a1 + 360 + 6 * Math.sin(t / 40) - 4) % 360;   /* the swing, and the current's set allowed for badly */
        if (run >= tot) stw = 0;
      }
      const ve = stw * Math.sin(hdg * D2R) + 1.2 * Math.sin(135 * D2R), vn = stw * Math.cos(hdg * D2R) + 1.2 * Math.cos(135 * D2R);
      const sog = Math.hypot(ve, vn), cog = (Math.atan2(ve, vn) / D2R + 360) % 360;
      const d = new Date(), hh = d.toISOString().slice(11, 19).replace(/:/g, "") + ".00", dd = d.toISOString().slice(2, 10).split("-").reverse().join("");
      const [la, lh] = dm(lat, true), [lo, oh] = dm(lon, false);
      line(ck(`GPRMC,${hh},A,${la},${lh},${lo},${oh},${sog.toFixed(1)},${cog.toFixed(1)},${dd},2.1,E,D`));
      line(ck(`GPGGA,${hh},${la},${lh},${lo},${oh},2,${SAT.length - 3},0.8,12.4,M,-28.1,M,4.0,0412`));
      line(ck(`HEHDT,${hdg.toFixed(1)},T`));
      line(ck(`VWVHW,${hdg.toFixed(1)},T,,M,${stw.toFixed(1)},N,${(stw * 1.852).toFixed(1)},K`));
      if (t % 5 === 0) {
        line(ck(`GPGST,${hh},1.2,1.8,1.1,35.0,0.6,0.5,2.5`));
        [["GP", 1], ["GL", 2], ["GA", 3], ["GB", 4]].forEach(([tk, id]) => {
          const u = SAT.filter(s => s[0] === tk && s[2] > 10).map(s => String(s[1]).padStart(2, "0"));
          line(ck(`GNGSA,A,3,${[...u, ...Array(12).fill("")].slice(0, 12).join(",")},1.6,0.8,1.4,${id}`));
          const ss = SAT.filter(s => s[0] === tk), n = Math.ceil(ss.length / 4);
          for (let i = 0; i < n; i++)
            line(ck(`${tk}GSV,${n},${i + 1},${String(ss.length).padStart(2, "0")}` + ss.slice(i * 4, i * 4 + 4).map(s =>
              `,${String(s[1]).padStart(2, "0")},${String(s[2]).padStart(2, "0")},${String(Math.round(s[3] + t / 60) % 360).padStart(3, "0")},${s[4] + Math.round(2 * Math.sin(t / 7 + s[1]))}`).join("") + ",1"));
        });
      }
      const k = x(), p = APP.nav.dir(lat, lon, cog, sog * 1852 / 3600 * k); lat = p.lat; lon = p.lon; t += k;
      run += stw * 1852 / 3600 * k;
      status(along ? `demo: along the route, ${k}× (${(run / 1852).toFixed(1)} nm run)` : `demo: a made-up track from the DR, ${t} s`);
    }, 1000);
    src = "demo"; stopSrc = () => clearInterval(id);
  }

  async function connectCalc() {
    if (!("serial" in navigator)) { $("calcstat").textContent = "Web Serial: Chrome or Edge on a computer"; return; }
    try {
      calcPort = await navigator.serial.requestPort();
      await calcPort.open({ baudRate: 115200 });
      calcWriter = calcPort.writable.getWriter();
      $("calcstat").textContent = "the DM42n: sending (NAV47: GPS SET, NMEA INPUT ON)";
    } catch (e) { $("calcstat").textContent = e.message; }
  }

  /* ---------------------------------------------------------------- the picture */
  function read() {
    if (!window.Module || !Module.ready) return null;
    const n = Module.ccall("api_gps_state", "number", ["number"], [ms()]), o = APP.nav.o();
    const s = Array.from(o.subarray(0, 26));
    sats = [];
    for (let i = 0; i < n; i++) { const k = 40 + 6 * i; sats.push({ sys: o[k], prn: o[k + 1], el: o[k + 2], az: o[k + 3], snr: o[k + 4], used: o[k + 5] }); }
    return { fix: s[0], lat: s[1], lon: s[2], q: s[3], dim: s[5], nused: s[6], pdop: s[7], hdop: s[8], vdop: s[9], age: s[10], stn: s[11],
             sl: s[12], so: s[13], sog: s[14], cog: s[15], hdg: s[16], stw: s[17], jd: s[18], ok: s[19], bad: s[20], fixAge: s[21],
             hdgAge: s[22], stwAge: s[23], alt: s[24] };
  }
  const n1 = v => isFinite(v) ? v.toFixed(1) : "–", deg = v => isFinite(v) ? String(Math.round(((v % 360) + 360) % 360)).padStart(3, "0") + "°" : "–";

  function sky(cv) {
    const g = cv.getContext("2d"), W = cv.width, cx = W / 2, cy = W / 2, R = W / 2 - 18;
    const css = getComputedStyle(document.body), ink = css.getPropertyValue("--ink") || "#222", soft = css.getPropertyValue("--soft") || "#777";
    g.clearRect(0, 0, W, W);
    g.strokeStyle = soft; g.fillStyle = soft; g.lineWidth = 1; g.font = "12px system-ui"; g.textAlign = "center"; g.textBaseline = "middle";
    [0, 30, 60].forEach(e => { g.beginPath(); g.arc(cx, cy, R * (90 - e) / 90, 0, 2 * Math.PI); g.setLineDash(e ? [2, 4] : []); g.stroke(); });
    g.setLineDash([]);
    ["N", "E", "S", "W"].forEach((t, i) => g.fillText(t, cx + (R + 10) * Math.sin(i * Math.PI / 2), cy - (R + 10) * Math.cos(i * Math.PI / 2)));
    sats.forEach(s => {
      if (s.el < 0 || s.az < 0) return;
      const r = R * (90 - s.el) / 90, x = cx + r * Math.sin(s.az * D2R), y = cy - r * Math.cos(s.az * D2R);
      g.strokeStyle = ink; g.fillStyle = ink; g.beginPath();
      if (s.sys === 0) g.arc(x, y, 6, 0, 2 * Math.PI);
      else if (s.sys === 1) g.rect(x - 5, y - 5, 10, 10);
      else if (s.sys === 2) { g.moveTo(x, y - 7); g.lineTo(x + 7, y); g.lineTo(x, y + 7); g.lineTo(x - 7, y); g.closePath(); }
      else if (s.sys === 3) { g.moveTo(x, y - 7); g.lineTo(x + 7, y + 5); g.lineTo(x - 7, y + 5); g.closePath(); }
      else { g.moveTo(x - 5, y - 5); g.lineTo(x + 5, y + 5); g.moveTo(x - 5, y + 5); g.lineTo(x + 5, y - 5); }
      s.used && s.sys <= 3 ? g.fill() : g.stroke();
      g.fillStyle = soft; g.textAlign = "left"; g.fillText(s.prn, x + 9, y - 2); g.textAlign = "center";
    });
  }
  function bars(cv) {
    const g = cv.getContext("2d"), W = cv.width, H = cv.height, css = getComputedStyle(document.body);
    const ink = css.getPropertyValue("--ink") || "#222", soft = css.getPropertyValue("--soft") || "#777";
    g.clearRect(0, 0, W, H);
    const list = sats.filter(s => s.snr >= 0).sort((a, b) => a.sys - b.sys || a.prn - b.prn), bw = Math.min(22, (W - 30) / Math.max(1, list.length));
    g.strokeStyle = soft; g.fillStyle = soft; g.font = "11px system-ui";
    [10, 20, 30, 40, 50].forEach(d => { const y = H - 20 - d * (H - 30) / 50; g.setLineDash([2, 3]); g.beginPath(); g.moveTo(26, y); g.lineTo(W, y); g.stroke(); g.fillText(d, 2, y + 4); });
    g.setLineDash([]);
    list.forEach((s, i) => {
      const x = 28 + i * bw, h = s.snr * (H - 30) / 50;
      g.fillStyle = ink; g.strokeStyle = ink;
      s.used ? g.fillRect(x, H - 20 - h, bw - 3, h) : g.strokeRect(x + 0.5, H - 20 - h, bw - 4, h);
      if (i === 0 || list[i - 1].sys !== s.sys) { g.fillStyle = soft; g.fillText(SYS[s.sys] || "?", x, H - 5); }
    });
  }

  function show() {
    if (!built) build();
    const g = read();
    if (!g) return;
    st = g;
    if (!$("gpsfix")) return;
    const live = g.fixAge >= 0 && g.fixAge < 10;
    const q = Module.UTF8ToString(Module.ccall("api_qual_name", "number", ["number"], [live ? g.q : 0]));
    const counts = SYS.map((n, k) => { const a = sats.filter(s => s.sys === k); return a.length ? `<span class="chip">${n} ${a.filter(s => s.used).length}/${a.length}</span>` : ""; }).join("");
    /* the current: ground less water (gyro, log), filtered */
    let cur2 = "";
    if (live && isFinite(g.hdg) && g.hdgAge < 5 && isFinite(g.stw) && g.stwAge < 5 && isFinite(g.cog)) {
      const e = g.sog * Math.sin(g.cog * D2R) - g.stw * Math.sin(g.hdg * D2R), n = g.sog * Math.cos(g.cog * D2R) - g.stw * Math.cos(g.hdg * D2R);
      if (!cur.ok) cur = { e, n, ok: true }; else { cur.e += 0.1 * (e - cur.e); cur.n += 0.1 * (n - cur.n); }
      cur2 = `${deg(Math.atan2(cur.e, cur.n) / D2R)} ${Math.hypot(cur.e, cur.n).toFixed(1)} kn`;
    }
    $("gpsfix").innerHTML = `
      <div class="chips"><span class="chip ${live ? (g.q >= 2 ? "ok" : "") : "warn"}">${live ? q : "NO FIX"}</span>
        ${live && g.dim ? `<span class="chip">${g.dim === 3 ? "3D" : "2D"}</span>` : ""}<span class="chip">${g.nused} used</span>${counts}</div>
      <div class="big mono" style="font-size:30px;margin:8px 0">${g.fixAge >= 0 ? F().pos(g.lat, g.lon) : "–"}</div>
      <table>
        <tr><td>SOG / COG</td><td class="num mono">${n1(g.sog)} kn · ${deg(g.cog)}</td><td>HDG (gyro)</td><td class="num mono">${g.hdgAge >= 0 && g.hdgAge < 5 ? deg(g.hdg) : "–"}</td></tr>
        <tr><td>STW (log)</td><td class="num mono">${g.stwAge >= 0 && g.stwAge < 5 ? n1(g.stw) + " kn" : "–"}</td><td>Set and drift</td><td class="num mono">${cur2 || "–"}</td></tr>
        <tr><td>PDOP HDOP VDOP</td><td class="num mono">${n1(g.pdop)} ${n1(g.hdop)} ${n1(g.vdop)}</td><td>Error (GST, 1σ)</td><td class="num mono">${isFinite(g.sl) ? Math.hypot(g.sl, g.so).toFixed(1) + " m" : "–"}</td></tr>
        <tr><td>Differential</td><td class="num mono">${g.stn >= 0 ? "station " + String(g.stn).padStart(4, "0") + " · " + n1(g.age) + " s" : "none"}</td><td>GPS time</td><td class="num mono">${g.jd ? F().dt(g.jd, 0) + " UT" : "–"}</td></tr>
        <tr><td>Last fix</td><td class="num mono">${g.fixAge >= 0 ? g.fixAge.toFixed(0) + " s ago" : "never"}</td><td>Sentences</td><td class="num mono">${g.ok} · bad ${g.bad}</td></tr>
      </table>`;
    sky($("gpssky")); bars($("gpsbars"));
    $("gpsraw").textContent = last.join("\n");
    followStep(g);
  }
  function followStep(g) {
    const live = g && g.fixAge >= 0 && g.fixAge < 10;
    if (follow && live && performance.now() - followT > 10000) {           /* the DR follows: every 10 s */
      followT = performance.now();
      const dr = $("dr"); dr.value = F().pos(g.lat, g.lon); dr.dispatchEvent(new Event("change"));
    }
  }

  function build() {
    built = true;
    $("t-gps").innerHTML = `<div class="two">
      <div>
        <div class="card"><h2>GPS</h2>
          <div style="display:flex;gap:6px;flex-wrap:wrap;align-items:center">
            <button id="gpsser">Connect a GPS (USB / serial)</button>
            <label>Baud <select id="gpsbaud"><option>4800</option><option>9600</option><option>38400</option><option>115200</option></select></label>
            <button id="gpsdemo" title="a made-up vessel: along the route if there is one">Demo</button>
            <select id="gpsx" title="the demo, faster"><option>1</option><option>5</option><option>10</option><option>30</option><option>60</option></select>×
            <button id="gpsstop">Stop</button></div>
          <div style="margin-top:6px"><label>Replay a log <input type="file" id="gpslog" accept=".nmea,.txt,.log"></label>
            <label>speed <select id="gpsspeed"><option>1</option><option>2</option><option>5</option><option>10</option></select></label></div>
          <div class="meta" id="gpsstat">no source</div>
          <div id="gpsfix"></div>
          <label style="display:block;margin-top:8px"><input type="checkbox" id="gpsfollow"> The DR follows the GPS (every 10 s: the
            almanac, tides, zones and the route's DR)</label>
        </div>
        <div class="card"><h2>To the calculator</h2>
          <label><input type="checkbox" id="gpscalctab" checked> To the Calculator tab's program (its FIX SATS NAV BOW pages;
            its NMEA input is on here)</label>
          <div style="margin-top:6px"><button id="gpsopen">Open on the calculator</button> <span class="meta">its FIX page (then SATS NAV BOW;
            its NMEA input is on here)</span></div>
          <div style="margin-top:6px"><button id="gpscalc">Connect the DM42n (USB)</button> <span class="meta" id="calcstat">this page as the bridge:
            the sentences NAV47 uses, paced for its buffer (as tools/nmea_bridge.py)</span></div>
        </div>
        <div class="card"><h3>The last sentences</h3><pre id="gpsraw" class="mono" style="font-size:11px;white-space:pre-wrap;margin:0"></pre></div>
      </div>
      <div>
        <div class="card"><h2>The satellites</h2><canvas id="gpssky" width="340" height="340" style="max-width:100%"></canvas>
          <div class="meta">● GPS ■ GLONASS ◆ Galileo ▲ BeiDou ✕ others · filled: used in the fix</div>
          <h3>Signals (dB-Hz)</h3><canvas id="gpsbars" width="460" height="170" style="max-width:100%"></canvas></div>
        <div class="meta">Web Serial needs Chrome or Edge on a computer. The ship's network (UDP / TCP): tools/nmea_bridge.py on a PC or a Pi.
          Not for navigation.</div>
      </div></div>`;
    $("gpsser").onclick = () => serialGps().catch(e => status(e.message));
    $("gpsdemo").onclick = demo;
    $("gpsstop").onclick = stop;
    $("gpslog").onchange = e => { const f = e.target.files[0]; if (f) replay(f); };
    $("gpsfollow").onchange = e => { follow = e.target.checked; followT = 0; };
    $("gpscalctab").onchange = e => { toCalcTab = e.target.checked; };
    $("gpscalc").onclick = connectCalc;
    $("gpsopen").onclick = () => { toCalcTab = true; $("gpscalctab").checked = true; APP.showTab("calc"); CALC.keys(["EXIT", "F6", "F6", "F6", "F1"]); };
  }
  /* every second: the state read; the Route tab's live layer fed; this tab shown when it is open */
  setInterval(() => {
    if (!window.Module || !Module.ready || typeof APP === "undefined" || !window.NAV47_UP) return;
    if (S().tab === "gps") { show(); }
    else if (src || follow) { st = read(); followStep(st); }
    if (typeof LIVE !== "undefined" && st) LIVE.tick(st, sats);
  }, 1000);
  /* #gpsdemo: the demo at once (tests, pictures), when the page is up */
  if (/gpsdemo/.test(location.hash)) { const w = setInterval(() => { if (window.NAV47_UP) { clearInterval(w); demo(); } }, 200); }

  return { show, line, demo, stop, state: () => st, sats: () => sats, running: () => !!src };
})();
