# Host-only tests need no Switch SDK. Hardware builds require a configured SDK.
ifneq ($(MAKECMDGOALS),host-test)
ifndef DEVKITPRO
$(error Set DEVKITPRO to an installed devkitPro SDK directory)
endif
export DEVKITPRO
include $(DEVKITPRO)/libnx/switch_rules
endif
CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
CXXFLAGS := -std=c++20 -O2 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fno-exceptions -fno-rtti -D_POSIX_C_SOURCE=200809L -D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -I$(DEVKITPRO)/libnx/include
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs
LIBS := -L$(DEVKITPRO)/libnx/lib -lnx
SOURCE := ../../mc_mitm/source/amazon_remote
TRANSPORT := $(SOURCE)/hogp_client.cpp $(SOURCE)/libnx_session.cpp $(SOURCE)/remote_decoder.cpp $(SOURCE)/diagnostic_log.cpp
HEADERS := $(wildcard *.hpp $(SOURCE)/*.hpp)
all: build/exefs.nsp build/amazon-remote-control.nro
build/exefs/main: build/module.elf
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/exefs/main.npdm: module.json | build/exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,-Map,build/module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/exefs.nsp: build/exefs/main build/exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/exefs $@
build/control.elf: control/main.cpp io.hpp protocol.hpp startup.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Startup Check" "switch-amazon-remote contributors" "0.2.0-a2-diag1" $@
build/amazon-remote-control.nro: build/control.elf build/control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/launch-check.elf: control/launch_check.cpp launch_test.hpp launch_sdk.hpp io.hpp protocol.hpp startup.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/launch_check.cpp $(LIBS) -o $@
build/launch-check.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Launch Check" "switch-amazon-remote contributors" "0.2.0-a2-diag2" $@
build/amazon-remote-launch-check.nro: build/launch-check.elf build/launch-check.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/launch-check.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/exefs:
	mkdir -p $@
host-test: tests/test_module.cpp tests/test_hdls.cpp tests/test_startup.cpp tests/test_launch.cpp tests/test_launch_sdk.cpp tests/fake_sdk/switch.h hdls_backend.hpp controller.hpp protocol.hpp io.hpp config.hpp startup.hpp startup_storage.hpp launch_test.hpp launch_sdk.hpp $(SOURCE)/remote_decoder.hpp
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_module.cpp -o tests/test_module
	./tests/test_module
	mkdir -p build
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_hdls.cpp -o build/test_hdls
	./build/test_hdls
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_startup.cpp -o build/test_startup
	./build/test_startup
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_launch.cpp -o build/test_launch
	./build/test_launch
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_launch_sdk.cpp -o build/test_launch_sdk
	./build/test_launch_sdk
HOST_CXX ?= c++
.PHONY: all host-test
