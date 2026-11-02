######################################
# NAV47 - DMCP5 program for the SwissMicros DM42n
# Almanac 47 + Zones 47 + Tide 47 in one native program.
#
#   make                          public build (NOAA + TICON-3 stations)
#   make TIDE47=~/opt/tide47      + YOUR private data from that Tide 47 checkout, compiled in:
#                                 python/stations_private.py, streams_private.py, flow_private.py
#                                 (written to build/gen/, never to src/: keep the .pg5 to yourself)
#   make test                     PC tests: C engines against the Python twins (needs the 47 repos)
######################################
TARGET = NAV47
BUILD_DIR = build
PYTHON ?= python3
TIDE47 ?=
ALMANAC47 ?= ../almanac47
ZONES47 ?= ../zones47
TIDE47_TEST ?= $(if $(TIDE47),$(TIDE47),../tide47)

C_INCLUDES += -Idmcp -Isrc
C_SOURCES  += dmcp/sys/pgm_syscalls.c
ASM_SOURCES = dmcp/startup_pgm.s
C_SOURCES += src/main.c src/astro.c src/almdata.c src/gfx.c src/fonts.c src/zones.c src/zonedata.c \
             src/c47scr.c src/views.c src/fonts21.c src/ntext.c src/moon47.c src/route.c \
             src/tide.c src/tidecons.c src/tidedata_pub.c src/tidefile.c $(BUILD_DIR)/gen/tidedata_priv.c

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

C_DEFS  += -D__weak="__attribute__((weak))" -D__packed="__attribute__((__packed__))"
CPUFLAGS += -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16

CFLAGS  = -std=gnu11 $(CPUFLAGS) $(C_DEFS) $(C_INCLUDES) -Wall -fdata-sections -ffunction-sections
CFLAGS += -Wno-misleading-indentation -ffp-contract=off
ifeq ($(DEBUG), 1)
CFLAGS += -O0 -g -DDEBUG
else
CFLAGS += -O2 -g
endif
CFLAGS += -MD -MP -MF .dep/$(@F).d

LDSCRIPT = stm32_program.ld
LDFLAGS  = $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--wrap=_malloc_r -g -lm

all: $(BUILD_DIR)/$(TARGET).pg5

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# the private data: regenerated every build from TIDE47 (empty without it)
$(BUILD_DIR)/gen/tidedata_priv.c: FORCE
	@$(PYTHON) tools/gen_tides.py private "$(if $(TIDE47),$(TIDE47),-)" $@.new >/dev/null && \
	  (cmp -s $@.new $@ || mv $@.new $@) && rm -f $@.new && \
	  echo "private data: $(if $(TIDE47),from $(TIDE47),none (public build))"
FORCE:

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).pg5: $(BUILD_DIR)/$(TARGET).elf tools/pg5tool.py
	$(OBJCOPY) --remove-section .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_qspi.bin
	@if ! $(PYTHON) tools/pg5tool.py qspi $(BUILD_DIR)/$(TARGET)_qspi.bin src/qspi_crc.h; then \
	   rm -f $(BUILD_DIR)/pgm_syscalls.o $<; $(MAKE) --no-print-directory $@; exit $$?; fi
	$(PYTHON) tools/pg5tool.py pg5 $(BUILD_DIR)/$(TARGET)_flash.bin $@
	$(SIZE) $<

$(BUILD_DIR):
	mkdir -p $@ $@/gen

# regenerate the data from the 47 repositories (committed in src/)
data:
	$(PYTHON) tools/gen_almanac.py $(ALMANAC47)
	$(PYTHON) tools/gen_fonts.py $(ALMANAC47)
	$(PYTHON) tools/gen_fonts21.py $(ALMANAC47)
	$(PYTHON) tools/gen_zones.py $(ZONES47)
	$(PYTHON) tools/gen_tides.py cons $(TIDE47_TEST)
	$(PYTHON) tools/gen_tides.py public $(TIDE47_TEST)

test:
	$(PYTHON) test/test_astro.py $(ALMANAC47)
	$(PYTHON) test/test_moon47.py $(ALMANAC47)
	$(PYTHON) test/test_zones.py $(ZONES47)
	$(PYTHON) test/test_tide.py $(TIDE47_TEST)

SIM_SRC = test/sim/sim.c src/main.c src/astro.c src/almdata.c src/gfx.c src/fonts.c src/zones.c src/zonedata.c \
          src/c47scr.c src/views.c src/fonts21.c src/ntext.c src/moon47.c src/route.c \
          src/tide.c src/tidecons.c src/tidedata_pub.c src/tidefile.c $(BUILD_DIR)/gen/tidedata_priv.c
$(BUILD_DIR)/gen/simfont.h: test/sim/gen_simfont.py
	@mkdir -p $(BUILD_DIR)/gen
	$(PYTHON) test/sim/gen_simfont.py $@

sim: $(SIM_SRC) $(BUILD_DIR)/gen/simfont.h | $(BUILD_DIR)
	gcc -std=gnu11 -O2 -w -ffp-contract=off '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' \
	    -Itest/sim -Isrc -I$(BUILD_DIR)/gen -o $(BUILD_DIR)/sim $(SIM_SRC) -lm

clean:
	-rm -fR .dep $(BUILD_DIR)

-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)

.PHONY: clean all test data sim FORCE
