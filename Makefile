TOOLCHAIN ?= /Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/
CC := $(TOOLCHAIN)arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN)arm-none-eabi-objcopy
NM := $(TOOLCHAIN)arm-none-eabi-nm
SIZE := $(TOOLCHAIN)arm-none-eabi-size
BUILD := build
# make dev: the app installed on your module, so a remote install sends
# only the blocks that changed.
SEED ?=

SW := third_party/starterware
ARCH := -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -marm
INCLUDES := -Isrc/platform -Isrc/board -Isrc/storage -Isrc/audio -Isrc/app -Isrc/ui -Isrc/ui/lxr -Isrc/engine -Isrc/engine/port \
	-Isrc/engine/lxr -Isrc/engine/lxr/Soundengine -isystem third_party/cmsis/Core_A \
	-isystem $(SW)/include -isystem $(SW)/include/hw -isystem $(SW)/include/armv7a \
	-isystem $(SW)/include/armv7a/am335x -isystem $(SW)/mmcsdlib/include -isystem third_party/fatfs
CFLAGS := $(ARCH) -std=gnu17 -mfp16-format=ieee -O2 -g -ffunction-sections -fdata-sections $(INCLUDES) -Dam335x -Dgcc
# The engine is built with clang: on the Cortex-A8 it does scalar float math
# in NEON registers, which are pipelined, instead of the unpipelined VFP.
# That needs relaxed float rules; NaN and infinity checks still hold.
ENGINE_CC ?= clang
ENGINE_FLAGS := --target=armv7a-none-eabihf -mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard -marm -fshort-enums \
	-std=gnu17 -O3 -funsafe-math-optimizations -ffunction-sections -fdata-sections -g \
	-isystem $(TOOLCHAIN)../arm-none-eabi/include $(INCLUDES) -Dam335x -Dgcc
# The LXR sources keep their own style.
LXR_WARNINGS := -w
OWN_WARNINGS := -Wall -Wextra -Werror
LDFLAGS := $(ARCH) -nostartfiles -specs=nano.specs -T src/firmware.ld -Wl,--gc-sections \
	-Wl,-Map=$(BUILD)/firmware.map

OWN_SOURCES := $(wildcard src/*/*.c) src/platform/start.S $(wildcard third_party/fonts/spleen/*.c)
FATFS_SOURCES := third_party/fatfs/ff.c third_party/fatfs/ffunicode.c
LXR_SOURCES := $(shell find src/engine/lxr src/ui/lxr -name '*.c') src/engine/port/half.c src/engine/port/user_waves.c
LXR_HEADERS := $(shell find src/engine/lxr src/engine/port src/ui/lxr -name '*.h') src/engine/engine.h
SW_SOURCES := $(addprefix $(SW)/drivers/,mcasp.c edma.c hsi2c.c gpio_v2.c uart_irda_cir.c \
	watchdog.c gpmc.c tsc_adc.c hs_mmcsd.c) \
	$(SW)/mmcsdlib/mmcsd_proto.c $(SW)/mmcsdlib/hs_mmcsdlib.c \
	$(addprefix $(SW)/system_config/,interrupt.c cache.c mmu.c cpu.c cp15.S exceptionhandler.S)
OWN_OBJECTS := $(patsubst %,$(BUILD)/%.o,$(OWN_SOURCES))
LXR_OBJECTS := $(patsubst %,$(BUILD)/%.o,$(LXR_SOURCES))
SW_OBJECTS := $(patsubst %,$(BUILD)/%.o,$(SW_SOURCES) $(FATFS_SOURCES))
CODE_BYTES = $$(( 0x$$($(NM) $(BUILD)/firmware.elf | awk '$$3 == "text_end" { print $$1 }') - 0x80000000 ))

.PHONY: all dev clean
all: $(BUILD)/app
dev: $(BUILD)/app-dev

# Listed first: make 3.81 takes the first matching pattern.
$(BUILD)/src/ui/lxr/%.o: src/ui/lxr/% $(LXR_HEADERS) Makefile
	@mkdir -p $(dir $@)
	@echo clang $<; $(ENGINE_CC) $(ENGINE_FLAGS) $(LXR_WARNINGS) -c -o $@ $<

$(BUILD)/src/engine/lxr/%.o: src/engine/lxr/% $(LXR_HEADERS) Makefile
	@mkdir -p $(dir $@)
	@echo clang $<; $(ENGINE_CC) $(ENGINE_FLAGS) $(LXR_WARNINGS) -c -o $@ $<

$(BUILD)/src/engine/port/%.o: src/engine/port/% Makefile
	@mkdir -p $(dir $@)
	@echo clang $<; $(ENGINE_CC) $(ENGINE_FLAGS) $(OWN_WARNINGS) -c -o $@ $<

$(BUILD)/src/engine/%.o: src/engine/% $(LXR_HEADERS) $(wildcard src/*/*.h) Makefile
	@mkdir -p $(dir $@)
	@echo clang $<; $(ENGINE_CC) $(ENGINE_FLAGS) $(OWN_WARNINGS) -c -o $@ $<

$(BUILD)/third_party/fatfs/%.o: third_party/fatfs/% third_party/fatfs/ffconf.h Makefile
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -c -o $@ $<

$(BUILD)/third_party/fonts/%.o: third_party/fonts/% src/app/font.h Makefile
	@mkdir -p $(dir $@)
	@echo cc $<; $(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/src/%.o: src/% $(wildcard src/*/*.h) $(LXR_HEADERS) Makefile
	@mkdir -p $(dir $@)
	@echo cc $<; $(CC) $(CFLAGS) $(OWN_WARNINGS) -c -o $@ $<

$(BUILD)/$(SW)/%.o: $(SW)/% Makefile
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -fpermissive -c -o $@ $<

$(BUILD)/firmware.elf: $(OWN_OBJECTS) $(LXR_OBJECTS) $(SW_OBJECTS) src/firmware.ld
	$(CC) $(LDFLAGS) -o $@ $(OWN_OBJECTS) $(LXR_OBJECTS) $(SW_OBJECTS) -lm -lc -lgcc
	@$(SIZE) $@

$(BUILD)/payload.bin: $(BUILD)/firmware.elf
	$(OBJCOPY) -O binary -j .vectors -j .text -j .identity $< $@

$(BUILD)/app: $(BUILD)/payload.bin tools/pack.py
	python3 tools/pack.py --code-bytes $(CODE_BYTES) $< $@

$(BUILD)/app-dev: $(BUILD)/payload.bin tools/pack.py $(SEED)
	@test -n "$(SEED)" || { echo "make dev needs SEED=<the app installed on your module>"; exit 1; }
	python3 tools/pack.py --code-bytes $(CODE_BYTES) --seed $(SEED) --seed-output $@ $< $(BUILD)/app

clean:
	rm -rf $(BUILD)
