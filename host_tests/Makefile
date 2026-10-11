CXX ?= clang++
all: test_boot_layout_reader
test: test-boot-layout-reader
test_boot_layout_reader: test_boot_layout_reader.cpp ../tools/amazon-remote-module/control/boot_layout_reader.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-boot-layout-reader: test_boot_layout_reader
	./test_boot_layout_reader
.PHONY: test-boot-layout-reader
all: test_deferred_reader
test: test-deferred-reader
test_deferred_reader: test_deferred_reader.cpp $(wildcard ../tools/amazon-remote-module/control/deferred_reader*.hpp) $(wildcard ../tools/driver-capability-p1/*.hpp) ../tools/amazon-remote-module/control/observation_checkpoint.hpp ../tools/amazon-remote-module/control/observation_checkpoint_io.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-deferred-reader: test_deferred_reader
	./test_deferred_reader
.PHONY: test-deferred-reader
all: test_deferred_runtime
test: test-deferred-runtime
test_deferred_runtime: test_deferred_runtime.cpp ../mc_mitm/source/amazon_remote/deferred_read_runtime.cpp ../mc_mitm/source/amazon_remote/deferred_read_runtime.hpp $(wildcard deferred_sdk/*) $(wildcard ../tools/driver-capability-p1/*.hpp)
	$(CXX) $(CXXFLAGS) -DARMODULE_CONTROLLER_DEFERRED_D1 -DARMODULE_DEFERRED_HOST_TEST -Ideferred_sdk $< ../mc_mitm/source/amazon_remote/deferred_read_runtime.cpp -o $@
test-deferred-runtime: test_deferred_runtime
	@set -e; for mode in not-ready no-events queue-full reply-before reply-after timeout-queued timeout-native error; do ./test_deferred_runtime $$mode; done
.PHONY: test-deferred-runtime
all: test_deferred_read_state
test: test-deferred-read-state
test_deferred_read_state: test_deferred_read_state.cpp ../tools/driver-capability-p1/deferred_read_state.hpp ../tools/driver-capability-p1/deferred_transport.hpp ../tools/driver-capability-p1/deferred_endpoint.hpp ../tools/driver-capability-p1/deferred_capture.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-deferred-read-state: test_deferred_read_state
	./test_deferred_read_state
.PHONY: test-deferred-read-state
all: test_deferred_endpoint
test: test-deferred-endpoint
test_deferred_endpoint: test_deferred_endpoint.cpp ../tools/driver-capability-p1/deferred_endpoint.hpp ../tools/driver-capability-p1/deferred_capture.hpp ../mc_mitm/source/amazon_remote/controller_passive.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-deferred-endpoint: test_deferred_endpoint
	./test_deferred_endpoint
.PHONY: test-deferred-endpoint
all: test_deferred_capture
test: test-deferred-capture
test_deferred_capture: test_deferred_capture.cpp ../tools/driver-capability-p1/deferred_capture.hpp ../mc_mitm/source/amazon_remote/controller_passive.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test-deferred-capture: test_deferred_capture
	./test_deferred_capture
.PHONY: test-deferred-capture
all: test_passive_read_flow
test: test-passive-read-flow
test_passive_read_flow: test_passive_read_flow.cpp ../tools/amazon-remote-module/control/passive_read_flow.hpp ../tools/amazon-remote-module/control/passive_read_io.hpp ../tools/amazon-remote-module/control/observation_checkpoint.hpp ../tools/amazon-remote-module/control/observation_checkpoint_io.hpp ../mc_mitm/source/amazon_remote/controller_passive_cache.hpp ../mc_mitm/source/amazon_remote/controller_passive.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-passive-read-flow: test_passive_read_flow
	./test_passive_read_flow
.PHONY: test-passive-read-flow
all: test_controller_passive_cache
test: test-controller-passive-cache
test_controller_passive_cache: test_controller_passive_cache.cpp ../mc_mitm/source/amazon_remote/controller_passive_runtime.cpp ../mc_mitm/source/amazon_remote/controller_passive_runtime.hpp ../mc_mitm/source/amazon_remote/controller_passive_cache.hpp ../mc_mitm/source/amazon_remote/controller_passive.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp passive_sdk/stratosphere.hpp passive_sdk/switch.h
	$(CXX) $(CXXFLAGS) -pthread -DARMODULE_CONTROLLER_PASSIVE_P1 -Ipassive_sdk $< ../mc_mitm/source/amazon_remote/controller_passive_runtime.cpp -o $@
test-controller-passive-cache: test_controller_passive_cache
	./test_controller_passive_cache
.PHONY: test-controller-passive-cache
all: test_controller_passive
test: test-controller-passive
test_controller_passive: test_controller_passive.cpp ../mc_mitm/source/amazon_remote/controller_passive.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-controller-passive: test_controller_passive
	./test_controller_passive
.PHONY: test-controller-passive
all: test_observation_checkpoint
test: test-observation-checkpoint
test_observation_checkpoint: test_observation_checkpoint.cpp ../tools/amazon-remote-module/control/observation_checkpoint.hpp ../tools/amazon-remote-module/control/observation_checkpoint_io.hpp ../tools/amazon-remote-module/io.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-observation-checkpoint: test_observation_checkpoint
	./test_observation_checkpoint
.PHONY: test-observation-checkpoint
all: test_controller_query
all: test_controller_query_isolation
test: test-controller-query-isolation
test_controller_query_isolation: test_controller_query_isolation.cpp ../tools/amazon-remote-module/control/controller_query_isolation.hpp ../tools/amazon-remote-module/control/controller_query_report.hpp $(wildcard ../tools/amazon-remote-module/*.hpp) $(wildcard ../mc_mitm/source/amazon_remote/*.hpp)
	$(CXX) $(CXXFLAGS) $< -o $@
test-controller-query-isolation: test_controller_query_isolation
	./test_controller_query_isolation
.PHONY: test-controller-query-isolation
all: test_controller_query_runtime
test: test-controller-query-runtime
test_controller_query_runtime: test_controller_query_runtime.cpp ../mc_mitm/source/amazon_remote/controller_query_runtime.cpp ../mc_mitm/source/amazon_remote/controller_query.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-controller-query-runtime: test_controller_query_runtime
	./test_controller_query_runtime
.PHONY: test-controller-query-runtime
all: test_controller_query_report
test: test-controller-query-report
test_controller_query_report: test_controller_query_report.cpp ../tools/amazon-remote-module/control/controller_query_report.hpp $(wildcard ../tools/amazon-remote-module/*.hpp) ../mc_mitm/source/amazon_remote/controller_query.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-controller-query-report: test_controller_query_report
	./test_controller_query_report
.PHONY: test-controller-query-report
test: test-controller-query
test_controller_query: test_controller_query.cpp ../mc_mitm/source/amazon_remote/controller_query.hpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-controller-query: test_controller_query
	./test_controller_query
.PHONY: test-controller-query
all: test_controller_capabilities
test: test-controller-capabilities
test_controller_capabilities: test_controller_capabilities.cpp ../mc_mitm/source/amazon_remote/controller_capabilities.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test-controller-capabilities: test_controller_capabilities
	./test_controller_capabilities
.PHONY: test-controller-capabilities
all: test_wake_session test_wake_central
test: test-wake-a3
test_wake_session: test_wake_session.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp)
	$(CXX) $(CXXFLAGS) -DARMODULE_WAKE_A3 $< -o $@
test_wake_central: test_wake_central.cpp test_central_bond_runtime.cpp test_bond_save_io.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/central_bond_runtime.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_WAKE_A3 -DARMODULE_NATIVE_HCI_C2 -Icentral_bond_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test-wake-a3: test_wake_session test_wake_central
	./test_wake_session
	@set -e; for mode in cycles live pending-stop stop shutdown bad-save; do ./test_wake_central $$mode; done
.PHONY: test-wake-a3
WAKE_FLAGS := -DARMODULE_WAKE_A3 -DARMODULE_AUTOMATIC_A2 -DARMODULE_AUTOMATIC_A1 -DARMODULE_NATIVE_HCI_C2 -DARMODULE_NATIVE_PEER_C1 -DARMODULE_CENTRAL_BOND_B1 -DARMODULE_CONNECT_DISPATCH -DARMODULE_DISCONNECT_CHECK -DARMODULE_STARTUP_TRACE -DARMODULE_DISCOVERY_TRACE -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12
test_retry_transport: test_retry_transport.cpp test_libnx_input_adapter.cpp fake_sdk/switch.h $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_RETRY_A5 -DARMODULE_STARTUP_A4 $(WAKE_FLAGS) -Ifake_sdk $< $(filter ../%.cpp,$^) -o $@
all: test_retry_transport
test: test-retry-a5
test-retry-a5: test_retry_transport
	./test_retry_transport
.PHONY: test-retry-a5
test_latency_transport: test_retry_transport.cpp test_libnx_input_adapter.cpp fake_sdk/switch.h $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_LATENCY_A6 -DARMODULE_RETRY_A5 -DARMODULE_STARTUP_A4 $(WAKE_FLAGS) -Ifake_sdk $< $(filter ../%.cpp,$^) -o $@
test_compact_log: test_compact_log.cpp ../tools/amazon-remote-module/compact_log.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
all: test_latency_transport test_compact_log
test: test-latency-a6
test-latency-a6: test_latency_transport test_compact_log
	./test_latency_transport
	./test_compact_log
.PHONY: test-latency-a6
test_hid_information: test_hid_information.cpp test_libnx_input_adapter.cpp fake_sdk/switch.h $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_HID_INFO_H1 -DARMODULE_LATENCY_A6 -DARMODULE_RETRY_A5 -DARMODULE_STARTUP_A4 $(WAKE_FLAGS) -Ifake_sdk $< $(filter ../%.cpp,$^) -o $@
all: test_hid_information
test: test-hid-information
test-hid-information: test_hid_information
	./test_hid_information
.PHONY: test-hid-information
test_wake_transport: test_wake_transport.cpp test_libnx_input_adapter.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) $(WAKE_FLAGS) -Ifake_sdk $< $(filter ../%.cpp,$^) -o $@
test-wake-transport: test_wake_transport
	./test_wake_transport
all: test_wake_transport
test: test-wake-transport
# SETUP R3: unfinished-attempt sleep retirement, plus A3 regressions built with R3.
R3_CENTRAL_DEPS := test_central_bond_runtime.cpp test_bond_save_io.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/central_bond_runtime.cpp
R3_TRANSPORT_DEPS := test_libnx_input_adapter.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
test_wake_session_r3: test_wake_session.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp)
	$(CXX) $(CXXFLAGS) -DARMODULE_WAKE_A3 -DARMODULE_SETUP_SLEEP_R3 $< -o $@
test_setup_r3_central: test_setup_sleep_r3_central.cpp $(R3_CENTRAL_DEPS)
	$(CXX) $(CXXFLAGS) -DARMODULE_WAKE_A3 -DARMODULE_NATIVE_HCI_C2 -DARMODULE_SETUP_SLEEP_R3 -Icentral_bond_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test_wake_central_r3: test_wake_central.cpp $(R3_CENTRAL_DEPS)
	$(CXX) $(CXXFLAGS) -DARMODULE_WAKE_A3 -DARMODULE_NATIVE_HCI_C2 -DARMODULE_SETUP_SLEEP_R3 -Icentral_bond_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test_setup_r3_transport: test_setup_sleep_r3_transport.cpp $(R3_TRANSPORT_DEPS)
	$(CXX) $(CXXFLAGS) $(WAKE_FLAGS) -DARMODULE_SETUP_SLEEP_R3 -Ifake_sdk $< $(filter ../%.cpp,$^) -o $@
test_wake_transport_r3: test_wake_transport.cpp $(R3_TRANSPORT_DEPS)
	$(CXX) $(CXXFLAGS) $(WAKE_FLAGS) -DARMODULE_SETUP_SLEEP_R3 -Ifake_sdk $< $(filter ../%.cpp,$^) -o $@
test-setup-r3: test_wake_session_r3 test_setup_r3_central test_wake_central_r3 test_setup_r3_transport test_wake_transport_r3
	./test_wake_session_r3
	@set -e; for mode in cycles live stop shutdown unprepared attempted mixed; do ./test_setup_r3_central $$mode; done
	@set -e; for mode in cycles live pending-stop stop shutdown bad-save; do ./test_wake_central_r3 $$mode; done
	./test_setup_r3_transport
	./test_wake_transport_r3
.PHONY: test-setup-r3
all: test_wake_session_r3 test_setup_r3_central test_wake_central_r3 test_setup_r3_transport test_wake_transport_r3
test: test-setup-r3
.PHONY: test-wake-transport
all: test_auto_a1_transport
test: test-auto-a1
test_auto_a1_transport: test_saved_peer.cpp test_libnx_input_adapter.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_AUTOMATIC_A1 -DARMODULE_NATIVE_HCI_C2 -DARMODULE_NATIVE_PEER_C1 -DARMODULE_CENTRAL_BOND_B1 -DARMODULE_CONNECT_DISPATCH -DARMODULE_DISCONNECT_CHECK -DARMODULE_STARTUP_TRACE -DARMODULE_DISCOVERY_TRACE -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk test_saved_peer.cpp $(filter ../%.cpp,$^) -o $@
test-auto-a1: test_auto_a1_transport
	./test_auto_a1_transport
.PHONY: test-auto-a1
all: test_hci_status_trace test_hci_c2_runtime test_hci_c2_transport
test: test-hci-c2
test_hci_status_trace: test_hci_status_trace.cpp ../mc_mitm/source/amazon_remote/hci_status_trace.hpp ../mc_mitm/source/amazon_remote/central_bond_wire.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_hci_c2_runtime: test_central_bond_runtime.cpp test_bond_save_io.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/central_bond_runtime.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_NATIVE_HCI_C2 -Icentral_bond_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test_hci_c2_transport: test_saved_peer.cpp test_libnx_input_adapter.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) -DARMODULE_NATIVE_HCI_C2 -DARMODULE_NATIVE_PEER_C1 -DARMODULE_CENTRAL_BOND_B1 -DARMODULE_CONNECT_DISPATCH -DARMODULE_DISCONNECT_CHECK -DARMODULE_STARTUP_TRACE -DARMODULE_DISCOVERY_TRACE -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk test_saved_peer.cpp $(filter ../%.cpp,$^) -o $@
test-hci-c2: test_hci_status_trace test_hci_c2_runtime test_hci_c2_transport
	./test_hci_status_trace
	@set -e; for mode in reconnect reconnect-fail peer-new peer-existing peer-native-fail peer-old-bridge peer-hci peer-ready-expired peer-ready-ended c2-empty; do ./test_hci_c2_runtime $$mode; done
	./test_hci_c2_transport
.PHONY: test-hci-c2
all: test_saved_peer
test_saved_peer: test_saved_peer.cpp test_libnx_input_adapter.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp ../tools/amazon-remote-module/control/saved_discovery_view.hpp fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_NATIVE_PEER_C1 -DARMODULE_CENTRAL_BOND_B1 -DARMODULE_CONNECT_DISPATCH -DARMODULE_DISCONNECT_CHECK -DARMODULE_STARTUP_TRACE -DARMODULE_DISCOVERY_TRACE -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk test_saved_peer.cpp $(filter ../%.cpp,$^) -o $@
test: test-saved-peer
test-saved-peer: test_saved_peer
	./test_saved_peer
.PHONY: test-saved-peer
all: test_saved_discovery
test_saved_discovery: test_saved_discovery.cpp test_libnx_input_adapter.cpp $(wildcard ../mc_mitm/source/amazon_remote/*.hpp) ../mc_mitm/source/amazon_remote/libnx_session.cpp ../mc_mitm/source/amazon_remote/hogp_client.cpp ../mc_mitm/source/amazon_remote/remote_decoder.cpp ../mc_mitm/source/amazon_remote/diagnostic_log.cpp ../tools/amazon-remote-module/control/saved_discovery_view.hpp fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_SAVED_DISCOVERY_B3 -DARMODULE_CENTRAL_BOND_B1 -DARMODULE_CONNECT_DISPATCH -DARMODULE_DISCONNECT_CHECK -DARMODULE_STARTUP_TRACE -DARMODULE_DISCOVERY_TRACE -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk test_saved_discovery.cpp $(filter ../%.cpp,$^) -o $@
test: test-saved-discovery
test-saved-discovery: test_saved_discovery
	./test_saved_discovery
.PHONY: test-saved-discovery
all: test_power_lifecycle
test_power_lifecycle: test_power_lifecycle.cpp $(wildcard ../mc_mitm/source/amazon_remote/*power*.hpp) ../mc_mitm/source/amazon_remote/capture_lease.hpp ../mc_mitm/source/amazon_remote/capture_diagnostics.hpp ../tools/amazon-remote-module/control/capture_preflight.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test: test-power-lifecycle
test-power-lifecycle: test_power_lifecycle
	./test_power_lifecycle
.PHONY: test-power-lifecycle
all: test_central_bond_runtime
test_central_bond_runtime: test_central_bond_runtime.cpp test_bond_save_io.cpp central_bond_sdk/switch.h central_bond_sdk/stratosphere.hpp $(wildcard ../mc_mitm/source/amazon_remote/central_bond*) $(wildcard ../mc_mitm/source/amazon_remote/bond_*.hpp) ../mc_mitm/source/amazon_remote/legacy_encryption_request.hpp
	$(CXX) $(CXXFLAGS) -Icentral_bond_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test: test-central-bond-runtime
test-central-bond-runtime: test_central_bond_runtime
	@set -e; for mode in enroll reconnect reconnect-fail cancel disconnect timeout suspend missing-key duplicate-key invalid-record idle-power power-live power-attempted power-terminal; do ./test_central_bond_runtime $$mode; done
	@set -e; for mode in peer-new peer-existing peer-live peer-cancel-queued peer-send-fail peer-native-fail peer-secret-rejected peer-wrong-type peer-wrong-address-type peer-invalid-existed peer-wrong-nonce peer-wrong-target peer-timeout peer-lease-end peer-suspend peer-link-event peer-ready-expired peer-ready-ended; do ./test_central_bond_runtime $$mode; done
.PHONY: test-central-bond-runtime
all: test_native_metadata
all: test_bond_properties
test_bond_properties: test_bond_properties.cpp ../mc_mitm/source/amazon_remote/bond_properties.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test: test-bond-properties
test-bond-properties: test_bond_properties
	./test_bond_properties
.PHONY: test-bond-properties
test_native_metadata: test_native_metadata.cpp ../mc_mitm/source/amazon_remote/native_metadata.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test: test-native-metadata
test-native-metadata: test_native_metadata
	./test_native_metadata
.PHONY: test-native-metadata
CXXFLAGS = -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
ROOT := ..
all: test_sleep_resume_trace
test: test-sleep-resume-trace
test_sleep_resume_trace: test_sleep_resume_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/sleep_resume_trace_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/sleep_resume_trace.hpp $(ROOT)/mc_mitm/source/amazon_remote/sleep_resume_trace_runtime.hpp $(wildcard sleep_sdk/*)
	$(CXX) $(CXXFLAGS) -pthread -Isleep_sdk test_sleep_resume_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/sleep_resume_trace_runtime.cpp -o $@
test-sleep-resume-trace: test_sleep_resume_trace
	@set -e; for mode in core disabled invalid success existing create-fail write-fail open-fail corrupt short ack-fail; do ./test_sleep_resume_trace $$mode; done
.PHONY: test-sleep-resume-trace
all: test_intent_header_runtime
test: test-intent-header-runtime
test_intent_header_runtime: test_intent_header_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/intent_trace_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/intent_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/ble_header_trace.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*trace*.hpp) $(wildcard intent_sdk/*) $(wildcard filter_sdk/*)
	$(CXX) $(CXXFLAGS) -Iintent_sdk test_intent_header_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/intent_trace_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/intent_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/ble_header_trace.cpp -o $@
test-intent-header-runtime: test_intent_header_runtime
	./test_intent_header_runtime disabled
	./test_intent_header_runtime enabled
.PHONY: test-intent-header-runtime
SOURCES := test_hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp
all: test_hogp_client test_input_diagnostic test_libnx_input_adapter test_libnx_census_adapter test_diagnostic_log test_ble_preflight test_advertisement_policy test_managed_observer test_intent_trace test_ble_header_trace test_scan_cache_census test_target_sighting test_capture_lease test_connection_evidence test_core_security test_session_budget test_bond_check test_pairing_audit test_enrollment_context
all: test_core_security_reader
all: test_bond_transaction
all: test_legacy_bond_material
all: test_bond_save_io
all: test_bond_record test_bond_store
all: test_bond_candidate
all: test_legacy_bond_reload
all: test_legacy_native_confirmation
all: test_legacy_encryption_request
test_legacy_encryption_request: test_legacy_encryption_request.cpp $(ROOT)/mc_mitm/source/amazon_remote/legacy_encryption_request.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: test-legacy-encryption-request
test-legacy-encryption-request: test_legacy_encryption_request
	./test_legacy_encryption_request
.PHONY: test-legacy-encryption-request
test_legacy_native_confirmation: test_legacy_native_confirmation.cpp bond_record_fixture.hpp bond_host_sha256.hpp bond_save_sdk/switch.h $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/bond_*.hpp) $(ROOT)/mc_mitm/source/amazon_remote/legacy_bond_material.hpp $(ROOT)/mc_mitm/source/amazon_remote/legacy_native_confirmation.hpp
	$(CXX) $(CXXFLAGS) -Ibond_save_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test: test-legacy-native-confirmation
test-legacy-native-confirmation: test_legacy_native_confirmation
	./test_legacy_native_confirmation
.PHONY: test-legacy-native-confirmation
test_legacy_bond_reload: test_legacy_bond_reload.cpp bond_record_fixture.hpp bond_host_sha256.hpp bond_save_sdk/switch.h $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/bond_*.hpp) $(ROOT)/mc_mitm/source/amazon_remote/legacy_bond_material.hpp $(ROOT)/mc_mitm/source/amazon_remote/legacy_bond_reload.hpp
	$(CXX) $(CXXFLAGS) -Ibond_save_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test: test-legacy-bond-reload
test-legacy-bond-reload: test_legacy_bond_reload
	./test_legacy_bond_reload
.PHONY: test-legacy-bond-reload
HOST_CRYPTO_LIBS := $(if $(filter Darwin,$(shell uname -s)),,-lcrypto)
BOND_HEADERS := $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/bond_*.hpp) $(ROOT)/mc_mitm/source/amazon_remote/legacy_bond_material.hpp
test_bond_candidate: test_bond_candidate.cpp test_bond_save_io.cpp bond_record_fixture.hpp bond_host_sha256.hpp bond_save_sdk/switch.h $(BOND_HEADERS)
	$(CXX) $(CXXFLAGS) -Ibond_save_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test: test-bond-candidate
test-bond-candidate: test_bond_candidate
	./test_bond_candidate
.PHONY: test-bond-candidate
test_bond_record: test_bond_record.cpp bond_record_fixture.hpp bond_host_sha256.hpp bond_save_sdk/switch.h $(BOND_HEADERS)
	$(CXX) $(CXXFLAGS) -Ibond_save_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test_bond_store: test_bond_store.cpp test_bond_save_io.cpp bond_record_fixture.hpp bond_host_sha256.hpp bond_save_sdk/switch.h $(BOND_HEADERS)
	$(CXX) $(CXXFLAGS) -Ibond_save_sdk $< $(HOST_CRYPTO_LIBS) -o $@
test: test-bond-record-store
test-bond-record-store: test_bond_record test_bond_store
	./test_bond_record
	./test_bond_store
.PHONY: test-bond-record-store
test_bond_save_io: test_bond_save_io.cpp bond_save_sdk/switch.h $(ROOT)/mc_mitm/source/amazon_remote/bond_save_io.hpp $(ROOT)/mc_mitm/source/amazon_remote/legacy_bond_material.hpp
	$(CXX) $(CXXFLAGS) -Ibond_save_sdk $< -o $@
test: test-bond-save-io
test-bond-save-io: test_bond_save_io
	./test_bond_save_io
.PHONY: test-bond-save-io
test_legacy_bond_material: test_legacy_bond_material.cpp $(ROOT)/mc_mitm/source/amazon_remote/legacy_bond_material.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: test-legacy-bond-material
test-legacy-bond-material: test_legacy_bond_material
	./test_legacy_bond_material
.PHONY: test-legacy-bond-material
test_bond_transaction: test_bond_transaction.cpp $(ROOT)/mc_mitm/source/amazon_remote/bond_transaction.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: test-bond-transaction
test-bond-transaction: test_bond_transaction
	./test_bond_transaction
.PHONY: test-bond-transaction
all: test_driver_state_reader
all: test_native_bond_trial
all: test_setup_view
all: test_native_bond_backend
all: test_libnx_native_owner
all: test_native_postcheck test_postcheck_backend
all: test_reconnect_policy test_libnx_reconnect
all: test_libnx_known_reconnect
all: test_libnx_scan_assisted
all: test_libnx_manufacturer_scan
all: test_libnx_connect_dispatch
test_libnx_connect_dispatch: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_CONNECT_DISPATCH -DARMODULE_DISCONNECT_CHECK -DARMODULE_STARTUP_TRACE -DARMODULE_DISCOVERY_TRACE -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk $(filter %.cpp,$^) -o $@
test: test-connect-dispatch
test-connect-dispatch: test_libnx_connect_dispatch
	./test_libnx_connect_dispatch
all: test_arm_startup_diagnostic
all: test_capture_preflight
test_capture_preflight: test_capture_preflight.cpp $(ROOT)/tools/amazon-remote-module/control/capture_preflight.hpp $(ROOT)/tools/amazon-remote-module/control/setup_view.hpp $(ROOT)/mc_mitm/source/amazon_remote/capture_diagnostics.hpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test: test-capture-preflight
test-capture-preflight: test_capture_preflight
	./test_capture_preflight
all: test_manufacturer_filter_correction
all: test_filter_correction_runtime
all: test_lease_scan_trace_runtime
test_lease_scan_trace_runtime: test_lease_scan_trace_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/lease_scan_trace_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/intent_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/ble_header_trace.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) filter_sdk/stratosphere.hpp filter_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ifilter_sdk $(filter %.cpp,$^) -o $@
test: test-lease-scan-trace-runtime
test-lease-scan-trace-runtime: test_lease_scan_trace_runtime
	@for scenario in 0 1 2 3 4 5 6 7 8 9 10 11; do ./test_lease_scan_trace_runtime $$scenario || exit $$?; done
test_filter_correction_runtime: test_filter_correction_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/filter_correction_runtime.cpp $(ROOT)/mc_mitm/source/amazon_remote/manufacturer_filter_correction.hpp filter_sdk/stratosphere.hpp filter_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ifilter_sdk $(filter %.cpp,$^) -o $@
test: test-filter-correction-runtime
test-filter-correction-runtime: test_filter_correction_runtime
	@for scenario in 0 1 2 3 4 5 6 7 8 9 10; do ./test_filter_correction_runtime $$scenario || exit $$?; done
test_manufacturer_filter_correction: test_manufacturer_filter_correction.cpp $(ROOT)/mc_mitm/source/amazon_remote/manufacturer_filter_correction.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test: test-manufacturer-filter-correction
test-manufacturer-filter-correction: test_manufacturer_filter_correction
	./test_manufacturer_filter_correction
test_arm_startup_diagnostic: test_arm_startup_diagnostic.cpp $(ROOT)/tools/amazon-remote-module/arm_startup_diagnostic.hpp $(ROOT)/tools/amazon-remote-module/control/setup_view.hpp
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15 $< -o $@
test: test-arm-startup
test-arm-startup: test_arm_startup_diagnostic
	./test_arm_startup_diagnostic
test_libnx_manufacturer_scan: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_MANUFACTURER_SCAN_CHECK -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk $(filter %.cpp,$^) -o $@
test_libnx_scan_assisted: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_SCAN_ASSISTED_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk $(filter %.cpp,$^) -o $@
test_libnx_known_reconnect: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_KNOWN_RECONNECT_CHECK -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -Ifake_sdk test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp -o $@
all: test_discovery_diagnostic
all: test_receiver_diagnostic
test_receiver_diagnostic: test_receiver_diagnostic.cpp $(ROOT)/mc_mitm/source/amazon_remote/receiver_diagnostic.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test_discovery_diagnostic: test_discovery_diagnostic.cpp $(ROOT)/tools/amazon-remote-module/discovery_diagnostic.hpp $(ROOT)/mc_mitm/source/amazon_remote/target_sighting.hpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_reconnect_policy: test_reconnect_policy.cpp $(ROOT)/tools/amazon-remote-module/protocol.hpp
	$(CXX) $(CXXFLAGS) -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15 $< -o $@
test_libnx_reconnect: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) $(wildcard $(ROOT)/tools/amazon-remote-pairing-audit/native_bond*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_RECONNECT_CHECK -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15_SYSTEM -DARMODULE_NAV15 -DARMODULE_NAV14 -DARMODULE_NAV13 -DARMODULE_NAV12 -Ifake_sdk $(filter %.cpp,$^) -o $@
test_native_postcheck: test_native_postcheck.cpp postcheck_fixture.hpp $(ROOT)/tools/amazon-remote-pairing-audit/native_postcheck.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV15 -Ipairing_audit_sdk $< -o $@
test_postcheck_backend: test_enrollment_context.cpp postcheck_fixture.hpp $(ROOT)/tools/amazon-remote-pairing-audit/native_postcheck.hpp $(ROOT)/tools/amazon-remote-pairing-audit/backend.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DPOSTCHECK_TEST -DARMODULE_NAV15 -Ipairing_audit_sdk $< -o $@
test_libnx_native_owner: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) $(wildcard $(ROOT)/tools/amazon-remote-pairing-audit/native_bond*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_NATIVE_OWNER -DARMODULE_NAV17 -DARMODULE_NAV16 -DARMODULE_NAV15 -Ifake_sdk $(filter %.cpp,$^) -o $@
test_native_bond_backend: test_native_bond_backend.cpp $(ROOT)/tools/amazon-remote-pairing-audit/native_bond_backend.hpp $(ROOT)/tools/amazon-remote-pairing-audit/native_guard_diagnostic.hpp $(ROOT)/tools/amazon-remote-pairing-audit/native_bond_guard.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ipairing_audit_sdk $< -o $@
test_setup_view: test_setup_view.cpp $(ROOT)/tools/amazon-remote-module/control/setup_view.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_native_bond_trial: test_native_bond_trial.cpp $(ROOT)/tools/amazon-remote-pairing-audit/native_bond_trial.hpp $(ROOT)/tools/amazon-remote-pairing-audit/native_bond_guard.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ipairing_audit_sdk $< -o $@
test_driver_state_reader: test_driver_state_reader.cpp $(ROOT)/tools/amazon-remote-pairing-audit/driver_state_reader.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ipairing_audit_sdk $< -o $@
test_core_security_reader: test_core_security_reader.cpp $(ROOT)/tools/amazon-remote-pairing-audit/core_security_reader.hpp $(ROOT)/mc_mitm/source/amazon_remote/core_security_evidence.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ipairing_audit_sdk $< -o $@
test_enrollment_context: test_enrollment_context.cpp $(ROOT)/tools/amazon-remote-pairing-audit/audit.hpp $(ROOT)/tools/amazon-remote-pairing-audit/context.hpp $(ROOT)/tools/amazon-remote-pairing-audit/backend.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ipairing_audit_sdk $< -o $@
test_pairing_audit: test_pairing_audit.cpp $(ROOT)/tools/amazon-remote-pairing-audit/audit.hpp $(ROOT)/tools/amazon-remote-pairing-audit/context.hpp $(ROOT)/tools/amazon-remote-pairing-audit/backend.hpp pairing_audit_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ipairing_audit_sdk $< -o $@
test_bond_check: test_bond_check.cpp $(ROOT)/tools/amazon-remote-bond-check/check.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_session_budget: test_session_budget.cpp $(ROOT)/mc_mitm/source/amazon_remote/session_budget.hpp $(ROOT)/mc_mitm/source/amazon_remote/health_log_gate.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_hogp_client: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@
test_input_diagnostic: test_input_diagnostic.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/input_diagnostic_policy.hpp
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@
test_libnx_input_adapter: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ifake_sdk $(filter %.cpp,$^) -o $@
test_libnx_census_adapter: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -DARMODULE_NAV17 -Ifake_sdk $(filter %.cpp,$^) -o $@
test_diagnostic_log: test_diagnostic_log.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@
test_ble_preflight: test_ble_preflight.cpp $(ROOT)/mc_mitm/source/amazon_remote/ble_preflight.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_advertisement_policy: test_advertisement_policy.cpp $(ROOT)/mc_mitm/source/amazon_remote/advertisement_policy.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@
test_managed_observer: test_managed_observer.cpp $(ROOT)/mc_mitm/source/amazon_remote/managed_observer.cpp
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
test_intent_trace: test_intent_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/intent_trace.cpp
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
test_ble_header_trace: test_ble_header_trace.cpp $(ROOT)/mc_mitm/source/amazon_remote/ble_header_trace.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@
test_scan_cache_census: test_scan_cache_census.cpp $(ROOT)/mc_mitm/source/amazon_remote/scan_cache_census.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
test_target_sighting: test_target_sighting.cpp $(ROOT)/mc_mitm/source/amazon_remote/target_sighting.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test_capture_lease: test_capture_lease.cpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test_connection_evidence: test_connection_evidence.cpp $(ROOT)/mc_mitm/source/amazon_remote/connection_evidence.hpp $(ROOT)/mc_mitm/source/amazon_remote/target_sighting.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test_core_security: test_core_security.cpp $(ROOT)/mc_mitm/source/amazon_remote/core_security_evidence.hpp $(ROOT)/mc_mitm/source/amazon_remote/target_sighting.hpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) $(CXXFLAGS) -pthread $< -o $@
test_core_security_tsan: test_core_security.cpp $(ROOT)/mc_mitm/source/amazon_remote/core_security_evidence.hpp $(ROOT)/mc_mitm/source/amazon_remote/target_sighting.hpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -pthread $< -o $@
tsan-core-security: test_core_security_tsan
	./test_core_security_tsan
test_capture_lease_tsan: test_capture_lease.cpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -pthread $< -o $@
tsan-capture-lease: test_capture_lease_tsan
	./test_capture_lease_tsan
test: all
	./test_receiver_diagnostic
	cd $(ROOT) && ./host_tests/test_hogp_client
	./test_input_diagnostic
	./test_libnx_input_adapter
	./test_libnx_census_adapter
	./test_libnx_native_owner
	./test_native_postcheck
	./test_postcheck_backend
	./test_diagnostic_log
	./test_ble_preflight
	./test_advertisement_policy
	./test_managed_observer
	./test_intent_trace
	./test_ble_header_trace
	./test_scan_cache_census
	./test_target_sighting
	./test_capture_lease
	./test_connection_evidence
	./test_core_security
	./test_core_security_reader
	./test_driver_state_reader
	./test_native_bond_trial
	./test_native_bond_backend
	./test_setup_view
	./test_reconnect_policy
	./test_discovery_diagnostic
	./test_libnx_reconnect
	./test_libnx_known_reconnect
	./test_libnx_scan_assisted
	./test_libnx_manufacturer_scan
	./test_session_budget
	./test_bond_check
	./test_pairing_audit
	./test_enrollment_context
.PHONY: all test tsan-capture-lease tsan-core-security
