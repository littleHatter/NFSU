.SUFFIXES:
ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set; use a devkitPro 3ds-dev shell)
endif
include $(DEVKITARM)/3ds_rules
TARGET := nfs_undercover
BUILD := build
APP_TITLE := Need for Speed Undercover
APP_DESCRIPTION := N-Gage 2.0 game, ported to New 3DS
APP_AUTHOR := NFS compatibility project
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CPPFLAGS := -D__3DS__ -I. -I$(CTRULIB)/include
CFLAGS := -O2 -g -Wall -Wno-unused-parameter -mword-relocations -ffunction-sections $(ARCH) $(CPPFLAGS)
CXXFLAGS := $(CFLAGS) -std=gnu++17 -Wno-psabi
LDFLAGS := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
SOURCES := main_3ds runtime_s3e loader heap cpu_native guest_interp
OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .o,$(SOURCES))) $(BUILD)/native_arm.o $(BUILD)/LzmaDec.o
.PHONY: all clean
all: $(TARGET).3dsx
$(TARGET).3dsx: $(TARGET).elf $(TARGET).smdh
	3dsxtool $< $@ --smdh=$(TARGET).smdh
$(TARGET).smdh: icon.png Makefile
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $< $@
$(TARGET).elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) $^ -L$(CTRULIB)/lib -lctru -lm -o $@
$(BUILD):
	mkdir -p $@
$(BUILD)/main_3ds.o: bottom_art.h prompt_art.h
prompt_art.h: artwork/A.png artwork/Start.png artwork/B.png generate_prompts.py
	python3 generate_prompts.py
bottom_art.h: bottomscreen.png generate_bottom_art.py
	python3 generate_bottom_art.py $< $@
$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) -MMD -MP $(CXXFLAGS) -c $< -o $@
$(BUILD)/native_arm.o: native_arm.S | $(BUILD)
	$(CC) -MMD -MP -x assembler-with-cpp $(ARCH) $(CPPFLAGS) -c $< -o $@
$(BUILD)/LzmaDec.o: third_party/lzma/LzmaDec.c | $(BUILD)
	$(CC) -MMD -MP $(CFLAGS) -c $< -o $@
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx $(TARGET).smdh
-include $(wildcard $(BUILD)/*.d)
