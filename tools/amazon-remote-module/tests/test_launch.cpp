// SPDX-License-Identifier: GPL-2.0-only
#include "../launch_test.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
namespace {
struct FakeBackend {
    std::uint32_t query = armodule::ProcessNotFoundResult, init = 0, launch = 0;
    std::uint64_t pid = 0, now = 100;
    bool verify = true, same_hash = true, create = true, update = true;
    std::vector<unsigned> calls;
    std::uint32_t QueryProcess(std::uint64_t &out) { calls.push_back(1); out = pid; return query; }
    bool VerifyModule(std::array<std::uint8_t,32> &out) { calls.push_back(2); out = armodule::LaunchModuleHash; if (!same_hash) out[0] ^= 1; return verify; }
    std::uint64_t Now() { return now++; }
    bool CreateIntent(const armodule::LaunchRecord &record) { calls.push_back(3); assert(armodule::ValidLaunchRecord(record)); return create; }
    std::uint32_t InitializeShell() { calls.push_back(4); return init; }
    std::uint32_t LaunchDiagnosticModule(std::uint64_t &out) { calls.push_back(5); out = launch ? 0 : 123; return launch; }
    void CloseShell() { calls.push_back(6); }
    bool UpdateReceipt(const armodule::LaunchRecord &record) { calls.push_back(7); assert(armodule::ValidLaunchRecord(record)); return update; }
};
}
int main() {
    using namespace armodule;
    FakeBackend backend; OperatorLaunchTest gate; LaunchRecord record{};
    assert(gate.Request(true,backend,record) == LaunchOutcome::Ignored && backend.calls.empty());
    gate.ObserveReleased(true);
    assert(gate.Request(false,backend,record) == LaunchOutcome::Ignored && backend.calls.empty());
    assert(gate.Request(true,backend,record) == LaunchOutcome::LaunchReturned);
    assert((backend.calls == std::vector<unsigned>{1,2,3,4,5,6,7}));
    assert(record.launch_result == 0 && record.returned_pid == 123 && ValidLaunchRecord(record));
    gate.ObserveReleased(true); const auto count = backend.calls.size();
    assert(gate.Request(true,backend,record) == LaunchOutcome::Ignored && backend.calls.size() == count);
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        FakeBackend fake; OperatorLaunchTest test; LaunchRecord value{}; test.ObserveReleased(true);
        LaunchOutcome expected{}; std::vector<unsigned> wanted;
        switch (scenario) {
            case 0: fake.query = 0; fake.pid = 100; expected = LaunchOutcome::ProcessNotAbsent; wanted = {1}; break;
            case 1: fake.query = 0x123; expected = LaunchOutcome::ProcessNotAbsent; wanted = {1}; break;
            case 2: fake.pid = 99; expected = LaunchOutcome::ProcessNotAbsent; wanted = {1}; break;
            case 3: fake.verify = false; expected = LaunchOutcome::BinaryNotVerified; wanted = {1,2}; break;
            case 4: fake.same_hash = false; expected = LaunchOutcome::BinaryNotVerified; wanted = {1,2}; break;
            case 5: fake.create = false; expected = LaunchOutcome::IntentNotSaved; wanted = {1,2,3}; break;
            case 6: fake.init = 77; expected = LaunchOutcome::ServiceFailed; wanted = {1,2,3,4,7}; break;
            case 7: fake.launch = 88; expected = LaunchOutcome::LaunchReturned; wanted = {1,2,3,4,5,6,7}; break;
            case 8: fake.update = false; expected = LaunchOutcome::FinalReceiptNotSaved; wanted = {1,2,3,4,5,6,7}; break;
        }
        assert(test.Request(true,fake,value) == expected && fake.calls == wanted && test.Consumed());
        if (scenario == 6) assert(value.service_init_result == 77 && !value.launch_result);
        if (scenario == 7) assert(value.launch_result == 88 && !value.returned_pid);
        test.ObserveReleased(true); assert(test.Request(true,fake,value) == LaunchOutcome::Ignored && fake.calls == wanted);
    }
    auto bad = record; bad.module_sha256[0] ^= 1; assert(!ValidLaunchRecord(bad));
    bad = record; bad.before_query_result = 123; assert(!ValidLaunchRecord(bad));
    bad = record; bad.reserved = 1; assert(!ValidLaunchRecord(bad));
    bad = record; bad.returned_ms = 0; assert(!ValidLaunchRecord(bad));
    auto observed = record; observed.observed_ms = record.returned_ms+5000;
    observed.observed_query_result = ProcessNotFoundResult; assert(ValidLaunchRecord(observed));
    observed.observed_ms = record.returned_ms-1; assert(!ValidLaunchRecord(observed));
    observed = record; observed.observed_reserved = 1; assert(!ValidLaunchRecord(observed));
    observed = record; observed.observed_query_result = ProcessNotFoundResult; assert(!ValidLaunchRecord(observed));
    std::puts("PASS operator-only/release gate, single consumed attempt, exact absence/hash admission, durable intent before launch, raw API Results and cleanup; not hardware proof");
}
