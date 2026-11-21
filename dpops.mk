# DP OPS: DPCAP47's engine (src/dp/dpcap.c, a copy: make dpsync DPCAP47=~/opt/dpcap47.pg5) and src/dpops.c
DPCAP47 ?= $(HOME)/opt/dpcap47.pg5
DPOPS_SRC = src/dp/dpcap.c src/dpops.c
C_SOURCES += $(DPOPS_SRC)

dpsync: ; cp $(DPCAP47)/src/dpcap.c $(DPCAP47)/src/dpcap.h src/dp/ && git -C $(DPCAP47) log --oneline -1 > src/dp/FROM.txt && cat src/dp/FROM.txt
test-dp: ; mkdir -p build/host && gcc -std=c99 -O2 -Wall -Isrc $(DPOPS_SRC) test/dpops/test_dpops.c -lm -o build/host/test_dpops && build/host/test_dpops
.PHONY: dpsync test-dp
