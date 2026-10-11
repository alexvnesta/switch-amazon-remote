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
# Explicit offline candidate target only, excluded from the default build.
build/passive-reader.elf: control/passive_reader_main.cpp control/passive_read_flow.hpp control/passive_read_io.hpp control/observation_checkpoint.hpp control/observation_checkpoint_io.hpp control/operator_input.hpp $(SOURCE)/controller_passive_cache.hpp $(SOURCE)/controller_passive.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/passive_reader_main.cpp $(LIBS) -o $@
build/passive-reader.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Report" "switch-amazon-remote contributors" "0.2.0-p1-readonly" $@
build/amazon-remote-passive-reader.nro: build/passive-reader.elf build/passive-reader.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/passive-reader.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/native-metadata.elf: control/native_metadata_main.cpp control/driver_identity_sdk.hpp control/driver_identity.hpp control/operator_input.hpp $(SOURCE)/native_metadata.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/native_metadata_main.cpp $(LIBS) -o $@
build/native-metadata.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-m1" $@
build/amazon-remote-native-metadata.nro: build/native-metadata.elf build/native-metadata.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native-metadata.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
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
NAV16_FLAGS := -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12
NAV17_FLAGS := -DARMODULE_NATIVE_OWNER -DARMODULE_NAV17 $(NAV16_FLAGS)
RECONNECT_FLAGS := -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 $(NAV16_FLAGS)
RECONNECT2_FLAGS := -DARMODULE_DISCOVERY_TRACE $(RECONNECT_FLAGS)
KNOWN_FLAGS := -DARMODULE_KNOWN_RECONNECT_CHECK $(RECONNECT_FLAGS)
SCAN_ASSISTED_FLAGS := -DARMODULE_SCAN_ASSISTED_CHECK $(RECONNECT2_FLAGS)
MANUFACTURER_SCAN_FLAGS := -DARMODULE_MANUFACTURER_SCAN_CHECK $(SCAN_ASSISTED_FLAGS)
STARTUP_TRACE_FLAGS := -DARMODULE_STARTUP_TRACE $(MANUFACTURER_SCAN_FLAGS)
DISCONNECT_FLAGS := -DARMODULE_DISCONNECT_CHECK $(STARTUP_TRACE_FLAGS)
SLOT_CHECK_FLAGS := -DARMODULE_CONTROLLER_SLOTS $(DISCONNECT_FLAGS)
WINDOW_CHECK_FLAGS := -DARMODULE_CAPTURE_PREFLIGHT $(SLOT_CHECK_FLAGS)
CONNECT_DISPATCH_FLAGS := -DARMODULE_CONNECT_DISPATCH $(DISCONNECT_FLAGS)
CONNECT_SETUP_FLAGS := -DARMODULE_CONNECT_DISPATCH $(WINDOW_CHECK_FLAGS)
PEER_C1_FLAGS := -DARMODULE_NATIVE_PEER_C1 -DARMODULE_CENTRAL_BOND_B1
HCI_C2_FLAGS := -DARMODULE_NATIVE_HCI_C2 $(PEER_C1_FLAGS)
AUTO_A1_FLAGS := -DARMODULE_AUTOMATIC_A1 $(HCI_C2_FLAGS) $(CONNECT_DISPATCH_FLAGS)
AUTO_A2_FLAGS := -DARMODULE_AUTOMATIC_A2 $(AUTO_A1_FLAGS)
WAKE_A3_FLAGS := -DARMODULE_WAKE_A3 $(AUTO_A2_FLAGS)
STARTUP_A4_FLAGS := -DARMODULE_STARTUP_A4 $(WAKE_A3_FLAGS)
RETRY_A5_FLAGS := -DARMODULE_RETRY_A5 $(STARTUP_A4_FLAGS)
LATENCY_A6_FLAGS := -DARMODULE_LATENCY_A6 $(RETRY_A5_FLAGS)
HID_INFO_H1_FLAGS := -DARMODULE_HID_INFO_H1 $(LATENCY_A6_FLAGS)
build/controller-query-q1.elf: control/controller_query_main.cpp control/controller_query_report.hpp control/operator_input.hpp io.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(HID_INFO_H1_FLAGS) $(LDFLAGS) control/controller_query_main.cpp $(LIBS) -o $@
build/controller-query-q1.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Capabilities" "switch-amazon-remote contributors" "0.3.0-query-q1" $@
build/amazon-remote-capabilities-q1.nro: build/controller-query-q1.elf build/controller-query-q1.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/controller-query-q1.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/controller-query-q2.elf: control/controller_query_main.cpp control/controller_query_report.hpp control/controller_query_isolation.hpp control/controller_query_isolation_sdk.hpp control/operator_input.hpp io.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(HID_INFO_H1_FLAGS) -DARMODULE_CONTROLLER_QUERY_Q2 $(LDFLAGS) control/controller_query_main.cpp $(LIBS) -o $@
build/controller-query-q2.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Capabilities" "switch-amazon-remote contributors" "0.3.0-query-q2" $@
build/amazon-remote-capabilities-q2.nro: build/controller-query-q2.elf build/controller-query-q2.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/controller-query-q2.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/hid-info-h1-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(HID_INFO_H1_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/hid-info-h1-exefs/main: build/hid-info-h1-module.elf
	mkdir -p build/hid-info-h1-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/hid-info-h1-exefs/main.npdm: module-nav15.json
	mkdir -p build/hid-info-h1-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/hid-info-h1-exefs.nsp: build/hid-info-h1-exefs/main build/hid-info-h1-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/hid-info-h1-exefs $@
build/hid-info-h1-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(HID_INFO_H1_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/hid-info-h1-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-hid-h1" $@
build/amazon-remote-hid-info-h1.nro: build/hid-info-h1-control.elf build/hid-info-h1-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/hid-info-h1-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
# SETUP R3: H1 plus retire-at-sleep for an unfinished attempt (companion must match).
SETUP_R3_FLAGS := -DARMODULE_SETUP_SLEEP_R3 $(HID_INFO_H1_FLAGS)
build/setup-r3-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(SETUP_R3_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/setup-r3-exefs/main: build/setup-r3-module.elf
	mkdir -p build/setup-r3-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/setup-r3-exefs/main.npdm: module-nav15.json
	mkdir -p build/setup-r3-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/setup-r3-exefs.nsp: build/setup-r3-exefs/main build/setup-r3-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/setup-r3-exefs $@
build/setup-r3-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(SETUP_R3_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/setup-r3-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.1-setup-r3" $@
build/amazon-remote-setup-r3.nro: build/setup-r3-control.elf build/setup-r3-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/setup-r3-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/latency-a6-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LATENCY_A6_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/latency-a6-exefs/main: build/latency-a6-module.elf
	mkdir -p build/latency-a6-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/latency-a6-exefs/main.npdm: module-nav15.json
	mkdir -p build/latency-a6-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/latency-a6-exefs.nsp: build/latency-a6-exefs/main build/latency-a6-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/latency-a6-exefs $@
build/latency-a6-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LATENCY_A6_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/latency-a6-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-lat-a6" $@
build/amazon-remote-latency-a6.nro: build/latency-a6-control.elf build/latency-a6-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/latency-a6-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/retry-a5-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(RETRY_A5_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/retry-a5-exefs/main: build/retry-a5-module.elf
	mkdir -p build/retry-a5-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/retry-a5-exefs/main.npdm: module-nav15.json
	mkdir -p build/retry-a5-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/retry-a5-exefs.nsp: build/retry-a5-exefs/main build/retry-a5-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/retry-a5-exefs $@
build/retry-a5-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(RETRY_A5_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/retry-a5-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-retry-a5" $@
build/amazon-remote-retry-a5.nro: build/retry-a5-control.elf build/retry-a5-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/retry-a5-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/startup-a4-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(STARTUP_A4_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/startup-a4-exefs/main: build/startup-a4-module.elf
	mkdir -p build/startup-a4-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/startup-a4-exefs/main.npdm: module-nav15.json
	mkdir -p build/startup-a4-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/startup-a4-exefs.nsp: build/startup-a4-exefs/main build/startup-a4-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/startup-a4-exefs $@
build/startup-a4-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(STARTUP_A4_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/startup-a4-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-boot-a4" $@
build/amazon-remote-startup-a4.nro: build/startup-a4-control.elf build/startup-a4-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/startup-a4-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/wake-a3-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(WAKE_A3_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/wake-a3-exefs/main: build/wake-a3-module.elf
	mkdir -p build/wake-a3-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/wake-a3-exefs/main.npdm: module-nav15.json
	mkdir -p build/wake-a3-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/wake-a3-exefs.nsp: build/wake-a3-exefs/main build/wake-a3-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/wake-a3-exefs $@
build/wake-a3-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(WAKE_A3_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/wake-a3-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-wake-a3" $@
build/amazon-remote-wake-a3.nro: build/wake-a3-control.elf build/wake-a3-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/wake-a3-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/auto-a2-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(AUTO_A2_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/auto-a2-exefs/main: build/auto-a2-module.elf
	mkdir -p build/auto-a2-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/auto-a2-exefs/main.npdm: module-nav15.json
	mkdir -p build/auto-a2-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/auto-a2-exefs.nsp: build/auto-a2-exefs/main build/auto-a2-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/auto-a2-exefs $@
build/auto-a2-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(AUTO_A2_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/auto-a2-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-auto-a2" $@
build/amazon-remote-auto-a2.nro: build/auto-a2-control.elf build/auto-a2-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/auto-a2-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/auto-a1-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(AUTO_A1_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/auto-a1-exefs/main: build/auto-a1-module.elf
	mkdir -p build/auto-a1-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/auto-a1-exefs/main.npdm: module-nav15.json
	mkdir -p build/auto-a1-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/auto-a1-exefs.nsp: build/auto-a1-exefs/main build/auto-a1-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/auto-a1-exefs $@
build/auto-a1-control.elf: control/automatic_main.cpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(AUTO_A1_FLAGS) $(LDFLAGS) control/automatic_main.cpp $(LIBS) -o $@
build/auto-a1-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote" "switch-amazon-remote contributors" "0.3.0-auto-a1" $@
build/amazon-remote-auto-a1.nro: build/auto-a1-control.elf build/auto-a1-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/auto-a1-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/hci-c2-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(HCI_C2_FLAGS) $(CONNECT_DISPATCH_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/hci-c2-exefs/main: build/hci-c2-module.elf
	mkdir -p build/hci-c2-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/hci-c2-exefs/main.npdm: module-nav15.json
	mkdir -p build/hci-c2-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/hci-c2-exefs.nsp: build/hci-c2-exefs/main build/hci-c2-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/hci-c2-exefs $@
build/hci-c2-control.elf: control/main.cpp control/saved_discovery_view.hpp hci_c2_identity.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(HCI_C2_FLAGS) -DARMODULE_OPERATOR_SLOTS $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/hci-c2-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-hci-c2" $@
build/amazon-remote-hci-c2.nro: build/hci-c2-control.elf build/hci-c2-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/hci-c2-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/peer-c1-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(PEER_C1_FLAGS) $(CONNECT_DISPATCH_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/peer-c1-exefs/main: build/peer-c1-module.elf
	mkdir -p build/peer-c1-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/peer-c1-exefs/main.npdm: module-nav15.json
	mkdir -p build/peer-c1-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/peer-c1-exefs.nsp: build/peer-c1-exefs/main build/peer-c1-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/peer-c1-exefs $@
build/peer-c1-control.elf: control/main.cpp control/saved_discovery_view.hpp peer_c1_identity.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(PEER_C1_FLAGS) -DARMODULE_OPERATOR_SLOTS $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/peer-c1-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-peer-c1" $@
build/amazon-remote-peer-c1.nro: build/peer-c1-control.elf build/peer-c1-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/peer-c1-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/central-b1-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_SAVED_DISCOVERY_B3 -DARMODULE_CENTRAL_BOND_B1 $(CONNECT_DISPATCH_FLAGS) $(LDFLAGS) main.cpp $(TRANSPORT) $(LIBS) -o $@
build/central-b1-exefs/main: build/central-b1-module.elf
	mkdir -p build/central-b1-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/central-b1-exefs/main.npdm: module-nav15.json
	mkdir -p build/central-b1-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/central-b1-exefs.nsp: build/central-b1-exefs/main build/central-b1-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/central-b1-exefs $@
build/central-b1-control.elf: control/main.cpp control/saved_discovery_view.hpp control/setup_view.hpp central_b1_identity.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_SAVED_DISCOVERY_B3 -DARMODULE_CENTRAL_BOND_B1 -DARMODULE_OPERATOR_SLOTS $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/central-b1-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-saved-b3" $@
build/amazon-remote-central-b1.nro: build/central-b1-control.elf build/central-b1-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/central-b1-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
# Read-only metadata check, temporarily occupying the existing Setup slot.
build/driver-identity-control.elf: control/driver_identity_main.cpp control/driver_identity_sdk.hpp control/driver_identity.hpp control/operator_input.hpp io.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(LDFLAGS) control/driver_identity_main.cpp $(LIBS) -o $@
build/driver-identity-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-id-i1" $@
build/amazon-remote-driver-identity.nro: build/driver-identity-control.elf build/driver-identity-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/driver-identity-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
# Frontend-only P4 retains the exact installed P3 module identity and protocol.
# M2 adds only fixed M1 metadata observation around the existing P3 Link.
build/bond-properties-control.elf: control/main.cpp control/security_capture.hpp control/security_capture_sdk.hpp control/driver_identity.hpp control/driver_identity_sdk.hpp control/setup_view.hpp control/operator_input.hpp control/capture_preflight.hpp $(HEADERS) $(wildcard ../../mc_mitm/source/amazon_remote/*.hpp) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_BOND_PROPERTIES -DARMODULE_SECURITY_CAPTURE -DARMODULE_OPERATOR_SLOTS $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/bond-properties-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-props-m3" $@
build/amazon-remote-bond-properties.nro: build/bond-properties-control.elf build/bond-properties-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/bond-properties-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/security-events-control.elf: control/main.cpp control/security_capture.hpp control/security_capture_sdk.hpp control/driver_identity.hpp control/driver_identity_sdk.hpp control/setup_view.hpp control/operator_input.hpp control/capture_preflight.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_SECURITY_CAPTURE -DARMODULE_OPERATOR_SLOTS $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/security-events-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-events-m2" $@
build/amazon-remote-security-events.nro: build/security-events-control.elf build/security-events-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/security-events-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/operator-slots-control.elf: control/main.cpp control/setup_view.hpp control/operator_input.hpp control/capture_preflight.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) -DARMODULE_OPERATOR_SLOTS $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/operator-slots-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-p4" $@
build/amazon-remote-operator-slots.nro: build/operator-slots-control.elf build/operator-slots-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/operator-slots-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/connect-dispatch-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(CONNECT_DISPATCH_FLAGS) $(LDFLAGS) -Wl,-Map,build/connect-dispatch-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/connect-dispatch-exefs/main: build/connect-dispatch-module.elf
	mkdir -p build/connect-dispatch-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/connect-dispatch-exefs/main.npdm: module-nav15.json
	mkdir -p build/connect-dispatch-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/connect-dispatch-exefs.nsp: build/connect-dispatch-exefs/main build/connect-dispatch-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/connect-dispatch-exefs $@
build/connect-dispatch-control.elf: control/main.cpp control/setup_view.hpp control/operator_input.hpp control/capture_preflight.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(CONNECT_SETUP_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/connect-dispatch-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-p3" $@
build/amazon-remote-connect-dispatch.nro: build/connect-dispatch-control.elf build/connect-dispatch-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/connect-dispatch-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/window-control.elf: control/main.cpp control/setup_view.hpp control/operator_input.hpp control/capture_preflight.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(WINDOW_CHECK_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/window-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-p2" $@
build/amazon-remote-window.nro: build/window-control.elf build/window-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/window-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
# Setup-only P1 deliberately keeps the existing D3 module hash and binary.
build/slots-control.elf: control/main.cpp control/setup_view.hpp control/operator_input.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(SLOT_CHECK_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/slots-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-p1" $@
build/amazon-remote-slots.nro: build/slots-control.elf build/slots-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/slots-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/disconnect-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(DISCONNECT_FLAGS) $(LDFLAGS) -Wl,-Map,build/disconnect-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/disconnect-exefs/main: build/disconnect-module.elf
	mkdir -p build/disconnect-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/disconnect-exefs/main.npdm: module-nav15.json
	mkdir -p build/disconnect-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/disconnect-exefs.nsp: build/disconnect-exefs/main build/disconnect-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/disconnect-exefs $@
build/disconnect-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(DISCONNECT_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/disconnect-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-d3" $@
build/amazon-remote-disconnect.nro: build/disconnect-control.elf build/disconnect-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/disconnect-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/startup-trace-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(STARTUP_TRACE_FLAGS) $(LDFLAGS) -Wl,-Map,build/startup-trace-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/startup-trace-exefs/main: build/startup-trace-module.elf
	mkdir -p build/startup-trace-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/startup-trace-exefs/main.npdm: module-nav15.json
	mkdir -p build/startup-trace-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/startup-trace-exefs.nsp: build/startup-trace-exefs/main build/startup-trace-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/startup-trace-exefs $@
build/startup-trace-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(STARTUP_TRACE_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/startup-trace-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-s3" $@
build/amazon-remote-startup-trace.nro: build/startup-trace-control.elf build/startup-trace-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/startup-trace-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/manufacturer-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(MANUFACTURER_SCAN_FLAGS) $(LDFLAGS) -Wl,-Map,build/manufacturer-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/manufacturer-exefs/main: build/manufacturer-module.elf
	mkdir -p build/manufacturer-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/manufacturer-exefs/main.npdm: module-nav15.json
	mkdir -p build/manufacturer-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/manufacturer-exefs.nsp: build/manufacturer-exefs/main build/manufacturer-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/manufacturer-exefs $@
build/manufacturer-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(MANUFACTURER_SCAN_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/manufacturer-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-s2" $@
build/amazon-remote-manufacturer-scan.nro: build/manufacturer-control.elf build/manufacturer-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/manufacturer-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/scan-assisted-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(SCAN_ASSISTED_FLAGS) $(LDFLAGS) -Wl,-Map,build/scan-assisted-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/scan-assisted-exefs/main: build/scan-assisted-module.elf
	mkdir -p build/scan-assisted-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/scan-assisted-exefs/main.npdm: module-nav15.json
	mkdir -p build/scan-assisted-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/scan-assisted-exefs.nsp: build/scan-assisted-exefs/main build/scan-assisted-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/scan-assisted-exefs $@
build/scan-assisted-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(SCAN_ASSISTED_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/scan-assisted-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-s1" $@
build/amazon-remote-scan-assisted.nro: build/scan-assisted-control.elf build/scan-assisted-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/scan-assisted-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/known-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(KNOWN_FLAGS) $(LDFLAGS) -Wl,-Map,build/known-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/known-exefs/main: build/known-module.elf
	mkdir -p build/known-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/known-exefs/main.npdm: module-nav15.json
	mkdir -p build/known-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/known-exefs.nsp: build/known-exefs/main build/known-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/known-exefs $@
build/known-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(KNOWN_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/known-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-k1" $@
build/amazon-remote-known.nro: build/known-control.elf build/known-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/known-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/reconnect2-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(RECONNECT2_FLAGS) $(LDFLAGS) -Wl,-Map,build/reconnect2-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/reconnect2-exefs/main: build/reconnect2-module.elf
	mkdir -p build/reconnect2-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/reconnect2-exefs/main.npdm: module-nav15.json
	mkdir -p build/reconnect2-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/reconnect2-exefs.nsp: build/reconnect2-exefs/main build/reconnect2-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/reconnect2-exefs $@
build/reconnect2-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(RECONNECT2_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/reconnect2-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-r2" $@
build/amazon-remote-reconnect2.nro: build/reconnect2-control.elf build/reconnect2-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/reconnect2-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/reconnect-module.elf: main.cpp $(TRANSPORT) $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(RECONNECT_FLAGS) $(LDFLAGS) -Wl,-Map,build/reconnect-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/reconnect-exefs/main: build/reconnect-module.elf
	mkdir -p build/reconnect-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/reconnect-exefs/main.npdm: module-nav15.json
	mkdir -p build/reconnect-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/reconnect-exefs.nsp: build/reconnect-exefs/main build/reconnect-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/reconnect-exefs $@
build/reconnect-control.elf: control/main.cpp control/setup_view.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(RECONNECT_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/reconnect-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-r1" $@
build/amazon-remote-reconnect.nro: build/reconnect-control.elf build/reconnect-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/reconnect-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav17-module.elf: main.cpp $(TRANSPORT) $(HEADERS) ../amazon-remote-pairing-audit/native_bond_owner.hpp ../amazon-remote-pairing-audit/native_bond_guard.hpp ../amazon-remote-pairing-audit/native_bond_trial.hpp ../amazon-remote-pairing-audit/context.hpp ../amazon-remote-pairing-audit/backend.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(NAV17_FLAGS) $(LDFLAGS) -Wl,-Map,build/nav17-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav17-exefs/main: build/nav17-module.elf
	mkdir -p build/nav17-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav17-exefs/main.npdm: module-nav15.json
	mkdir -p build/nav17-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav17-exefs.nsp: build/nav17-exefs/main build/nav17-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav17-exefs $@
build/navigation17-control.elf: control/main.cpp control/setup_view.hpp ../amazon-remote-pairing-audit/native_bond_guard.hpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(NAV17_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation17-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Setup" "switch-amazon-remote contributors" "0.2.0-nav17-o1" $@
build/amazon-remote-navigation17.nro: build/navigation17-control.elf build/navigation17-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation17-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/nav16-module.elf: main.cpp $(TRANSPORT) $(HEADERS) ../amazon-remote-pairing-audit/context.hpp ../amazon-remote-pairing-audit/backend.hpp Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(NAV16_FLAGS) $(LDFLAGS) -Wl,-Map,build/nav16-module.map main.cpp $(TRANSPORT) $(LIBS) -o $@
build/nav16-exefs/main: build/nav16-module.elf
	mkdir -p build/nav16-exefs
	$(DEVKITPRO)/tools/bin/elf2nso $< $@
build/nav16-exefs/main.npdm: module-nav15.json
	mkdir -p build/nav16-exefs
	$(DEVKITPRO)/tools/bin/npdmtool $< $@
build/nav16-exefs.nsp: build/nav16-exefs/main build/nav16-exefs/main.npdm
	$(DEVKITPRO)/tools/bin/build_pfs0 build/nav16-exefs $@
build/navigation16-control.elf: control/main.cpp $(HEADERS) Makefile | build/exefs
	$(CXX) $(CXXFLAGS) $(NAV16_FLAGS) $(LDFLAGS) control/main.cpp $(LIBS) -o $@
build/navigation16-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 16" "switch-amazon-remote contributors" "0.2.0-a2-nav16" $@
build/amazon-remote-navigation16.nro: build/navigation16-control.elf build/navigation16-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation16-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
build/navigation14-control.nacp: Makefile | build/exefs
	$(DEVKITPRO)/tools/bin/nacptool --create "Amazon Remote Navigation 14" "switch-amazon-remote contributors" "0.2.0-a2-nav14" $@
build/amazon-remote-navigation14.nro: build/navigation14-control.elf build/navigation14-control.nacp
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/navigation14-control.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg
host-test: tests/test_module.cpp tests/test_hdls.cpp tests/test_startup.cpp tests/test_launch.cpp tests/test_launch_sdk.cpp tests/fake_sdk/switch.h hdls_backend.hpp controller.hpp protocol.hpp io.hpp config.hpp startup.hpp startup_storage.hpp launch_test.hpp launch_sdk.hpp $(SOURCE)/remote_decoder.hpp $(SOURCE)/remote_decoder.cpp $(SOURCE)/k7q3m7_descriptor.hpp
	mkdir -p build
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_early_link_retry.cpp -o build/test_early_link_retry
	./build/test_early_link_retry
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(STARTUP_A4_FLAGS) -Itests/fake_sdk tests/test_startup_power.cpp -o build/test_startup_power
	./build/test_startup_power
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(WAKE_A3_FLAGS) -Itests/fake_sdk tests/test_wake_idle_health.cpp -o build/test_wake_idle_health
	./build/test_wake_idle_health
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(AUTO_A2_FLAGS) -Itests/fake_sdk tests/test_wake_idle_health.cpp -o build/test_a2_idle_health
	./build/test_a2_idle_health
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(WAKE_A3_FLAGS) tests/test_lifecycle.cpp -o build/test_wake_lifecycle
	./build/test_wake_lifecycle
	mkdir -p build
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(AUTO_A2_FLAGS) -Itests/fake_sdk tests/test_restartable_status.cpp -o build/test_restartable_status
	./build/test_restartable_status
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(AUTO_A1_FLAGS) tests/test_automatic_start.cpp -o build/test_automatic_start
	./build/test_automatic_start
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_security_capture.cpp -o build/test_security_capture
	./build/test_security_capture
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_BOND_PROPERTIES tests/test_security_capture.cpp -o build/test_bond_properties_capture
	./build/test_bond_properties_capture
	mkdir -p build
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_driver_identity.cpp -o build/test_driver_identity
	./build/test_driver_identity
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_loader tests/test_driver_identity_sdk.cpp -o build/test_driver_identity_sdk
	./build/test_driver_identity_sdk
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_operator_input.cpp -o build/test_operator_input
	./build/test_operator_input
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_module.cpp -o tests/test_module
	./tests/test_module
	mkdir -p build
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_connection_capture.cpp -o build/test_connection_capture
	./build/test_connection_capture
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/fake_sdk tests/test_hdls.cpp $(SOURCE)/remote_decoder.cpp -o build/test_hdls
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
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NATIVE_OWNER -DARMODULE_NAV15 tests/test_enrollment.cpp -o build/test_enrollment_owner
	./build/test_enrollment_owner
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV14 tests/test_enrollment.cpp -o build/test_enrollment14
	./build/test_enrollment14
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation15s
	./build/test_navigation15s
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(NAV16_FLAGS) -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation16
	./build/test_navigation16
	$(HOST_CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer $(NAV17_FLAGS) -Itests/fake_sdk tests/test_navigation.cpp -o build/test_navigation17
	./build/test_navigation17
HOST_CXX ?= c++
.PHONY: all host-test
