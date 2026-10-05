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
build/safe-id-exefs/main: build/exefs/main
	mkdir -p build/safe-id-exefs
	cp $< $@
build/safe-id-exefs/main.npdm: module-safe-id.json
	mkdir -p build/safe-id-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/safe-id-exefs.nsp: build/safe-id-exefs/main build/safe-id-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/safe-id-exefs $@
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
build/state-check.elf: control/state_check.cpp state_sdk.hpp state_check.hpp startup.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/state_check.cpp $(LIBS) -o $@
build/state-check.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote State Check" "switch-amazon-remote contributors" "0.2.0-a2-diag3" $@
build/amazon-remote-state-check.nro: build/state-check.elf build/state-check.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/state-check.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/memory-check.elf: control/memory_check.cpp memory_sdk.hpp memory_check.hpp module_identity.hpp state_sdk.hpp state_check.hpp startup.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/memory_check.cpp $(LIBS) -o $@
build/memory-check.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote RAM Check" "switch-amazon-remote contributors" "0.2.0-a2-diag4" $@
build/amazon-remote-memory-check.nro: build/memory-check.elf build/memory-check.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/memory-check.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/layout-check.elf: control/layout_check.cpp layout_check.hpp memory_sdk.hpp memory_check.hpp module_identity.hpp state_sdk.hpp state_check.hpp startup.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/layout_check.cpp $(LIBS) -o $@
build/layout-check.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Layout Check" "switch-amazon-remote contributors" "0.2.0-a2-diag5" $@
build/amazon-remote-layout-check.nro: build/layout-check.elf build/layout-check.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/layout-check.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/launch-fix.elf: control/launch_fix.cpp launch_fix.hpp launch_fix_sdk.hpp state_sdk.hpp state_check.hpp startup.hpp protocol.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/launch_fix.cpp $(LIBS) -o $@
build/launch-fix.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Launch Fix" "switch-amazon-remote contributors" "0.2.0-a2-diag6" $@
build/amazon-remote-launch-fix.nro: build/launch-fix.elf build/launch-fix.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/launch-fix.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/native-hdls.elf: native_hdls.cpp native_hdls.hpp hdls_backend.hpp hdls_trace.hpp controller.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) native_hdls.cpp $(LIBS) -o $@
build/native-hdls-exefs/main: build/native-hdls.elf
	mkdir -p build/native-hdls-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/native-hdls-exefs/main.npdm: module-native-hdls.json
	mkdir -p build/native-hdls-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/native-hdls-exefs.nsp: build/native-hdls-exefs/main build/native-hdls-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/native-hdls-exefs $@
build/native-pad-check.elf: control/native_hdls_check.cpp native_hdls.hpp native_identity.hpp native_hdls_sdk.hpp launch_fix_sdk.hpp launch_fix.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/native_hdls_check.cpp $(LIBS) -o $@
build/native-pad-check.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Native Pad Check 8" "switch-amazon-remote contributors" "0.2.0-a2-diag8" $@
build/amazon-remote-native-pad-check.nro: build/native-pad-check.elf build/native-pad-check.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native-pad-check.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav9-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV9 $(LDFLAGS) -Wl,-Map,build/nav9-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav9-exefs/main: build/nav9-module.elf
	mkdir -p build/nav9-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav9-exefs/main.npdm: module-nav9.json
	mkdir -p build/nav9-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav9-exefs.nsp: build/nav9-exefs/main build/nav9-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav9-exefs $@
build/navigation9-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV9 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation9-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 9" "switch-amazon-remote contributors" "0.2.0-a2-nav9" $@
build/amazon-remote-navigation9.nro: build/navigation9-control.elf build/navigation9-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation9-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav10-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV10 $(LDFLAGS) -Wl,-Map,build/nav10-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav10-exefs/main: build/nav10-module.elf
	mkdir -p build/nav10-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav10-exefs/main.npdm: module-nav10.json
	mkdir -p build/nav10-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav10-exefs.nsp: build/nav10-exefs/main build/nav10-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav10-exefs $@
build/navigation10-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV10 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation10-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 10" "switch-amazon-remote contributors" "0.2.0-a2-nav10" $@
build/amazon-remote-navigation10.nro: build/navigation10-control.elf build/navigation10-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation10-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav11-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV11 $(LDFLAGS) -Wl,-Map,build/nav11-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav11-exefs/main: build/nav11-module.elf
	mkdir -p build/nav11-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav11-exefs/main.npdm: module-nav11.json
	mkdir -p build/nav11-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav11-exefs.nsp: build/nav11-exefs/main build/nav11-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav11-exefs $@
build/navigation11-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV11 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation11-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 11" "switch-amazon-remote contributors" "0.2.0-a2-nav11" $@
build/amazon-remote-navigation11.nro: build/navigation11-control.elf build/navigation11-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation11-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav12-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV12 $(LDFLAGS) -Wl,-Map,build/nav12-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav12-exefs/main: build/nav12-module.elf
	mkdir -p build/nav12-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav12-exefs/main.npdm: module-nav12.json
	mkdir -p build/nav12-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav12-exefs.nsp: build/nav12-exefs/main build/nav12-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav12-exefs $@
build/navigation12-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV12 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation12-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 12" "switch-amazon-remote contributors" "0.2.0-a2-nav12" $@
build/amazon-remote-navigation12.nro: build/navigation12-control.elf build/navigation12-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation12-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/exefs:
	mkdir -p $@
build/nav13-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) -Wl,-Map,build/nav13-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav13-exefs/main: build/nav13-module.elf
	mkdir -p build/nav13-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav13-exefs/main.npdm: module-nav13.json
	mkdir -p build/nav13-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav13-exefs.nsp: build/nav13-exefs/main build/nav13-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav13-exefs $@
build/navigation13-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation13-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 13" "switch-amazon-remote contributors" "0.2.0-a2-nav13" $@
build/amazon-remote-navigation13.nro: build/navigation13-control.elf build/navigation13-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation13-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav14-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) -Wl,-Map,build/nav14-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav14-exefs/main: build/nav14-module.elf
	mkdir -p build/nav14-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav14-exefs/main.npdm: module-nav14.json
	mkdir -p build/nav14-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav14-exefs.nsp: build/nav14-exefs/main build/nav14-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav14-exefs $@
build/nav15-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) -Wl,-Map,build/nav15-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav15-exefs/main: build/nav15-module.elf
	mkdir -p build/nav15-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav15-exefs/main.npdm: module-nav15.json
	mkdir -p build/nav15-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav15-exefs.nsp: build/nav15-exefs/main build/nav15-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav15-exefs $@
build/navigation15-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation15-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 15" "switch-amazon-remote contributors" "0.2.0-a2-nav15" $@
build/amazon-remote-navigation15.nro: build/navigation15-control.elf build/navigation15-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation15-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav15s-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) -Wl,-Map,build/nav15s-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav15s-exefs/main: build/nav15s-module.elf
	mkdir -p build/nav15s-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav15s-exefs/main.npdm: module-nav15.json
	mkdir -p build/nav15s-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav15s-exefs.nsp: build/nav15s-exefs/main build/nav15s-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav15s-exefs $@
build/navigation15s-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation15s-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 15S" "switch-amazon-remote contributors" "0.2.0-a2-nav15s" $@
build/amazon-remote-navigation15s.nro: build/navigation15s-control.elf build/navigation15s-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation15s-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/navigation14-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation14-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 14" "switch-amazon-remote contributors" "0.2.0-a2-nav14" $@
build/amazon-remote-navigation14.nro: build/navigation14-control.elf build/navigation14-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation14-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
host-test: tests/test_module.cpp tests/test_hdls.cpp tests/test_startup.cpp tests/test_launch.cpp tests/test_launch_sdk.cpp tests/fake_sdk/switch.h hdls_backend.hpp controller.hpp protocol.hpp io.hpp config.hpp startup.hpp startup_storage.hpp launch_test.hpp launch_sdk.hpp $(SOURCE)/remote_decoder.hpp
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_module.cpp -o tests/test_module
	./tests/test_module
	mkdir -p build
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_connection_capture.cpp -o build/test_connection_capture
	./build/test_connection_capture
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_hdls.cpp -o build/test_hdls
	./build/test_hdls
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_startup.cpp -o build/test_startup
	./build/test_startup
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_launch.cpp -o build/test_launch
	./build/test_launch
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_launch_sdk.cpp -o build/test_launch_sdk
	./build/test_launch_sdk
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_state_sdk.cpp -o build/test_state_sdk
	./build/test_state_sdk
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_memory_sdk.cpp -o build/test_memory_sdk
	./build/test_memory_sdk
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_launch_fix.cpp -o build/test_launch_fix
	./build/test_launch_fix
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_native_hdls.cpp -o build/test_native_hdls
	./build/test_native_hdls
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV9 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation
	./build/test_navigation
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV10 -Itests/fake_sdk tests/test_status_storage.cpp -o build/test_status_storage
	./build/test_status_storage
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV10 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation10
	./build/test_navigation10
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV11 -Itests/fake_sdk tests/test_status_storage.cpp -o build/test_status_storage11
	./build/test_status_storage11
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV11 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation11
	./build/test_navigation11
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV12 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation12
	./build/test_navigation12
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV12 -Itests/fake_sdk tests/test_status_contention.cpp -o build/test_status_contention
	./build/test_status_contention
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_lifecycle.cpp -o build/test_lifecycle
	./build/test_lifecycle
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV13 -DARMODULE_NAV12 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation13
	./build/test_navigation13
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation14
	./build/test_navigation14
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation15
	./build/test_navigation15
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV15 tests/test_enrollment.cpp -o build/test_enrollment15
	./build/test_enrollment15
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV14 tests/test_enrollment.cpp -o build/test_enrollment14
	./build/test_enrollment14
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation15s
	./build/test_navigation15s
HOST_CXX ?= c++
.PHONY: all host-test
