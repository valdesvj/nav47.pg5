/* dp.js - NAV47 on the web: the DP tab. The platform (a square or a rectangle, its faces, its height) and the vessel
 * (a DPCAP47 .DPV) are made here and saved as DPOPS.TXT and VESSEL.DPV for the calculator's DP OPS; the same C
 * (src/dpops.c, DPCAP47's engine, in nav47.js) finds the best heading off the face, inside the thrusters' working
 * limits, with the platform's lee on the wind, and after the worst single failure. Static (DNV-ST-0111 level 1's
 * forces x 1.25): planning, not operations. */
"use strict";
const DPTAB = (() => {
  const S = () => APP.state, N = () => APP.nav, F = () => APP.fmt;
  const jd = () => S().live ? Date.now() / 864e5 + 2440587.5 : S().jd;
  const SIDES = ["STERN", "STBD", "PORT", "BOW"], SIDE_T = ["stern to", "starboard side to", "port side to", "bow to"];
  const KEY = "nav47web:dp", VKEY = "nav47web:dpv";
  const D0 = { name: "EXAMPLE PQ", lat: null, lon: null, shape: "rect", len: 40, wid: 30, orient: 30, height: 45, solid: false,
               face: -1, side: 0, standoff: 10, tol: 30, smin: 30, slen: 5, loa: 95, lims: "TUN 30",
               wfrom: 330, wkn: 25, cauto: true, cset: 135, ckn: 1.0, sauto: true, vfrom: 330, hs: 2, tp: 7, hdg: null };
  let P = Object.assign({}, D0, JSON.parse(localStorage.getItem(KEY) || "{}"));
  let VES = localStorage.getItem(VKEY) || "";
  let res = null, sweep = [], limw = null, csrc = "", err = "";
  const save = () => localStorage.setItem(KEY, JSON.stringify(P));
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  const pos = () => P.lat === null ? S().pos : [P.lat, P.lon];

  /* DPOPS.TXT: the calculator's file */
  function platText() {
    const [la, lo] = pos(), wid = P.shape === "square" ? P.len : P.wid;
    const lims = P.lims.split(/[\n,;]+/).map(s => s.trim()).filter(s => /^\S+\s+\d+(\.\d+)?$/.test(s));
    return ["NAV47 DPOPS 1", `PLATFORM ${P.name.replace(/,/g, " ").slice(0, 23)},${la.toFixed(5)},${lo.toFixed(5)}`,
            `SIZE ${(+P.len).toFixed(1)} ${(+wid).toFixed(1)}`, `ORIENT ${String(Math.round(P.orient) % 360).padStart(3, "0")}`,
            `HEIGHT ${Math.round(P.height)}`, `SOLID ${P.solid ? 1 : 0}`, `FACE ${P.face < 0 ? "AUTO" : P.face}`, `SIDE ${SIDES[P.side]}`,
            `STANDOFF ${(+P.standoff).toFixed(1)}`, `TOL ${Math.round(P.tol)}`, `SHELTER ${(P.smin / 100).toFixed(2)} ${(+P.slen).toFixed(1)}`,
            `LOA ${(+P.loa).toFixed(1)}`, ...lims.map(l => "LIM " + l)].join("\n") + "\n";
  }
  function setC() {
    err = "";
    const r = Module.ccall("api_dp_set", "number", ["string", "string"], [platText(), VES]);
    if (r) err = (r === 1 ? "DPOPS: " : "the vessel file: ") + N().str(Module._api_dp_err());
    return !r;
  }
  function weather() {
    const [la, lo] = pos();
    let cset = +P.cset, ckn = +P.ckn;
    if (P.cauto) {
      Module._api_dp_current(la, lo, jd(), +P.wfrom, +P.wkn);
      const o = N().o(); cset = o[0]; ckn = o[1]; csrc = N().str(Module._api_dp_csrc());
    } else csrc = "typed";
    let vfrom = +P.vfrom, hs = +P.hs, tp = +P.tp;
    if (P.sauto) { Module._api_dp_sea(+P.wkn); const o = N().o(); vfrom = +P.wfrom; hs = o[0]; tp = o[1]; }
    Module._api_dp_wx(+P.wfrom, +P.wkn, cset, ckn, vfrom, hs, tp);
    return { cset, ckn, vfrom, hs, tp };
  }
  function read() {
    const o = Array.from(N().o().subarray(0, 80));
    const r = { hdg: o[0], face: o[1], px: o[2], py: o[3], wleft: o[4], vleft: o[5], fmag: o[6], fto: o[7], blow: o[8],
                held: !!o[9], full: !!o[10], worst: o[11], wthr: o[12], fheld: !!o[13], fgrp: o[14], fuse: o[15], rank: o[16], normal: o[17],
                hull: Array.from({ length: 7 }, (_, i) => [o[20 + 2 * i], o[21 + 2 * i]]), use: o.slice(40, 52), fuse_t: o.slice(60, 72) };
    return r;
  }
  function vessel() {
    Module._api_dp_vessel();
    const o = N().o(), n = o[3];
    return { lpp: o[0], b: o[1], loa: o[2], thr: Array.from({ length: n }, (_, i) => ({ name: N().str(Module._api_dp_tname(i)), type: N().str(Module._api_dp_ttype(i)),
      x: o[4 + 5 * i], y: o[5 + 5 * i], grp: o[6 + 5 * i], kn: o[7 + 5 * i], lim: o[8 + 5 * i] })) };
  }
  function compute(best) {
    if (!setC()) { res = null; return; }
    const wx = weather();
    if (best || P.hdg === null) { Module._api_dp_best(); res = read(); P.hdg = null; limw = Module._api_dp_limit(); }
    else { Module._api_dp_eval(P.face < 0 ? (res ? res.face : 0) : P.face, +P.hdg); res = read(); limw = Module._api_dp_limit(); }
    res.wx = wx;
    const n = Module._api_dp_sweep(res.face), o = N().o();
    sweep = Array.from({ length: n }, (_, i) => ({ h: o[3 * i], rank: o[3 * i + 1], worst: o[3 * i + 2] }));
  }

  /* the picture: north up, metres; the lee shaded, the platform hatched, its working face thick, the vessel, its
     thrusters' loads, the weather's arrows; round it, each heading's answer on this face */
  function picture(cv, V) {
    const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    cv.width = r.width * dpr; cv.height = r.height * dpr;
    const g = cv.getContext("2d"); g.scale(dpr, dpr);
    const W = r.width, H = r.height, css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
    g.fillStyle = css("--sea") || "#dfe9f3"; g.fillRect(0, 0, W, H);
    if (!res) return;
    const wid = P.shape === "square" ? P.len : P.wid, o = P.orient * Math.PI / 180, a0 = [Math.sin(o), Math.cos(o)], a1 = [Math.cos(o), -Math.sin(o)];
    const pc = [[1, 1], [1, -1], [-1, -1], [-1, 1]].map(([s, t]) => [s * P.len / 2 * a0[0] + t * wid / 2 * a1[0], s * P.len / 2 * a0[1] + t * wid / 2 * a1[1]]);
    const cx = res.px / 2, cy = res.py / 2;
    let R = 1; [...pc, ...res.hull].forEach(([x, y]) => R = Math.max(R, Math.hypot(x - cx, y - cy)));
    const ring = Math.min(W, H) / 2 - 22, k = (ring - 34) / R, X0 = W / 2, Y0 = H / 2;
    const X = x => X0 + (x - cx) * k, Y = y => Y0 - (y - cy) * k;
    /* the ring: each heading's answer (the bow's heading, this face, this side) */
    const col = rk => ["#c0392b", "#e67e22", "#e5c02a", "#2e9e5b"][rk];
    for (const s of sweep) {
      const a = (s.h - 90) * Math.PI / 180;
      g.beginPath(); g.arc(X0, Y0, ring, a - 2.5 * Math.PI / 180, a + 2.5 * Math.PI / 180);
      g.strokeStyle = col(s.rank); g.lineWidth = 9; g.stroke();
    }
    g.fillStyle = css("--soft") || "#667"; g.font = "11px system-ui"; g.textAlign = "center";
    for (let d = 0; d < 360; d += 90) { const a = (d - 90) * Math.PI / 180; g.fillText(String(d).padStart(3, "0"), X0 + (ring + 16) * Math.cos(a), Y0 + (ring + 16) * Math.sin(a) + 4); }
    { const a = (res.hdg - 90) * Math.PI / 180; g.beginPath(); g.moveTo(X0 + (ring - 9) * Math.cos(a), Y0 + (ring - 9) * Math.sin(a));
      g.lineTo(X0 + (ring + 9) * Math.cos(a), Y0 + (ring + 9) * Math.sin(a)); g.strokeStyle = "#1b2129"; g.lineWidth = 3; g.stroke(); }
    /* the lee: the platform swept downwind for SHELTER's length */
    const D = P.slen * P.height, wf = P.wfrom * Math.PI / 180, dw = [-Math.sin(wf) * D, -Math.cos(wf) * D];
    const pts = [...pc, ...pc.map(([x, y]) => [x + dw[0], y + dw[1]])];
    const hull = convex(pts);
    g.beginPath(); hull.forEach(([x, y], i) => i ? g.lineTo(X(x), Y(y)) : g.moveTo(X(x), Y(y))); g.closePath();
    g.fillStyle = "rgba(90,120,160,0.18)"; g.fill();
    /* the platform */
    g.beginPath(); pc.forEach(([x, y], i) => i ? g.lineTo(X(x), Y(y)) : g.moveTo(X(x), Y(y))); g.closePath();
    g.fillStyle = "#8a9097"; g.fill(); g.strokeStyle = "#1b2129"; g.lineWidth = 1.2; g.stroke();
    const fk = [[0, 1], [3, 0], [2, 3], [1, 2]][res.face & 3];   /* face 0 +a0, 1 +a1 (orient + 90), 2 -a0, 3 -a1 */
    g.beginPath(); g.moveTo(X(pc[fk[0]][0]), Y(pc[fk[0]][1])); g.lineTo(X(pc[fk[1]][0]), Y(pc[fk[1]][1])); g.strokeStyle = "#d63a2a"; g.lineWidth = 4; g.stroke();
    g.fillStyle = "#fff"; g.font = "bold 11px system-ui"; g.fillText(P.name, X(0), Y(0) + 4);
    /* the standoff line */
    const nb = res.normal * Math.PI / 180, nx = Math.sin(nb), ny = Math.cos(nb), dd = ((res.face & 1) ? wid : P.len) / 2 + +P.standoff;
    g.setLineDash([4, 4]); g.beginPath();
    g.moveTo(X(nx * dd + ny * 60), Y(ny * dd - nx * 60)); g.lineTo(X(nx * dd - ny * 60), Y(ny * dd + nx * 60));
    g.strokeStyle = "#d63a2a"; g.lineWidth = 1; g.stroke(); g.setLineDash([]);
    /* the vessel */
    g.beginPath(); res.hull.forEach(([x, y], i) => i ? g.lineTo(X(x), Y(y)) : g.moveTo(X(x), Y(y))); g.closePath();
    g.fillStyle = "#f4f4f2"; g.fill(); g.strokeStyle = "#1b2129"; g.lineWidth = 1.5; g.stroke();
    const h = res.hdg * Math.PI / 180, f = [Math.sin(h), Math.cos(h)], sb = [Math.cos(h), -Math.sin(h)];
    const at = (x, y) => [res.px + x * f[0] + y * sb[0], res.py + x * f[1] + y * sb[1]];
    const [hx, hy] = at(V.loa * 0.33, 0);
    g.beginPath(); g.arc(X(hx), Y(hy), V.b * 0.32 * k, 0, 2 * Math.PI); g.fillStyle = "#3d444c"; g.fill();
    g.save(); g.translate(X(hx), Y(hy)); g.rotate(h + Math.PI / 2); g.fillStyle = "#fff"; g.font = `bold ${Math.max(8, V.b * 0.35 * k)}px system-ui`;
    g.textBaseline = "middle"; g.fillText("H", 0, 0); g.restore();
    /* the thrusters: their load (intact), as a disc */
    V.thr.forEach((t, i) => {
      const [x, y] = at(t.x, t.y), u = res.use[i] || 0, lim = t.lim || 1;
      g.beginPath(); g.arc(X(x), Y(y), 3 + 7 * Math.min(1, u), 0, 2 * Math.PI);
      g.fillStyle = u > lim * 1.001 ? "#c0392b" : u > lim * 0.8 ? "#e5a02a" : "#2e9e5b"; g.fill();
    });
    /* the weather */
    const arrows = [[P.wfrom, `wind ${Math.round(P.wkn)} kn`, "#1b2129", +P.wkn], [res.wx.cset + 180, `current ${res.wx.ckn.toFixed(1)} kn`, "#1f6fb2", res.wx.ckn],
                    [res.wx.vfrom, `sea ${res.wx.hs.toFixed(1)} m`, "#6b4fa0", res.wx.hs]];
    arrows.forEach(([from, label, c, v], i) => {
      if (!(v > 0)) return;
      const a = from * Math.PI / 180, r0 = ring - 30 - 18 * i, sx = X0 + r0 * Math.sin(a), sy = Y0 - r0 * Math.cos(a), L = 34;
      const ex = sx - L * Math.sin(a), ey = sy + L * Math.cos(a);
      g.strokeStyle = c; g.fillStyle = c; g.lineWidth = 2.5; APP.arrowPath(g, (sx + ex) / 2, (sy + ey) / 2, (from + 180) % 360, L);
      g.font = "11px system-ui"; g.textAlign = "center"; g.fillText(label, sx + 14 * Math.sin(a), sy - 14 * Math.cos(a));
    });
    g.fillStyle = "#1b2129"; g.textAlign = "left"; g.font = "bold 12px system-ui"; g.fillText("N ↑", 10, 18);
    const m = Math.pow(10, Math.floor(Math.log10(40 / k * 2))), bar = m * k;
    g.fillRect(10, H - 14, bar, 2); g.font = "11px system-ui"; g.fillText(`${m} m`, 14 + bar, H - 9);
  }
  function convex(p) {
    p = p.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    const cr = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]), lo = [], up = [];
    for (const q of p) { while (lo.length > 1 && cr(lo[lo.length - 2], lo[lo.length - 1], q) <= 0) lo.pop(); lo.push(q); }
    for (const q of p.reverse()) { while (up.length > 1 && cr(up[up.length - 2], up[up.length - 1], q) <= 0) up.pop(); up.push(q); }
    return lo.slice(0, -1).concat(up.slice(0, -1));
  }

  function render(best) {
    const el = document.getElementById("t-dp");
    compute(best === true);
    const V = res ? vessel() : { thr: [], loa: 0, b: 0 };
    const [la, lo] = pos(), num = (id, v, step, w) => `<input class="num" id="dp_${id}" type="number" step="${step || 1}" value="${v}" style="width:${w || 70}px">`;
    const st = !res ? "" : res.held ? (res.fheld ? ["ok", "HOLDS inside the working limits, and after the worst single failure"] : ["warn", `HOLDS inside the limits; NOT after group ${res.fgrp} is lost`])
      : res.full ? ["warn", "holds only BEYOND the working limits"] : ["bad", "NO HOLD"];
    const objs = (S().objects || []).filter(o => /platform|rig|wellhead|spm/.test(o.type));
    el.innerHTML = `<div class="grid2" style="display:grid;grid-template-columns:minmax(380px,1.25fr) minmax(330px,1fr);gap:14px">
     <div class="card"><h2>DP operation · ${esc(P.name)} <span class="meta">${F().pos(la, lo)}</span></h2>
      <canvas id="dpcv" style="width:100%;height:560px;border-radius:8px"></canvas>
      ${err ? `<div class="meta" style="color:var(--now)">${esc(err)}</div>` : ""}
      ${res ? `<div class="pill ${st[0]}" style="margin:8px 0;padding:6px 10px;border-radius:8px;font-weight:600;background:${st[0] === "ok" ? "#dff3e6" : st[0] === "warn" ? "#fdf0d5" : "#f8dcd8"}">${st[1]}</div>
      <table><tr><td>Heading</td><td class="num mono">${String(Math.round(res.hdg)).padStart(3, "0")}° ${P.hdg === null ? "(the best)" : "(yours)"}</td>
        <td>Face</td><td class="num mono">${String(Math.round(res.normal)).padStart(3, "0")}°${P.face < 0 ? " (AUTO)" : ""}, ${SIDE_T[P.side]}, ${P.standoff} m off</td></tr>
       <tr><td>Wind in the lee</td><td class="num mono">${Math.round(res.wleft * 100)} % of it</td><td>Weather's push</td>
        <td class="num mono">${Math.round(res.fmag)} kN to ${String(Math.round(res.fto)).padStart(3, "0")}°, ${res.blow >= 0 ? "blow-off" : "<b>blow-on</b>"} ${Math.abs(Math.round(res.blow))} kN</td></tr>
       <tr><td>Wind limit</td><td class="num mono">${limw >= 64 ? "> 64" : limw.toFixed(0)} kn</td><td colspan="2" class="meta">the wind from ${Math.round(P.wfrom)}° raised with its sea (DNV Table 2-1), inside the limits, this heading</td></tr></table>
      <table style="margin-top:8px"><tr><th>Thruster</th><th>Type</th><th class="num">Group</th><th class="num">Full kN</th><th class="num">Limit</th><th style="width:34%">Intact</th><th class="num">Group ${res.fgrp} lost</th></tr>
       ${V.thr.map((t, i) => { const u = res.use[i] || 0, l = t.lim || 1, c = u > l * 1.001 ? "#c0392b" : u > l * 0.8 ? "#e5a02a" : "#2e9e5b";
         return `<tr><td>${esc(t.name)}</td><td>${esc(t.type)}</td><td class="num">${t.grp}</td><td class="num mono">${Math.round(t.kn)}</td><td class="num mono">${Math.round(l * 100)} %</td>
         <td><div style="position:relative;height:12px;background:#e9ebec;border-radius:3px"><div style="width:${Math.min(100, u * 100)}%;height:12px;background:${c};border-radius:3px"></div>
           ${l < 1 ? `<div style="position:absolute;left:${l * 100}%;top:-3px;width:2px;height:18px;background:#1b2129"></div>` : ""}</div>
           <span class="mono" style="font-size:12px">${Math.round(u * 100)} %</span></td>
         <td class="num mono">${t.grp === res.fgrp ? "lost" : Math.round((res.fuse_t[i] || 0) * 100) + " %"}</td></tr>`; }).join("")}</table>
      <div class="meta">The ring: the answer at each heading of the bow on this face (green holds, and after a failure; yellow holds;
       orange only beyond the limits; red no hold). The thrusters' discs: their load. The shaded area: the platform's lee
       (taller than the vessel: ${P.smin} % of the wind left close in, all of it ${P.slen} heights downwind).</div>` : ""}
      <div class="controls" style="margin-top:8px"><button id="dp_best" class="on">Best heading</button>
       <label>Heading ${num("hdg", res ? Math.round(res.hdg) : 0, 5)}°</label><button class="small" id="dp_hm">−5°</button><button class="small" id="dp_hp">+5°</button></div>
     </div>
     <div>
      <div class="card"><h2>Weather <span class="meta">at ${F().dt(jd(), 0)} UT</span></h2>
       <div class="controls"><label>Wind from ${num("wfrom", P.wfrom)}°</label><label>${num("wkn", P.wkn, 1)} kn</label></div>
       <div class="controls"><label><input type="checkbox" id="dp_cauto" ${P.cauto ? "checked" : ""}> Current from the flow program</label>
        ${P.cauto ? `<span class="meta">${res ? `${String(Math.round(res.wx.cset)).padStart(3, "0")}° ${res.wx.ckn.toFixed(2)} kn` : ""} · ${esc(csrc)}</span>`
          : `<label>sets to ${num("cset", P.cset)}°</label><label>${num("ckn", P.ckn, 0.1)} kn</label>`}</div>
       <div class="controls"><label><input type="checkbox" id="dp_sauto" ${P.sauto ? "checked" : ""}> Sea from the wind (DNV)</label>
        ${P.sauto ? `<span class="meta">${res ? `Hs ${res.wx.hs.toFixed(1)} m, Tp ${res.wx.tp.toFixed(1)} s, from the wind` : ""}</span>`
          : `<label>from ${num("vfrom", P.vfrom)}°</label><label>Hs ${num("hs", P.hs, 0.1)} m</label><label>Tp ${num("tp", P.tp, 0.5)} s</label>`}</div>
       <div class="meta">The time is the page's (UT above, Now). The current: your FLOW atlas, else a stream station within 30 NM (the Tide / Flow tabs' files).</div></div>
      <div class="card"><h2>Platform</h2>
       <div class="controls"><label>Name <input id="dp_name" value="${esc(P.name)}" style="width:130px"></label>
        <label>Position <input class="pos" id="dp_pos" value="${F().pos(la, lo)}" style="width:190px"></label>
        <button class="small" id="dp_dr">the DR</button>
        ${objs.length ? `<select id="dp_obj"><option value="">an object…</option>${objs.map((o, i) => `<option value="${i}">${esc(o.name || o.type)}</option>`).join("")}</select>` : ""}</div>
       <div class="controls"><label>Shape <select id="dp_shape"><option value="square">square</option><option value="rect">rectangle</option></select></label>
        <label>${P.shape === "square" ? "Side" : "Length"} ${num("len", P.len)} m</label>${P.shape === "square" ? "" : `<label>Width ${num("wid", P.wid)} m</label>`}
        <label>Face 0 looks to ${num("orient", P.orient, 5)}°</label></div>
       <div class="controls"><label>Height ${num("height", P.height)} m</label>
        <label title="a gravity base: the waves sheltered too; a jacket lets them through"><input type="checkbox" id="dp_solid" ${P.solid ? "checked" : ""}> solid (waves sheltered)</label>
        <label>Lee: ${num("smin", P.smin, 5, 60)} % of the wind close in, all of it at ${num("slen", P.slen, 0.5, 60)} heights</label></div>
       <div class="controls"><label>Working face <select id="dp_face"><option value="-1">AUTO (the best)</option>
         ${[0, 1, 2, 3].map(f => `<option value="${f}">${f}: looks to ${String(Math.round((+P.orient + 90 * f) % 360)).padStart(3, "0")}°</option>`).join("")}</select></label>
        <label>The vessel <select id="dp_side">${SIDE_T.map((s, i) => `<option value="${i}">${s}</option>`).join("")}</select></label></div>
       <div class="controls"><label>Standoff ${num("standoff", P.standoff)} m</label><label>Heading ± ${num("tol", P.tol, 5)}° of square</label></div></div>
      <div class="card"><h2>Vessel <span class="meta">${res ? esc(N().str(Module._api_dp_vname())) : ""}</span></h2>
       <div class="controls"><label>LOA ${num("loa", P.loa)} m</label><label>Working limits <input id="dp_lims" value="${esc(P.lims)}" style="width:150px" title="TYPE % or NAME %: TUN 30, BT3 40, AZ 80"></label></div>
       <details><summary>The vessel file (DPCAP47 .DPV) ${VES ? "· yours" : "· the example: 95 m, three bow tunnels, two azimuths (made up)"}</summary>
        <textarea id="dp_ves" spellcheck="false" style="width:100%;height:220px;font:12px ui-monospace,monospace">${esc(VES || N().str(Module._api_dp_example_vessel()))}</textarea>
        <div class="controls"><button class="small" id="dp_vapply">Apply</button><label>Load <input type="file" id="dp_vfile" accept=".dpv,.txt"></label>
         <button class="small" id="dp_vex">The example</button></div></details></div>
      <div class="card"><h2>To the calculator</h2>
       <div class="controls"><button id="dp_tocalc">Send to the Calculator tab</button><button id="dp_savep">Save DPOPS.TXT</button><button id="dp_savev">Save VESSEL.DPV</button></div>
       <div class="meta">Copy both to /NAV47/ on the DM42n; DP OPS (menu 3, DP) reads them (LOAD reads them again). The wind, the current and
        the sea are the calculator's own (SET, FLOW, INPUT).</div>
       <details><summary>DPOPS.TXT</summary><pre class="mono" style="font-size:12px">${esc(platText())}</pre></details></div>
     </div></div>
     <div class="meta" style="margin:10px 2px">DPCAP47's engine: DNV-ST-0111 level 1's wind, current and wave-drift forces (x 1.25), each from its own
      direction, its thrust allocation with the thrusters' working limits in the intact case and full thrust after a
      redundancy group is lost. Static: no position excursions, no thruster wash on the platform, no current shelter. Planning, not operations: check against the vessel's DP FMEA, ASOG and capability plots.</div>`;
    if (res) picture(document.getElementById("dpcv"), V);
    bind();
  }
  function bind() {
    const $ = id => document.getElementById("dp_" + id), on = (id, f) => { const e = $(id); if (e) e.onchange = f; };
    const numf = ["wfrom", "wkn", "cset", "ckn", "vfrom", "hs", "tp", "len", "wid", "orient", "height", "smin", "slen", "standoff", "tol", "loa"];
    numf.forEach(k => on(k, e => { const v = parseFloat(e.target.value); if (isFinite(v)) { P[k] = v; save(); render(true); } }));
    on("name", e => { P.name = e.target.value.trim() || "PLATFORM"; save(); render(); });
    on("pos", e => { const p = APP.parsePos(e.target.value); if (p) { P.lat = p[0]; P.lon = p[1]; save(); } render(true); });
    $("dr").onclick = () => { P.lat = null; P.lon = null; save(); render(true); };
    on("obj", e => { const o = (S().objects || []).filter(o => /platform|rig|wellhead|spm/.test(o.type))[+e.target.value]; if (o) { P.lat = o.lat; P.lon = o.lon; P.name = (o.name || o.type).toUpperCase().slice(0, 23); save(); } render(true); });
    $("shape").value = P.shape; on("shape", e => { P.shape = e.target.value; save(); render(true); });
    $("face").value = String(P.face); on("face", e => { P.face = +e.target.value; P.hdg = null; save(); render(true); });
    $("side").value = String(P.side); on("side", e => { P.side = +e.target.value; P.hdg = null; save(); render(true); });
    on("solid", e => { P.solid = e.target.checked; save(); render(true); });
    on("cauto", e => { P.cauto = e.target.checked; save(); render(true); });
    on("sauto", e => { P.sauto = e.target.checked; save(); render(true); });
    on("lims", e => { P.lims = e.target.value; save(); render(true); });
    $("best").onclick = () => { P.hdg = null; save(); render(true); };
    on("hdg", e => { P.hdg = ((parseFloat(e.target.value) || 0) % 360 + 360) % 360; save(); render(); });
    $("hm").onclick = () => { P.hdg = ((res ? res.hdg : 0) + 355) % 360; save(); render(); };
    $("hp").onclick = () => { P.hdg = ((res ? res.hdg : 0) + 5) % 360; save(); render(); };
    $("vapply").onclick = () => { VES = $("ves").value; localStorage.setItem(VKEY, VES); render(true); };
    $("vex").onclick = () => { VES = ""; localStorage.removeItem(VKEY); render(true); };
    on("vfile", e => { const f = e.target.files[0]; if (f) f.text().then(t => { VES = t.replace(/\r/g, ""); localStorage.setItem(VKEY, VES); render(true); }); });
    $("savep").onclick = () => APP.download("DPOPS.TXT", platText());
    $("savev").onclick = () => APP.download("VESSEL.DPV", VES || N().str(Module._api_dp_example_vessel()));
    $("tocalc").onclick = () => {
      localStorage.setItem("nav47fs:/simdisk/NAV47/VESSEL.DPV", VES || N().str(Module._api_dp_example_vessel()));
      CALC.putAndRestart("DPOPS.TXT", platText());
    };
  }
  return { render };
})();
if (typeof TABS2 !== "undefined") TABS2.dp = () => DPTAB.render();
