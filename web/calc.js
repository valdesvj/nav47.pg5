/* calc.js - NAV47 on the web: the Calculator tab, the DM42n program itself (nav47.js), its keys, its LCD, its disk
 * (/simdisk in memory, kept in localStorage). It starts when the tab is first opened (or with #keys=...). */
"use strict";
const CALC = (() => {
  const K = { ENTER: 13, SWAP: 14, CHS: 15, E: 16, BSP: 17, UP: 18, N7: 19, N8: 20, N9: 21, DIV: 22, DOWN: 23, N4: 24, N5: 25,
              N6: 26, MUL: 27, SHIFT: 28, N1: 29, N2: 30, N3: 31, SUB: 32, EXIT: 33, N0: 34, DOT: 35, RUN: 36, ADD: 37,
              F1: 38, F2: 39, F3: 40, F4: 41, F5: 42, F6: 43 };
  const PREFIX = "nav47fs:";
  const ROUTES = {
    jfk:  "NAV47 ROUTE 1\nGC\nLIM 60.0\nSPLIT 0 0.000\nELL 0\nSPEED 480.0\n40.6400000 -73.7800000\n51.4700000 -0.4600000\n",
    cape: "NAV47 ROUTE 1\nCS\nLIM 45.0\nSPLIT 3 10.000\nELL 0\nSPEED 12.0\n-33.8000000 18.3000000\n-34.4000000 18.2000000\n-34.7000000 18.7000000\n-35.1000000 20.0000000\n-39.0000000 143.5000000\n-38.3300000 144.6500000\n",
    pac:  "NAV47 ROUTE 1\nGC\nLIM 60.0\nSPLIT 1 500.000\nELL 0\nSPEED 480.0\n35.6000000 139.8000000\n37.8000000 -122.4000000\n",
    none: "NAV47 ROUTE 1\nGC\nLIM 60.0\nSPLIT 0 0.000\nELL 0\n",
  };
  let started = false, shiftOn = false, shiftBtn;

  /* the keys */
  function key(label, code, cls, parent) {
    const b = document.createElement("button");
    b.className = "k " + (cls || ""); b.textContent = label; b.title = label;
    b.addEventListener("pointerdown", e => { e.preventDefault(); press(code); });
    parent.appendChild(b); return b;
  }
  function build() {
    const fk = document.getElementById("fkeys"), mk = document.getElementById("keys");
    ["F1", "F2", "F3", "F4", "F5", "F6"].forEach(f => key("", K[f], "f", fk));
    [["ENTER", K.ENTER, "wide"], ["x≷y", K.SWAP], ["+/−", K.CHS], ["←", K.BSP],
     ["▲", K.UP], ["7", K.N7], ["8", K.N8], ["9", K.N9], ["÷", K.DIV],
     ["▼", K.DOWN], ["4", K.N4], ["5", K.N5], ["6", K.N6], ["×", K.MUL],
     ["shift", K.SHIFT, "shift"], ["1", K.N1], ["2", K.N2], ["3", K.N3], ["−", K.SUB],
     ["EXIT", K.EXIT], ["0", K.N0], ["·", K.DOT], ["R/S", K.RUN], ["+", K.ADD]]
      .forEach(([l, c, cls]) => { const b = key(l, c, cls, mk); if (c === K.SHIFT) shiftBtn = b; });
    const PC = { Enter: K.ENTER, Escape: K.EXIT, Backspace: K.BSP, ArrowUp: K.UP, ArrowDown: K.DOWN, ArrowLeft: K.N4,
                 ArrowRight: K.N6, ".": K.DOT, ",": K.DOT, "-": K.CHS, "+": K.ADD, "*": K.MUL, "/": K.DIV, " ": K.RUN,
                 r: K.RUN, R: K.RUN, s: K.SHIFT, S: K.SHIFT, Shift: K.SHIFT, F1: K.F1, F2: K.F2, F3: K.F3, F4: K.F4, F5: K.F5, F6: K.F6 };
    for (let d = 0; d <= 9; d++) PC[String(d)] = K["N" + d];
    window.addEventListener("keydown", e => {
      if (document.getElementById("t-calc").hidden) return;
      if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName) || e.ctrlKey || e.metaKey || e.altKey) return;
      const c = PC[e.key];
      if (c !== undefined) { e.preventDefault(); press(c); }
    });
    document.getElementById("png").onclick = () => {
      const cv = document.getElementById("lcd"), big = document.createElement("canvas"); big.width = 800; big.height = 480;
      const g = big.getContext("2d"); g.imageSmoothingEnabled = false; g.drawImage(cv, 0, 0, 800, 480);
      const a = document.createElement("a"); a.href = big.toDataURL("image/png"); a.download = "NAV47.png"; a.click();
    };
    document.querySelectorAll("[data-route]").forEach(b => b.onclick = () => putAndRestart("ROUTE.TXT", ROUTES[b.dataset.route]));
    document.getElementById("loadroute").onchange = e => {
      const f = e.target.files[0]; if (f) f.text().then(t => putAndRestart("ROUTE.TXT", t.replace(/\r/g, "")));
    };
    document.getElementById("reset").onclick = () => {
      if (!confirm("Forget the calculator's route, settings and files in this browser?")) return;
      Object.keys(localStorage).filter(k => k.startsWith(PREFIX)).forEach(k => localStorage.removeItem(k)); location.reload();
    };
  }
  function press(code) {
    if (!started || !window.Module || !Module.ready) return;
    Module._nav47_key(code);
    shiftOn = code === K.SHIFT ? !shiftOn : false;
    if (shiftBtn) shiftBtn.classList.toggle("on", shiftOn);
  }

  /* the LCD */
  let img, cx;
  const BG = [223, 230, 214], INK = [30, 34, 32];
  function lcd(fb) {
    if (!cx) { const cv = document.getElementById("lcd"); cx = cv.getContext("2d"); img = cx.createImageData(400, 240); }
    const d = img.data;
    for (let i = 0; i < 96000; i++) {
      const c = fb[i] ? INK : BG, o = i * 4;
      d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
    }
    cx.putImageData(img, 0, 0);
  }

  /* the disk */
  function walk(FS, dir, out) {
    for (const n of FS.readdir(dir)) {
      if (n === "." || n === "..") continue;
      const p = dir + "/" + n, st = FS.stat(p);
      if (FS.isDir(st.mode)) walk(FS, p, out); else out.push(p);
    }
    return out;
  }
  function saveAll() {
    const FS = Module.FS;
    for (const p of walk(FS, "/simdisk", [])) {
      if (p.startsWith("/simdisk/NAV47/TIDES/")) continue;  /* your tide files: kept once, by the page */
      try { localStorage.setItem(PREFIX + p, FS.readFile(p, { encoding: "utf8" })); } catch (e) { /* full */ }
    }
    listFiles();
  }
  function restore() {
    const FS = Module.FS;
    FS.mkdirTree("/simdisk/NAV47/ROUTES");
    for (let i = 0; i < localStorage.length; i++) {
      const k = localStorage.key(i);
      if (!k.startsWith(PREFIX)) continue;
      const p = k.slice(PREFIX.length);
      FS.mkdirTree(p.slice(0, p.lastIndexOf("/")));
      FS.writeFile(p, localStorage.getItem(k));
    }
    if (!localStorage.getItem(PREFIX + "/simdisk/NAV47/NAV47.CFG")) {
      const tz = -new Date().getTimezoneOffset() / 60;
      FS.writeFile("/simdisk/NAV47/NAV47.CFG", "NAV47 CFG 1\nTZ " + tz.toFixed(4) + "\n");
    }
    if (!localStorage.getItem(PREFIX + "/simdisk/NAV47/ROUTE.TXT")) FS.writeFile("/simdisk/NAV47/ROUTE.TXT", ROUTES.cape);
    /* your tide files (the page's Tide / Flow tabs): on the calculator's disk, and in its NAV47.CFG (4 at most) */
    const TK = "nav47web:tidefile:", mine = Object.keys(localStorage).filter(k => k.startsWith(TK)).sort();
    FS.mkdirTree("/simdisk/NAV47/TIDES");
    for (const k of mine) FS.writeFile("/simdisk/NAV47/TIDES/" + k.slice(TK.length), localStorage.getItem(k));
    let cfg = FS.readFile("/simdisk/NAV47/NAV47.CFG", { encoding: "utf8" }).split("\n").filter(l => l && !l.startsWith("FILE /NAV47/TIDES/"));
    mine.slice(0, 4).forEach(k => cfg.push("FILE /NAV47/TIDES/" + k.slice(TK.length)));
    FS.writeFile("/simdisk/NAV47/NAV47.CFG", cfg.join("\n") + "\n");
    listFiles();
    if (/keys=/.test(location.hash) || /tab=calc/.test(location.hash)) start();
  }
  function listFiles() {
    const t = document.getElementById("files"); t.innerHTML = "";
    for (const p of walk(Module.FS, "/simdisk", []).sort()) {
      const tr = t.insertRow(), data = Module.FS.readFile(p, { encoding: "utf8" });
      tr.insertCell().textContent = p.replace("/simdisk", "");
      const r = tr.insertCell(); r.className = "num"; r.textContent = data.length + " B ";
      const a = document.createElement("a"); a.textContent = "download"; a.href = "#";
      a.onclick = e => { e.preventDefault(); APP.download(p.split("/").pop(), data); };
      r.appendChild(a);
    }
  }
  function putAndRestart(path, text) {
    localStorage.setItem(PREFIX + "/simdisk/NAV47/" + path, text);
    location.hash = "tab=calc"; location.reload();
  }

  /* the program: started once */
  function start() {
    if (started || !Module.ready) return;
    started = true;
    document.getElementById("calcstatus").textContent = "NAV47 running · shift EXIT ends it (reload to start again)";
    Module.callMain([]);
    const m = location.hash.match(/keys=([^&]*)/);
    if (m) {
      const ks = decodeURIComponent(m[1]).split(",").filter(Boolean).map(s => K[s] !== undefined ? K[s] : K["N" + s]);
      ks.forEach((c, i) => setTimeout(() => c !== undefined && press(c), 400 + 250 * i));
    }
  }
  build();
  return { lcd, saveAll, restore, start, putAndRestart, K };
})();
