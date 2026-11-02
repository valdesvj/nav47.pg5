# NAV47: working rules

- Work on a new branch; Victor creates and merges the PRs. Never force-push or rewrite history.
- Commit author: "Victor Valdes" <valdesvj@users.noreply.github.com>.
- Never commit private data: no Aramco, Admiralty (UKHO) or GEOSA constants, no FLOW atlas, no .TID / .FLW made with
  --private, nothing from build/ (build/gen/tidedata_priv.c is private when made with TIDE47=).
- The engines have Python twins in the other 47 repositories (reference only, never edited from here):
  src/astro.c = almanac47 python/native/c47astro.py, src/tide.c = tide47 python/tide47ref.py,
  src/zones.c = zones47 DST rules. A change to an engine: run make test against the twins.
- src/almdata.c, fonts.c, zonedata.c, tidecons.c, tidedata_pub.c are generated (make data); never edit them by hand.
- DMCP5: 8 KB of static RAM. Big buffers go on the heap (struct Nav, tide.c work()); check .bss in arm-none-eabi-size.
- Check every screen in the simulator (make sim; test/sim/render.py) before committing.
- Ask before anything that can't be undone. Pull requests only for nav47.pg5.
