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
               face: -1, side: 1, par: true, standoff: 10, mincl: 5, tol: 30, smin: 30, slen: 5, loa: 95, lims: "TUN 30",
               wfrom: 330, wkn: 25, cauto: true, cset: 135, ckn: 1.0, sauto: true, vfrom: 330, hs: 2, tp: 7, hdg: null,
               acur: -1, amargin: 20 };                    /* ABS SKP: the current (-1 today's, 0.5 .. 3 kn), the margin % */
  let P = Object.assign({}, D0, JSON.parse(localStorage.getItem(KEY) || "{}"));
  let VES = localStorage.getItem(VKEY) || "";
  let res = null, sweep = [], limw = null, csrc = "", err = "", quick = [];
  let ABS = { key: "", r: null, ms: 0 };                     /* ABS SKP: computed once a vessel / current / margin */
  /* the set-ups compared at a click: [side, parallel, label] */
  const SETUPS = [[1, true, "Starboard side to, parallel"], [2, true, "Port side to, parallel"], [0, false, "Stern to"], [3, false, "Bow to"]];
  const setupName = () => P.side === 1 || P.side === 2 ? `${SIDE_T[P.side]}, ${P.par ? "parallel to the face" : `within ±${P.tol}° of parallel`}` : `${SIDE_T[P.side]}, ±${P.tol}°`;
  const save = () => localStorage.setItem(KEY, JSON.stringify(P));
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
  const pos = () => P.lat === null ? S().pos : [P.lat, P.lon];

  /* DPOPS.TXT: the calculator's file */
  function platText() {
    const [la, lo] = pos(), wid = P.shape === "square" ? P.len : P.wid;
    const lims = P.lims.split(/[\n,;]+/).map(s => s.trim()).filter(s => /^\S+\s+\d+(\.\d+)?$/.test(s));
    return ["NAV47 DPOPS 1", `PLATFORM ${P.name.replace(/,/g, " ").slice(0, 23)},${la.toFixed(5)},${lo.toFixed(5)}`,
            `SIZE ${(+P.len).toFixed(1)} ${(+wid).toFixed(1)}`, `ORIENT ${String(Math.round(P.orient) % 360).padStart(3, "0")}`,
            `HEIGHT ${Math.round(P.height)}`, `SOLID ${P.solid ? 1 : 0}`, `FACE ${P.face < 0 ? "AUTO" : P.face}`, `SIDE ${SIDES[P.side]}`, `PARALLEL ${P.par ? 1 : 0}`,
            `STANDOFF ${(+P.standoff).toFixed(1)}`, `MINCLEAR ${(+P.mincl).toFixed(1)}`, `TOL ${Math.round(P.tol)}`, `SHELTER ${(P.smin / 100).toFixed(2)} ${(+P.slen).toFixed(1)}`,
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
    const o = Array.from(N().o().subarray(0, 130));
    const r = { hdg: o[0], face: o[1], px: o[2], py: o[3], wleft: o[4], vleft: o[5], fmag: o[6], fto: o[7], blow: o[8],
                held: !!o[9], full: !!o[10], worst: o[11], wthr: o[12], fheld: !!o[13], fgrp: o[14], fuse: o[15], rank: o[16], normal: o[17], clear: o[18], geo: !!o[19], mid: o[37],
                hull: Array.from({ length: 7 }, (_, i) => [o[20 + 2 * i], o[21 + 2 * i]]), use: o.slice(40, 52), fuse_t: o.slice(60, 72),
                f: Array.from({ length: 12 }, (_, i) => [o[80 + 2 * i], o[81 + 2 * i]]), ff: Array.from({ length: 12 }, (_, i) => [o[104 + 2 * i], o[105 + 2 * i]]) };
    return r;
  }
  function vessel() {
    Module._api_dp_vessel();
    const o = N().o(), n = o[3];
    return { lpp: o[0], b: o[1], loa: o[2], thr: Array.from({ length: n }, (_, i) => ({ name: N().str(Module._api_dp_tname(i)), type: N().str(Module._api_dp_ttype(i)),
      x: o[4 + 5 * i], y: o[5 + 5 * i], grp: o[6 + 5 * i], kn: o[7 + 5 * i], lim: o[8 + 5 * i] })) };
  }
  function compute(best) {
    /* each set-up's best, for the quick comparison (the page's own set-up last) */
    const keep = [P.side, P.par];
    quick = SETUPS.map(([sd, par, label]) => {
      P.side = sd; P.par = par;
      if (!setC()) return { label, ok: false };
      weather(); Module._api_dp_best(); const r = read();
      return { label, sd, par, ok: true, rank: r.rank, hdg: r.hdg, normal: r.normal, worst: r.worst, fgrp: r.fgrp, blow: r.blow };
    });
    [P.side, P.par] = keep;
    /* Best heading (P.bestAll): the best on the face of every set-up, side to parallel or within the tolerance, stern
       to, bow to: the one that holds best (holds after a failure, holds, beyond the limits), then the least loaded;
       its side taken, so the vessel goes to that heading */
    if (best && P.bestAll) {
      let top = null;
      for (const [sd, par] of [[1, true], [2, true], [1, false], [2, false], [0, false], [3, false]]) {
        P.side = sd; P.par = par;
        if (!setC()) continue;
        weather(); Module._api_dp_best(); const r = read();
        const better = !top || r.rank > top.rank || (r.rank === top.rank && r.worst < top.worst - 1e-4);
        if (better) top = { sd, par, rank: r.rank, worst: r.worst };
      }
      if (top) { P.side = top.sd; P.par = top.par; save(); } else [P.side, P.par] = keep;
    }
    if (!setC()) { res = null; return; }
    const wx = weather();
    if (best || P.hdg === null) { Module._api_dp_best(); res = read(); P.hdg = null; limw = Module._api_dp_limit(); }
    else { Module._api_dp_eval(P.face < 0 ? (res ? res.face : 0) : P.face, +P.hdg); res = read(); limw = Module._api_dp_limit(); }
    res.wx = wx;
    const n = Module._api_dp_sweep(res.face), o = N().o();
    sweep = Array.from({ length: n }, (_, i) => ({ h: o[3 * i], rank: o[3 * i + 1], worst: o[3 * i + 2] }));
  }

  /* ABS SKP (the ABS DPS Guide, Section 9) as the calculator's: the current today's (to half a knot, 0.5 .. 3) or chosen */
  const absCur = () => P.acur >= 0 ? +P.acur : Math.min(3, Math.max(0.5, Math.round((res && res.wx ? res.wx.ckn : 1) * 2) / 2));
  function absCompute() {
    const cur = absCur(), key = [platText(), VES, cur, P.amargin].join("|");
    if (ABS.key === key) return;
    const t0 = performance.now(), ok = Module._api_dp_abs(cur, P.amargin / 100), o = N().o();
    ABS = { key, ms: performance.now() - t0, r: ok ? { a: o[0], b: o[1], c: o[2], d: o[3], bt: o[4], ct: o[5], dg: o[6], cur,
      intact: Array.from(o.subarray(10, 46)), worst: Array.from(o.subarray(50, 86)) } : null };
  }
  const skp = p => p < 0 || p > 100.5 ? "-" : String(Math.round(p));
  const absAt = (w, rel) => { rel = ((rel % 360) + 360) % 360; const h = Math.floor(rel / 10) % 36, k = (h + 1) % 36, f = (rel - h * 10) / 10; return w[h] + f * (w[k] - w[h]); };
  /* the polar: 0 the bow up, the limiting 1-min wind (m/s) from each direction, rings 10 .. 40; today's wind its arrow */
  function absPolar(cv, A, rel) {
    const b = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    cv.width = b.width * dpr; cv.height = b.height * dpr;
    const g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0);
    const W = b.width, H = b.height, cx = W / 2, cy = H / 2, R = Math.min(W, H) / 2 - 22, D = Math.PI / 180;
    g.fillStyle = "#f7f8fa"; g.fillRect(0, 0, W, H);
    g.strokeStyle = "#c9ced3"; g.fillStyle = "#6b7480"; g.font = "11px system-ui"; g.lineWidth = 1;
    for (let k = 1; k <= 4; k++) { g.beginPath(); g.arc(cx, cy, R * k / 4, 0, 2 * Math.PI); g.stroke(); g.fillText(k * 10 + " m/s", cx + 3, cy - R * k / 4 + 12); }
    for (let a = 0; a < 360; a += 30) { g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx + R * Math.sin(a * D), cy - R * Math.cos(a * D)); g.stroke();
      g.fillText(String(a).padStart(3, "0"), cx + (R + 12) * Math.sin(a * D) - 9, cy - (R + 12) * Math.cos(a * D) + 4); }
    const curve = (w, dash, col) => { g.beginPath(); g.setLineDash(dash); g.strokeStyle = col; g.lineWidth = 2.5;
      for (let a = 0; a <= 360; a += 2.5) { const r = Math.min(40, absAt(w, a)) / 40 * R, x = cx + r * Math.sin(a * D), y = cy - r * Math.cos(a * D); a ? g.lineTo(x, y) : g.moveTo(x, y); }
      g.stroke(); g.setLineDash([]); };
    curve(A.intact, [], "#1f5fa8"); curve(A.worst, [7, 5], "#d63a2a");
    g.fillStyle = "#1b2129"; g.beginPath(); g.moveTo(cx, cy - 16); g.lineTo(cx + 5, cy - 8); g.lineTo(cx + 5, cy + 12); g.lineTo(cx - 5, cy + 12); g.lineTo(cx - 5, cy - 8); g.closePath(); g.fill();
    if (rel !== null) {                                        /* today's wind: from its direction off the bow */
      const x0 = cx + (R + 4) * Math.sin(rel * D), y0 = cy - (R + 4) * Math.cos(rel * D), x1 = cx + (R - 34) * Math.sin(rel * D), y1 = cy - (R - 34) * Math.cos(rel * D);
      g.strokeStyle = "#2e8b57"; g.lineWidth = 3; g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.stroke();
      g.fillStyle = "#2e8b57"; g.beginPath(); g.arc(x1, y1, 4, 0, 2 * Math.PI); g.fill();
    }
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
    const ring = Math.min(W, H) / 2 - 46, k = (ring - 34) / R, X0 = W / 2, Y0 = H / 2;
    const X = x => X0 + (x - cx) * k, Y = y => Y0 - (y - cy) * k;
    /* the ring: each heading's answer (the bow's heading, this face, this side) */
    const col = rk => ["#c0392b", "#e67e22", "#e5c02a", "#2e9e5b"][rk];
    for (const s of sweep) {
      const a = (s.h - 90) * Math.PI / 180;
      g.beginPath(); g.arc(X0, Y0, ring, a - 2.5 * Math.PI / 180, a + 2.5 * Math.PI / 180);
      g.strokeStyle = col(s.rank); g.lineWidth = 9; g.stroke();
    }
    g.fillStyle = css("--soft") || "#667"; g.font = "11px system-ui"; g.textAlign = "center";
    for (let d = 0; d < 360; d += 90) { const a = (d - 90) * Math.PI / 180; g.fillText(String(d).padStart(3, "0"), X0 + (ring - 18) * Math.cos(a), Y0 + (ring - 18) * Math.sin(a) + 4); }
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
    /* the thrusters: their load (intact) a disc, their force an arrow; the weather's whole push, thick, at midships (t) */
    const TF = 9.80665, fmax = Math.max(res.fmag, ...V.thr.map((t, i) => Math.hypot(res.f[i][0], res.f[i][1])), 1);
    const arrowTo = (x, y, to, L, w, c) => {
      const a = to * Math.PI / 180, ex = x + L * Math.sin(a), ey = y - L * Math.cos(a), hd = Math.min(12, 4 + w * 3);
      g.strokeStyle = c; g.fillStyle = c; g.lineWidth = w; g.lineCap = "round";
      g.beginPath(); g.moveTo(x, y); g.lineTo(ex - hd * 0.6 * Math.sin(a), ey + hd * 0.6 * Math.cos(a)); g.stroke();
      g.beginPath(); g.moveTo(ex, ey); g.lineTo(ex - hd * Math.sin(a) + hd * 0.5 * Math.cos(a), ey + hd * Math.cos(a) + hd * 0.5 * Math.sin(a));
      g.lineTo(ex - hd * Math.sin(a) - hd * 0.5 * Math.cos(a), ey + hd * Math.cos(a) - hd * 0.5 * Math.sin(a)); g.closePath(); g.fill();
      return [ex, ey];
    };
    const tips = [];
    V.thr.forEach((t, i) => {
      const [x, y] = at(t.x, t.y), u = res.use[i] || 0, lim = t.lim || 1, col = u > lim * 1.001 ? "#c0392b" : u > lim * 0.8 ? "#e5a02a" : "#2e9e5b";
      g.beginPath(); g.arc(X(x), Y(y), 4, 0, 2 * Math.PI); g.fillStyle = col; g.fill();
      const [fx_, fy_] = res.f[i], m = Math.hypot(fx_, fy_);
      let tip = [X(x), Y(y)];
      if (m >= 0.5) {
        const ex = fx_ * f[0] + fy_ * sb[0], ey = fx_ * f[1] + fy_ * sb[1], to = Math.atan2(ex, ey) * 180 / Math.PI;
        tip = arrowTo(X(x), Y(y), to, 8 + 70 * m / fmax, 2.5, col);
      }
      tips.push({ x: X(x), y: Y(y), tx: tip[0], ty: tip[1], name: t.name, t: m / TF });
    });
    /* the labels: thrusters within 22 px of each other share one (their names, each force), clear of the hull */
    const used = new Set();
    tips.forEach((p, i) => {
      if (used.has(i)) return;
      const grp = tips.map((q, j) => j).filter(j => !used.has(j) && Math.hypot(tips[j].x - p.x, tips[j].y - p.y) < 22);
      grp.forEach(j => used.add(j));
      const mx = grp.reduce((s, j) => s + tips[j].tx, 0) / grp.length, my = grp.reduce((s, j) => s + tips[j].ty, 0) / grp.length;
      const ox = mx - X(res.px), oy = my - Y(res.py), ol = Math.hypot(ox, oy) || 1;   /* out from midships */
      const lab = grp.map(j => `${tips[j].name} ${tips[j].t.toFixed(1)}`).join(" · ") + " t";
      g.font = "11px system-ui"; const w = g.measureText(lab).width + 6;
      const lx = Math.max(w / 2 + 4, Math.min(W - w / 2 - 4, mx + 16 * ox / ol)), ly = Math.max(14, Math.min(H - 6, my + 16 * oy / ol));
      g.fillStyle = "rgba(255,255,255,0.85)"; g.fillRect(lx - w / 2, ly - 10, w, 14);
      g.fillStyle = "#1b2129"; g.textAlign = "center"; g.fillText(lab, lx, ly + 1);
    });
    /* the weather's whole push (x 1.25): a circle in the lower right corner, its direction above, tonnes below */
    { const cx0 = W - 48, cy0 = H - 52, cr = 24;
      g.fillStyle = "rgba(255,255,255,0.9)"; g.beginPath(); g.arc(cx0, cy0, cr, 0, 2 * Math.PI); g.fill();
      g.strokeStyle = "#b0281e"; g.lineWidth = 2; g.stroke();
      if (res.fmag > 0.5) { const a = res.fto * Math.PI / 180; arrowTo(cx0 - (cr - 5) * Math.sin(a), cy0 + (cr - 5) * Math.cos(a), res.fto, 2 * cr - 10, 3.5, "#b0281e"); }
      g.fillStyle = "#1b2129"; g.textAlign = "center"; g.font = "bold 12px system-ui";
      g.fillText(res.fmag > 0.5 ? String(Math.round(res.fto)).padStart(3, "0") + "°" : "–", cx0, cy0 - cr - 6);
      g.fillText(`${(res.fmag / TF).toFixed(1)} t`, cx0, cy0 + cr + 15);
      g.font = "10px system-ui"; g.fillStyle = "#666"; g.fillText("weather's push", cx0, cy0 + cr + 27); }
    /* the weather: a key in the top left corner, clear of the drawing; a mark on the ring where each comes from */
    const arrows = [[P.wfrom, `wind from ${String(Math.round(P.wfrom)).padStart(3, "0")}° ${Math.round(P.wkn)} kn`, "#1b2129", +P.wkn],
                    [res.wx.cset + 180, `current to ${String(Math.round(res.wx.cset)).padStart(3, "0")}° ${res.wx.ckn.toFixed(1)} kn`, "#1f6fb2", res.wx.ckn],
                    [res.wx.vfrom, `sea from ${String(Math.round(res.wx.vfrom)).padStart(3, "0")}° ${res.wx.hs.toFixed(1)} m`, "#6b4fa0", res.wx.hs]];
    g.fillStyle = "rgba(255,255,255,0.85)"; g.fillRect(6, 26, 178, 74); g.strokeStyle = "#c9ced3"; g.lineWidth = 1; g.strokeRect(6, 26, 178, 74);
    arrows.forEach(([from, label, c, v], i) => {
      const y = 42 + 21 * i;
      g.fillStyle = c; g.strokeStyle = c; g.font = "12px system-ui"; g.textAlign = "left";
      if (v > 0) { g.lineWidth = 2.2; APP.arrowPath(g, 22, y, (from + 180) % 360, 18); } else g.fillText("–", 18, y + 4);
      g.fillText(v > 0 ? label : label.split(" ")[0] + " none", 38, y + 4);
      if (!(v > 0)) return;
      /* on the ring, as a DP console: from where it comes, pointing in; close ones set apart sideways */
      const near = arrows.slice(0, i).filter(([f2, , , v2]) => v2 > 0 && Math.abs(((f2 - from) % 360 + 540) % 360 - 180) < 12).length;
      const a = from * Math.PI / 180, side = near * 16, sx = X0 + (ring + 42) * Math.sin(a) + side * Math.cos(a), sy = Y0 - (ring + 42) * Math.cos(a) + side * Math.sin(a);
      arrowTo(sx, sy, (from + 180) % 360, 32, 3.5, c);
      g.font = "bold 13px system-ui"; g.textAlign = "center"; g.fillStyle = c;
      g.fillText(["W", "C", "S"][i], sx + 8 * Math.sin(a) + 11 * Math.cos(a), sy - 8 * Math.cos(a) + 11 * Math.sin(a) + 4);
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

  let typing = 0;
  function render(best) {
    const el = document.getElementById("t-dp");
    const open = [...el.querySelectorAll("details")].map(d => d.open);   /* the sections you opened stay open */
    compute(best === true);
    const V = res ? vessel() : { thr: [], loa: 0, b: 0 };
    const [la, lo] = pos(), num = (id, v, step, w) => `<input class="num" id="dp_${id}" type="number" step="${step || 1}" value="${v}" style="width:${w || 70}px">`;
    const st = !res ? "" : res.held ? (res.fheld ? ["ok", "HOLDS inside the working limits, and after the worst single failure"] : ["warn", `HOLDS inside the limits; NOT after group ${res.fgrp} is lost`])
      : res.full ? ["warn", "holds only BEYOND the working limits"] : ["bad", "NO HOLD"];
    const objs = (S().objects || []).filter(o => /platform|rig|wellhead|spm/.test(o.type));
    el.innerHTML = `<div class="grid2" style="display:grid;grid-template-columns:minmax(380px,1.25fr) minmax(330px,1fr);gap:14px">
     <div class="card"><h2>DP operation · ${esc(P.name)} <span class="meta">${F().pos(la, lo)}</span></h2>
      <div style="display:grid;grid-template-columns:repeat(4,1fr);gap:6px;margin-bottom:8px">${quick.map((q, i) => {
        const bg = !q.ok ? "#eee" : ["#f8dcd8", "#fbe3cc", "#fdf0d5", "#dff3e6"][q.rank], on = q.sd === P.side && (q.sd > 2 || q.sd < 1 || q.par === !!P.par);
        return `<button class="dpq" data-q="${i}" style="text-align:left;padding:6px 8px;border-radius:8px;border:${on ? "2px solid #1b2129" : "1px solid #c9ced3"};background:${bg};line-height:1.3">
          <b>${q.label}</b><br>${q.ok ? `<span class="mono">${String(Math.round(q.hdg)).padStart(3, "0")}°</span> off ${String(Math.round(q.normal)).padStart(3, "0")}° ·
          ${["no hold", "beyond limits", "holds", "holds + failure"][q.rank]}<br><span class="meta">worst ${Math.round(q.worst * 100)} % of its limit · ${q.blow >= 0 ? "blow-off" : "blow-on"}</span>` : ""}</button>`; }).join("")}</div>
      <canvas id="dpcv" style="width:100%;height:560px;border-radius:8px"></canvas>
      ${err ? `<div class="meta" style="color:var(--now)">${esc(err)}</div>` : ""}
      ${res ? `<div class="pill ${st[0]}" style="margin:8px 0;padding:6px 10px;border-radius:8px;font-weight:600;background:${st[0] === "ok" ? "#dff3e6" : st[0] === "warn" ? "#fdf0d5" : "#f8dcd8"}">${st[1]}</div>
      <div style="display:grid;grid-template-columns:max-content 1fr;gap:3px 14px;margin:4px 2px 0;font-size:14px">
       <span class="meta">Heading</span><span><b class="mono">${String(Math.round(res.hdg)).padStart(3, "0")}°</b> ${P.hdg === null ? (P.bestAll ? "the best on this face, of every set-up" : "the best for this set-up") : "yours"}</span>
       <span class="meta">Set-up</span><span>off the face looking to <span class="mono">${String(Math.round(res.normal)).padStart(3, "0")}°</span>${P.face < 0 ? " (AUTO: the best face)" : ""}, ${setupName()}, ${res.mid.toFixed(1)} m off</span>
       ${res.mid > +P.standoff + 0.05 ? `<span class="meta">Moved out</span><span>the heading turned: the ${P.side === 0 ? "stern" : P.side === 3 ? "bow" : "middle of the side"} <b>${res.mid.toFixed(1)} m</b> off, the nearest corner ${res.clear.toFixed(1)} m (corners ≥ ${P.mincl} m)</span>` : ""}
       <span class="meta">Wind in the lee</span><span>${res.wleft < 0.999 ? `${Math.round(res.wleft * 100)} % of it reaches her` : "none: all of the wind"}</span>
       <span class="meta">Weather's push</span><span><span class="mono">${(res.fmag / 9.80665).toFixed(1)} t</span> towards ${String(Math.round(res.fto)).padStart(3, "0")}°: ${res.blow >= 0 ? "blow-off" : "<b>blow-on</b>"} ${(Math.abs(res.blow) / 9.80665).toFixed(1)} t</span>
       <span class="meta">Wind limit</span><span><b class="mono">${limw >= 64 ? "> 64" : limw.toFixed(0)} kn</b> <span class="meta">from ${Math.round(P.wfrom)}°, with its sea (DNV Table 2-1), inside the limits, at this heading</span></span>
      </div>
      <table style="margin-top:8px"><tr><th>Thruster</th><th>Type</th><th class="num">Group</th><th class="num">Full t</th><th class="num">Limit</th><th style="width:30%">Intact</th><th class="num">Force</th><th class="num">Group ${res.fgrp} lost</th></tr>
       ${V.thr.map((t, i) => { const u = res.use[i] || 0, l = t.lim || 1, c = u > l * 1.001 ? "#c0392b" : u > l * 0.8 ? "#e5a02a" : "#2e9e5b";
         return `<tr><td>${esc(t.name)}</td><td>${esc(t.type)}</td><td class="num">${t.grp}</td><td class="num mono">${(t.kn / 9.80665).toFixed(1)}</td><td class="num mono">${Math.round(l * 100)} %</td>
         <td><div style="position:relative;height:12px;background:#e9ebec;border-radius:3px"><div style="width:${Math.min(100, u * 100)}%;height:12px;background:${c};border-radius:3px"></div>
           ${l < 1 ? `<div style="position:absolute;left:${l * 100}%;top:-3px;width:2px;height:18px;background:#1b2129"></div>` : ""}</div>
           <span class="mono" style="font-size:12px">${Math.round(u * 100)} %</span></td>
         <td class="num mono">${(Math.hypot(res.f[i][0], res.f[i][1]) / 9.80665).toFixed(1)} t</td>
         <td class="num mono">${t.grp === res.fgrp ? "lost" : `${Math.round((res.fuse_t[i] || 0) * 100)} % · ${(Math.hypot(res.ff[i][0], res.ff[i][1]) / 9.80665).toFixed(1)} t`}</td></tr>`; }).join("")}
       <tr><td colspan="5"><b>The weather's push</b> <span class="meta">(DNV x 1.25)</span></td><td colspan="2" class="mono"><b>${(res.fmag / 9.80665).toFixed(1)} t</b> towards ${String(Math.round(res.fto)).padStart(3, "0")}°</td></tr></table>
      <div class="meta">The four buttons: each set-up's best on ${P.face < 0 ? "the best face" : "this face"} (a click takes it). The ring: the answer at each heading of the bow on this face (green holds, and after a failure; yellow holds;
       orange only beyond the limits; red no hold). The thrusters: their load a disc (green, amber near the limit, red over it), their force an arrow in tonnes; the red arrow: the weather's whole push. The shaded area: the platform's lee
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
        ${objs.length ? `<select id="dp_obj"><option value="">an object…</option>${objs.map((o, i) => `<option value="${i}">${esc(o.name || o.type)}${o.orient !== null && o.orient !== undefined ? ` (${o.a === o.b ? o.a : o.a + "×" + o.b} m, ${String(Math.round(o.orient)).padStart(3, "0")}°)` : ""}</option>`).join("")}</select>` : ""}</div>
       <div class="controls"><label>Shape <select id="dp_shape"><option value="square">square</option><option value="rect">rectangle</option></select></label>
        <label>${P.shape === "square" ? "Side" : "Length"} ${num("len", P.len)} m</label>${P.shape === "square" ? "" : `<label>Width ${num("wid", P.wid)} m</label>`}
        <label>Face 0 looks to ${num("orient", P.orient, 5)}°</label></div>
       <div class="controls"><label>Height ${num("height", P.height)} m</label>
        <label title="a gravity base: the waves sheltered too; a jacket lets them through"><input type="checkbox" id="dp_solid" ${P.solid ? "checked" : ""}> solid (waves sheltered)</label>
        <label>Lee: ${num("smin", P.smin, 5, 60)} % of the wind close in, all of it at ${num("slen", P.slen, 0.5, 60)} heights</label></div>
       <div class="controls"><label>Working face <select id="dp_face"><option value="-1">AUTO (the best)</option>
         ${[0, 1, 2, 3].map(f => `<option value="${f}">${f}: looks to ${String(Math.round((+P.orient + 90 * f) % 360)).padStart(3, "0")}°</option>`).join("")}</select></label>
        <label>The vessel <select id="dp_side"><option value="1:1">starboard side to, parallel</option><option value="2:1">port side to, parallel</option>
          <option value="1:0">starboard side to, ± the tolerance</option><option value="2:0">port side to, ± the tolerance</option>
          <option value="0:0">stern to</option><option value="3:0">bow to</option></select></label></div>
       <div class="controls"><label title="the middle of the side (or the stern, the bow) this far off the face">Standoff ${num("standoff", P.standoff)} m</label>
        <label title="a turned heading: no corner closer to the face than this">corners ≥ ${num("mincl", P.mincl, 1)} m</label><label>Heading ± ${num("tol", P.tol, 5)}° of square</label></div></div>
      <div class="card"><h2>Vessel <span class="meta">${res ? esc(N().str(Module._api_dp_vname())) : ""}</span></h2>
       <div class="controls"><label>LOA ${num("loa", P.loa)} m</label><label>Working limits <input id="dp_lims" value="${esc(P.lims)}" style="width:150px" title="TYPE % or NAME %: TUN 30, BT3 40, AZ 80"></label></div>
       <details><summary>The vessel file (DPCAP47 .DPV) ${VES ? "· yours" : "· the example: 95 m, three bow tunnels, two azimuths (made up)"}</summary>
        <textarea id="dp_ves" spellcheck="false" style="width:100%;height:220px;font:12px ui-monospace,monospace">${esc(VES || N().str(Module._api_dp_example_vessel()))}</textarea>
        <div class="controls"><button class="small" id="dp_vapply">Apply</button><label>Load <input type="file" id="dp_vfile" accept=".dpv,.txt"></label>
         <button class="small" id="dp_vex">The example</button></div></details></div>
      ${res ? (() => { absCompute(); const A = ABS.r; if (!A) return `<div class="card"><h2>ABS SKP</h2><div class="meta">No result (no memory?).</div></div>`;
        const V2 = vessel(), tn = i => esc((V2.thr[i] || {}).name || "?"), rel = ((+P.wfrom - res.hdg) % 360 + 360) % 360;
        return `<div class="card"><h2>ABS SKP <span class="meta">the vessel's station keeping, every direction (ABS DPS Guide, Section 9)</span></h2>
        <div style="display:grid;grid-template-columns:minmax(200px,0.8fr) minmax(290px,1.2fr);gap:10px;align-items:start">
         <canvas id="dp_abscv" style="width:100%;aspect-ratio:1;border-radius:8px"></canvas>
         <div><div class="mono" style="font-size:17px;font-weight:700;background:#1b2129;color:#fff;padding:4px 8px;border-radius:6px;display:inline-block">SKP(${skp(A.a)}, ${skp(A.b)}, ${skp(A.c)}, ${skp(A.d)}, ${A.cur.toFixed(1)}, NS)</div>
          <table style="margin-top:6px;font-size:13px;width:100%">
           <tr><td>a</td><td>intact</td><td class="num">${skp(A.a)} %</td></tr>
           <tr><td>b</td><td>least effective thruster lost (${tn(A.bt)})</td><td class="num">${skp(A.b)} %</td></tr>
           <tr><td>c</td><td>most effective thruster lost (${tn(A.ct)})</td><td class="num">${skp(A.c)} %</td></tr>
           <tr><td>d</td><td>${A.d >= 0 ? `worst failure: group ${A.dg} lost` : "worst failure: no groups"}</td><td class="num">${skp(A.d)} %</td></tr>
           <tr><td>e</td><td>current</td><td class="num">${A.cur.toFixed(1)} kn</td></tr>
           <tr><td>f</td><td>the North Sea wind-wave table</td><td class="num">NS</td></tr></table>
          <div class="controls" style="margin-top:6px"><label>Current <select id="dp_acur"><option value="-1">today's (${absCur().toFixed(1)} kn)</option>
            ${[0.5, 1, 1.5, 2, 2.5, 3].map(c => `<option value="${c}">${c.toFixed(1)} kn</option>`).join("")}</select></label>
           <label>Margin <select id="dp_amargin">${[0, 10, 20, 30].map(m => `<option value="${m}">${m} %</option>`).join("")}</select></label></div>
          <div style="margin-top:6px;font-size:13px"><span style="color:#1f5fa8;font-weight:700">━━</span> intact &nbsp; <span style="color:#d63a2a;font-weight:700">╍╍</span> after the worst failure &nbsp; <span style="color:#2e8b57;font-weight:700">●━</span> today's wind</div>
          <div style="margin-top:4px;font-size:13px">Today ${String(Math.round(+P.wfrom)).padStart(3, "0")}° ${Math.round(+P.wkn)} kn, heading ${String(Math.round(res.hdg)).padStart(3, "0")}°: from ${String(Math.round(rel)).padStart(3, "0")}° off the bow
           she holds to <b>${absAt(A.intact, rel).toFixed(0)}</b> m/s intact, <b>${absAt(A.worst, rel).toFixed(0)}</b> m/s after the worst failure.</div>
          <div class="meta" style="margin-top:4px">The ABS limits are 1-minute mean winds (m/s); today's wind is a 10-minute mean (kn). SKP: the % of the year
           (the North Sea table, Table 1) the vessel holds station, with ${P.amargin} % margin on the weather. (${Math.round(ABS.ms)} ms)</div></div></div></div>`; })() : ""}
      <div class="card"><h2>To the calculator</h2>
       <div class="controls"><button id="dp_tocalc">Send to the Calculator tab</button><button id="dp_savep">Save DPOPS.TXT</button><button id="dp_savev">Save VESSEL.DPV</button></div>
       <div class="meta">Copy both to /NAV47/ on the DM42n; DP OPS (menu 3, DP) reads them (LOAD reads them again). The wind, the current and
        the sea are the calculator's own (SET, FLOW, INPUT).</div>
       <details><summary>DPOPS.TXT</summary><pre class="mono" style="font-size:12px">${esc(platText())}</pre></details></div>
     </div></div>
     <div class="meta" style="margin:10px 2px">DPCAP47's engine: DNV-ST-0111 level 1's wind, current and wave-drift forces (x 1.25), each from its own
      direction, its thrust allocation with the thrusters' working limits in the intact case and full thrust after a
      redundancy group is lost. Static: no position excursions, no thruster wash on the platform, no current shelter. Planning, not operations: check against the vessel's DP FMEA, ASOG and capability plots.</div>`;
    el.querySelectorAll("details").forEach((d, i) => { if (open[i]) d.open = true; });
    if (res) picture(document.getElementById("dpcv"), V);
    const acv = document.getElementById("dp_abscv");
    if (acv && ABS.r) absPolar(acv, ABS.r, ((+P.wfrom - res.hdg) % 360 + 360) % 360);
    bind();
  }
  function bind() {
    const $ = id => document.getElementById("dp_" + id), on = (id, f) => { const e = $(id); if (e) e.onchange = f; };
    const numf = ["wfrom", "wkn", "cset", "ckn", "vfrom", "hs", "tp", "len", "wid", "orient", "height", "smin", "slen", "standoff", "mincl", "tol", "loa"];
    numf.forEach(k => on(k, e => { const v = parseFloat(e.target.value); if (isFinite(v)) { P[k] = v; save(); render(true); } }));
    /* the weather: the best heading again while you type (a pause of 0.6 s), not only at Enter; the field keeps the cursor */
    ["wfrom", "wkn", "cset", "ckn", "vfrom", "hs", "tp"].forEach(k => { const e = $(k); if (e) e.oninput = () => {
      clearTimeout(typing);
      typing = setTimeout(() => {
        const i = $(k), v = parseFloat(i ? i.value : ""); if (!isFinite(v) || v === +P[k]) return;
        const at = i.selectionStart; P[k] = v; save(); render(true);
        const j = $(k); if (j) { j.focus(); try { j.setSelectionRange(at, at); } catch (x) { /* number fields: no caret */ } }
      }, 600);
    }; });
    on("name", e => { P.name = e.target.value.trim() || "PLATFORM"; save(); render(); });
    on("pos", e => { const p = APP.parsePos(e.target.value); if (p) { P.lat = p[0]; P.lon = p[1]; save(); } render(true); });
    $("dr").onclick = () => { P.lat = null; P.lon = null; save(); render(true); };
    on("obj", e => { const o = (S().objects || []).filter(o => /platform|rig|wellhead|spm/.test(o.type))[+e.target.value];
      if (o) {
        P.lat = o.lat; P.lon = o.lon; P.name = (o.name || o.type).toUpperCase().slice(0, 23);
        if (o.orient !== null && o.orient !== undefined) {          /* its orientation and size: face 0's length across, the other side along */
          P.orient = o.orient; P.wid = o.a; P.len = o.b; P.shape = o.a === o.b ? "square" : "rect";
        }
        P.hdg = null; save();
      }
      render(true); });
    $("shape").value = P.shape; on("shape", e => { P.shape = e.target.value; save(); render(true); });
    $("face").value = String(P.face); on("face", e => { P.face = +e.target.value; P.hdg = null; save(); render(true); });
    $("side").value = `${P.side}:${(P.side === 1 || P.side === 2) && P.par ? 1 : 0}`;
    on("side", e => { const [a, b] = e.target.value.split(":"); P.side = +a; P.par = b === "1"; P.hdg = null; P.bestAll = false; save(); render(true); });
    document.querySelectorAll(".dpq").forEach(b => b.onclick = () => { const q = quick[+b.dataset.q]; P.side = q.sd; P.par = q.par; P.hdg = null; P.bestAll = false; save(); render(true); });
    on("solid", e => { P.solid = e.target.checked; save(); render(true); });
    on("cauto", e => { P.cauto = e.target.checked; save(); render(true); });
    on("sauto", e => { P.sauto = e.target.checked; save(); render(true); });
    on("lims", e => { P.lims = e.target.value; save(); render(true); });
    $("best").onclick = () => { P.hdg = null; P.bestAll = true; save(); render(true); };   /* every set-up on the face */
    on("hdg", e => { P.hdg = ((parseFloat(e.target.value) || 0) % 360 + 360) % 360; P.bestAll = false; save(); render(); });
    $("hm").onclick = () => { P.hdg = ((res ? res.hdg : 0) + 355) % 360; P.bestAll = false; save(); render(); };
    $("hp").onclick = () => { P.hdg = ((res ? res.hdg : 0) + 5) % 360; P.bestAll = false; save(); render(); };
    $("vapply").onclick = () => { VES = $("ves").value; localStorage.setItem(VKEY, VES); render(true); };
    $("vex").onclick = () => { VES = ""; localStorage.removeItem(VKEY); render(true); };
    on("vfile", e => { const f = e.target.files[0]; if (f) f.text().then(t => { VES = t.replace(/\r/g, ""); localStorage.setItem(VKEY, VES); render(true); }); });
    if ($("acur")) { $("acur").value = String(P.acur); on("acur", e => { P.acur = +e.target.value; save(); render(); }); }
    if ($("amargin")) { $("amargin").value = String(P.amargin); on("amargin", e => { P.amargin = +e.target.value; save(); render(); }); }
    $("savep").onclick = () => APP.download("DPOPS.TXT", platText());
    $("savev").onclick = () => APP.download("VESSEL.DPV", VES || N().str(Module._api_dp_example_vessel()));
    $("tocalc").onclick = () => {
      localStorage.setItem("nav47fs:/simdisk/NAV47/VESSEL.DPV", VES || N().str(Module._api_dp_example_vessel()));
      CALC.putAndRestart("DPOPS.TXT", platText());
    };
  }
  return { render };
})();
/* the page's clock redraws the open tab every 5 s: not while you are typing in this one (it would wipe what you type) */
if (typeof TABS2 !== "undefined") TABS2.dp = () => {
  const a = document.activeElement, el = document.getElementById("t-dp");
  if (el && a && el.contains(a) && /INPUT|TEXTAREA|SELECT/.test(a.tagName)) return;
  DPTAB.render();
};
