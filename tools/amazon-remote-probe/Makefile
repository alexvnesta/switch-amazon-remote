ifndef DEVKITPRO
$(error Set DEVKITPRO to an installed devkitPro SDK directory)
endif
export DEVKITPRO
include $(DEVKITPRO)/libnx/switch_rules
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
APP_TITLE := Amazon Remote Probe
APP_AUTHOR := switch-amazon-remote contributors
APP_VERSION := 0.1.10
SOURCE := ../../mc_mitm/source/amazon_remote
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-exceptions -fno-rtti -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -I$(DEVKITPRO)/libnx/include
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -Wl,-Map,amazon-remote-probe.map
LIBS := -L$(DEVKITPRO)/libnx/lib -lnx
FILES := main.cpp $(SOURCE)/hogp_client.cpp $(SOURCE)/libnx_session.cpp $(SOURCE)/remote_decoder.cpp $(SOURCE)/diagnostic_log.cpp
all: amazon-remote-probe.nro
amazon-remote-probe.elf: $(FILES) $(wildcard $(SOURCE)/*.hpp)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(FILES) $(LIBS) -o $@
amazon-remote-probe.nacp: Makefile
	$(DEVKITPRO)/tools/bin/nacptool --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@
amazon-remote-probe.nro: amazon-remote-probe.elf amazon-remote-probe.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=amazon-remote-probe.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
.PHONY: all
