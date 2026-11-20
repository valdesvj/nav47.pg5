# geo47: geodesic / rhumb / route module for NAV47
GEO47_DIR = src/geo47
GEO47_SRC = $(GEO47_DIR)/geodesic.c $(GEO47_DIR)/geo_rhumb.c \
            $(GEO47_DIR)/geo_route.c $(GEO47_DIR)/geo_ellipsoids.c
C_SOURCES  += $(GEO47_SRC) src/geo_nav.c src/map47.c
C_INCLUDES += -I$(GEO47_DIR)

# host test (one-line rule, no tab needed)
test-geo: ; mkdir -p build/host && gcc -std=c99 -O2 -D_DEFAULT_SOURCE -I$(GEO47_DIR) $(GEO47_SRC) test/geo47/test_route.c -lm -o build/host/test_route && build/host/test_route > build/host/geo_out.txt && python3 test/geo47/verify.py build/host/geo_out.txt
.PHONY: test-geo

# CHART coastlines: Natural Earth (public domain) 110 m and 50 m land into build/coast/COAST_C.BIN and COAST_L.BIN;
# copy both to /NAV47/ on the calculator
NE_URL = https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson
build/coast/ne/ne_%_land.geojson: ; mkdir -p build/coast/ne && curl -sfL -o $@ $(NE_URL)/ne_$*_land.geojson
build/coast/COAST_C.BIN: build/coast/ne/ne_110m_land.geojson tools/prep_coast.py ; python3 -I tools/prep_coast.py $< $@ --tol 0.05
build/coast/COAST_L.BIN: build/coast/ne/ne_50m_land.geojson tools/prep_coast.py ; python3 -I tools/prep_coast.py $< $@ --tol 0.02
coast: build/coast/COAST_C.BIN build/coast/COAST_L.BIN
.PHONY: coast
