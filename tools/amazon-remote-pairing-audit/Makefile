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
build/audit.elf: main.cpp audit.hpp backend.hpp context.hpp ../amazon-remote-module/config.hpp Makefile | build
	$(CXX) $(CXXFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs main.cpp $(LIBS) -o $@
build/audit.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Pairing Audit" "switch-amazon-remote contributors" "0.2.0-readonly" $@
build/amazon-remote-pairing-audit.nro: build/audit.elf build/audit.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/audit.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/context.elf: context_main.cpp context.hpp audit.hpp backend.hpp ../amazon-remote-module/config.hpp Makefile | build
	$(CXX) $(CXXFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs context_main.cpp $(LIBS) -o $@
build/context.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Enrollment Context" "switch-amazon-remote contributors" "0.2.0-context3" $@
build/amazon-remote-enrollment-context.nro: build/context.elf build/context.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/context.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/core-security.elf: core_main.cpp core_security_reader.hpp ../../mc_mitm/source/amazon_remote/core_security_evidence.hpp ../../mc_mitm/source/amazon_remote/target_sighting.hpp Makefile | build
	$(CXX) $(CXXFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs core_main.cpp $(LIBS) -o $@
build/core-security.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Security Audit" "switch-amazon-remote contributors" "0.1.0-readonly" $@
build/amazon-remote-security-audit.nro: build/core-security.elf build/core-security.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/core-security.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/driver-state.elf: driver_state_main.cpp driver_state_reader.hpp Makefile | build
	$(CXX) $(CXXFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs driver_state_main.cpp $(LIBS) -o $@
build/driver-state.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Driver State" "switch-amazon-remote contributors" "0.1.0-readonly" $@
build/amazon-remote-driver-state.nro: build/driver-state.elf build/driver-state.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/driver-state.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
NATIVE_TRIAL_FLAGS := -D_POSIX_C_SOURCE=200809L -DARMODULE_NATIVE_OWNER -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12
build/native-bond.elf: native_postcheck_main.cpp native_postcheck.hpp backend.hpp native_bond_owner.hpp native_bond_trial.hpp native_bond_guard.hpp ../../mc_mitm/source/amazon_remote/enrollment_context.hpp context.hpp audit.hpp $(wildcard ../amazon-remote-module/*.hpp) Makefile | build
	$(CXX) $(CXXFLAGS) $(NATIVE_TRIAL_FLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs native_postcheck_main.cpp $(LIBS) -o $@
build/native-bond.nacp: Makefile | build
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Native Bond Trial" "switch-amazon-remote contributors" "0.1.6-post1" $@
build/amazon-remote-native-bond-trial.nro: build/native-bond.elf build/native-bond.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native-bond.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
.PHONY: all
