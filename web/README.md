# NAV47 on the web

`web/index.html` runs the DM42n program itself in the browser: `src/` and the simulator layer (`test/sim/sim.c`,
`NAV47_WEB`) compiled to WebAssembly by Emscripten, every page as on the calculator (the system font is the
simulator's stand-in). Click the keys or use the keyboard (F1–F6, digits, Enter, Esc = EXIT, arrows, Shift or s,
r or Space = R/S).

```bash
make coast      # build/coast/COAST_C.BIN, COAST_L.BIN (Natural Earth, public domain)
make web        # web/nav47.js: one file, the wasm and the coastlines inside it (needs emscripten)
google-chrome web/index.html          # works as a file, no server needed
```

- The calculator's disk (`/NAV47/NAV47.CFG`, `ROUTE.TXT`, `ROUTES/PLAN.CSV`, `PLAN.NMEA`) is kept in the browser's
  localStorage and listed on the page, with downloads. Example routes and *Load a ROUTE.TXT* restart the program.
- The clock is the computer's; on the first start CLOCK TZ is set from it. *Use this device's position* writes LAT / LON.
- `index.html#keys=F6,F4,RUN,F5,F4` presses keys after the start (demos, tests).
- Never private data: `make web` makes the private tide table empty, whatever `TIDE47` says.
