ifndef DEVKITPRO
$(error Set DEVKITPRO to an installed devkitPro SDK directory)
endif
export DEVKITPRO
include $(DEVKITPRO)/libnx/switch_rules
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-exceptions -fno-rtti -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -I$(DEVKITPRO)/libnx/include
LIBS := -L$(DEVKITPRO)/libnx/lib -lnx
all: build/amazon-remote-bond-check.nro
build:
	mkdir -p $@
build/check.elf: main.cpp check.hpp ../amazon-remote-module/config.hpp Makefile | build
	$(CXX) $(CXXFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs main.cpp $(LIBS) -o $@
build/check.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Paired-list Check" "switch-amazon-remote contributors" "0.1.0-readonly" $@
build/amazon-remote-bond-check.nro: build/check.elf build/check.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/check.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
.PHONY: all
