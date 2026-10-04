CXX ?= clang++
CXXFLAGS = -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
ROOT := ..
SOURCES := test_hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp
all: test_hogp_client test_input_diagnostic test_libnx_input_adapter test_diagnostic_log test_ble_preflight test_advertisement_policy test_managed_observer test_intent_trace test_ble_header_trace test_scan_cache_census test_target_sighting test_capture_lease
test_hogp_client: $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@
test_input_diagnostic: test_input_diagnostic.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/input_diagnostic_policy.hpp
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@
test_libnx_input_adapter: test_libnx_input_adapter.cpp $(ROOT)/mc_mitm/source/amazon_remote/libnx_session.cpp $(ROOT)/mc_mitm/source/amazon_remote/hogp_client.cpp $(ROOT)/mc_mitm/source/amazon_remote/remote_decoder.cpp $(ROOT)/mc_mitm/source/amazon_remote/diagnostic_log.cpp $(wildcard $(ROOT)/mc_mitm/source/amazon_remote/*.hpp) fake_sdk/switch.h
	$(CXX) $(CXXFLAGS) -Ifake_sdk $(filter %.cpp,$^) -o $@
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
test_capture_lease_tsan: test_capture_lease.cpp $(ROOT)/mc_mitm/source/amazon_remote/capture_lease.hpp
	$(CXX) -std=c++20 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -pthread $< -o $@
tsan-capture-lease: test_capture_lease_tsan
	./test_capture_lease_tsan
test: all
	cd $(ROOT) && ./host_tests/test_hogp_client
	./test_input_diagnostic
	./test_libnx_input_adapter
	./test_diagnostic_log
	./test_ble_preflight
	./test_advertisement_policy
	./test_managed_observer
	./test_intent_trace
	./test_ble_header_trace
	./test_scan_cache_census
	./test_target_sighting
	./test_capture_lease
.PHONY: all test tsan-capture-lease
