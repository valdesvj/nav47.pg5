# web: NAV47 in the browser (web/index.html). The simulator layer (test/sim/sim.c, NAV47_WEB) and the real src/
# compiled to WebAssembly by Emscripten, in one file web/nav47.js (the wasm and the coastlines inside it), so the page
# also works opened as a file. Never private data: the private tide table is made empty here, whatever TIDE47 says.
EMCC ?= $(firstword $(wildcard /usr/lib/emscripten/emcc) emcc)
WEB_OUT = web/nav47.js
WEB_SRC = $(filter-out %tidedata_priv.c,$(SIM_SRC)) build/web/tidedata_priv.c web/api.c
WEB_API = $(shell grep -o 'api_[a-z0-9_]*' web/api.c | sort -u | sed 's/^/_/' | paste -sd, -)
WEB_NOAA = $(sort $(wildcard build/noaa/N*.TID))
WEB_TICON = $(sort $(wildcard build/ticon/T*.TID))
WEB_FLAGS = -O2 -std=gnu11 -w -ffp-contract=off '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' \
            -DNAV47_SIM -DNAV47_WEB $(SIM_DEFS) -Itest/sim -Isrc -I$(GEO47_DIR) -Isrc/usb -I$(BUILD_DIR)/gen \
            -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sINVOKE_RUN=0 -sSINGLE_FILE=1 \
            -sENVIRONMENT=web -sEXPORTED_FUNCTIONS=_main,_nav47_key,_malloc,_free,$(WEB_API) -sEXPORTED_RUNTIME_METHODS=callMain,FS,ccall,cwrap,UTF8ToString,HEAPF64,stringToUTF8,lengthBytesUTF8 \
            --embed-file build/coast/COAST_C.BIN@/coast/COAST_C.BIN --embed-file build/coast/COAST_L.BIN@/coast/COAST_L.BIN \
            $(foreach f,$(WEB_NOAA),--embed-file $(f)@/noaa/$(notdir $(f))) $(foreach f,$(WEB_TICON),--embed-file $(f)@/ticon/$(notdir $(f)))

web: $(WEB_OUT)
$(WEB_OUT): web/api.c $(wildcard src/*.h src/geo47/*.h) $(filter-out %tidedata_priv.c,$(SIM_SRC)) $(BUILD_DIR)/gen/simfont.h build/coast/COAST_C.BIN build/coast/COAST_L.BIN $(WEB_NOAA) $(WEB_TICON) web.mk
	@test -n "$(WEB_NOAA)" || { echo "make web: no build/noaa/N*.TID - run make noaa first (NOAA's current stations)"; exit 1; }
	@mkdir -p build/web build/emcache
	$(PYTHON) tools/gen_tides.py private - build/web/tidedata_priv.c >/dev/null
	EM_CACHE=$(CURDIR)/build/emcache $(EMCC) $(WEB_FLAGS) -o $@ $(WEB_SRC)
.PHONY: web

# NOAA's tidal current stations (public domain) as .TID files: build/noaa/N*.TID (embedded by make web; copy them to
# /NAV47/TIDES/ for the calculator). Fetched once from NOAA's API (build/noaa/cache/); needs a Tide 47 checkout.
noaa: ; $(PYTHON) -I tools/noaa_currents.py $(or $(TIDE47),$(TIDE47_TEST),$(HOME)/opt/tide47) build/noaa
# your private tide files for the calculator and the web page: build/tidefiles/*.TID, FLOW.FLW (git-ignored, never
# commit them): make tidefiles TIDE47=~/opt/tide47
tidefiles: ; $(PYTHON) tools/gen_tides.py files $(TIDE47) build/tidefiles --private-only
.PHONY: noaa tidefiles
# TICON-3's tide gauges worldwide (CC BY 4.0) as .TID files: build/ticon/T*.TID (embedded by make web; copy an area to
# /NAV47/TIDES/ for the calculator). TICON-3: TICON_3.zip from doi:10.1594/PANGAEA.951610 (default ~/Downloads);
# the names: Natural Earth's populated places (public domain, downloaded once)
TICON_ZIP ?= $(HOME)/Downloads/TICON_3.zip
NE_PLACES = https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_10m_populated_places_simple.geojson
build/ticon/TICON_3.txt: ; mkdir -p build/ticon && unzip -oq $(TICON_ZIP) TICON_3.txt -d build/ticon
build/ticon/ne_places.geojson: ; mkdir -p build/ticon && curl -sfL -o $@ $(NE_PLACES)
ticon: build/ticon/TICON_3.txt build/ticon/ne_places.geojson
	$(PYTHON) -I tools/ticon_stations.py $(or $(TIDE47),$(TIDE47_TEST),$(HOME)/opt/tide47) build/ticon/TICON_3.txt build/ticon/ne_places.geojson build/ticon
.PHONY: ticon
