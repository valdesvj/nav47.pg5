# geo47: geodesic / rhumb / route module for NAV47
GEO47_DIR = src/geo47
GEO47_SRC = $(GEO47_DIR)/geodesic.c $(GEO47_DIR)/geo_rhumb.c \
            $(GEO47_DIR)/geo_route.c $(GEO47_DIR)/geo_ellipsoids.c
C_SOURCES  += $(GEO47_SRC) src/geo_nav.c
C_INCLUDES += -I$(GEO47_DIR)

# host test (one-line rule, no tab needed)
test-geo: ; mkdir -p build/host && gcc -std=c99 -O2 -D_DEFAULT_SOURCE -I$(GEO47_DIR) $(GEO47_SRC) test/geo47/test_route.c -lm -o build/host/test_route && build/host/test_route > build/host/geo_out.txt && python3 test/geo47/verify.py build/host/geo_out.txt
.PHONY: test-geo
