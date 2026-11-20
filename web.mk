# web: NAV47 in the browser (web/index.html). The simulator layer (test/sim/sim.c, NAV47_WEB) and the real src/
# compiled to WebAssembly by Emscripten, in one file web/nav47.js (the wasm and the coastlines inside it), so the page
# also works opened as a file. Never private data: the private tide table is made empty here, whatever TIDE47 says.
EMCC ?= $(firstword $(wildcard /usr/lib/emscripten/emcc) emcc)
WEB_OUT = web/nav47.js
WEB_SRC = $(filter-out %tidedata_priv.c,$(SIM_SRC)) build/web/tidedata_priv.c web/api.c
WEB_API = $(shell grep -o 'api_[a-z0-9_]*' web/api.c | sort -u | sed 's/^/_/' | paste -sd, -)
WEB_FLAGS = -O2 -std=gnu11 -w -ffp-contract=off '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' \
            -DNAV47_SIM -DNAV47_WEB $(SIM_DEFS) -Itest/sim -Isrc -I$(GEO47_DIR) -Isrc/usb -I$(BUILD_DIR)/gen \
            -sASYNCIFY -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sINVOKE_RUN=0 -sSINGLE_FILE=1 \
            -sENVIRONMENT=web -sEXPORTED_FUNCTIONS=_main,_nav47_key,$(WEB_API) -sEXPORTED_RUNTIME_METHODS=callMain,FS,ccall,cwrap,UTF8ToString,HEAPF64 \
            --embed-file build/coast/COAST_C.BIN@/coast/COAST_C.BIN --embed-file build/coast/COAST_L.BIN@/coast/COAST_L.BIN

web: $(WEB_OUT)
$(WEB_OUT): web/api.c $(wildcard src/*.h src/geo47/*.h) $(filter-out %tidedata_priv.c,$(SIM_SRC)) $(BUILD_DIR)/gen/simfont.h build/coast/COAST_C.BIN build/coast/COAST_L.BIN web.mk
	@mkdir -p build/web build/emcache
	$(PYTHON) tools/gen_tides.py private - build/web/tidedata_priv.c >/dev/null
	EM_CACHE=$(CURDIR)/build/emcache $(EMCC) $(WEB_FLAGS) -o $@ $(WEB_SRC)
.PHONY: web
