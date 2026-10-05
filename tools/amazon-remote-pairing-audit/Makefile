ifndef DEVKITPRO
$(error Set DEVKITPRO to an installed devkitPro SDK directory)
endif
export DEVKITPRO
include $(DEVKITPRO)/libnx/switch_rules
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-exceptions -fno-rtti -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -I$(DEVKITPRO)/libnx/include
LIBS := -L$(DEVKITPRO)/libnx/lib -lnx
all: build/amazon-remote-pairing-audit.nro
build:
	mkdir -p $@
build/audit.elf: main.cpp audit.hpp backend.hpp ../amazon-remote-module/config.hpp Makefile | build
	$(CXX) $(CXXFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs main.cpp $(LIBS) -o $@
build/audit.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Pairing Audit" "switch-amazon-remote contributors" "0.2.0-readonly" $@
build/amazon-remote-pairing-audit.nro: build/audit.elf build/audit.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/audit.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
.PHONY: all
