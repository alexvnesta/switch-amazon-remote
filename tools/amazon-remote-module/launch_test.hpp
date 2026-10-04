// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>
#include "startup.hpp"

namespace armodule {
inline constexpr std::uint32_t ProcessNotFoundResult = 0x20f;
inline constexpr std::array<std::uint8_t,32> LaunchModuleHash{
    0x98,0x4a,0x81,0x3a,0xa0,0x04,0x16,0xa8,0x7d,0xc3,0x8b,0xe8,0x8d,0xe0,0x17,0xc2,
    0x7b,0x85,0x9a,0x1a,0xfb,0xd2,0x82,0x2e,0xf8,0x8a,0x92,0x90,0x08,0x9e,0x15,0x80};
inline constexpr char LaunchPath[] = "sdmc:/config/amazon-remote-module/launch-diag2.bin";
inline constexpr char LaunchTempPath[] = "sdmc:/config/amazon-remote-module/launch-diag2.tmp";
enum class LaunchStage : std::uint32_t { Intent = 1, ServiceFailed, Returned };
struct LaunchRecord {
    std::uint32_t magic{0x41524c32}, version{1}, stage{}, reserved{};
    std::uint64_t issued_ms{}, returned_ms{}, returned_pid{};
    std::uint32_t before_query_result{}, service_init_result{}, launch_result{}, hash_verified{};
    std::array<std::uint8_t,32> module_sha256{};
    std::uint64_t observed_ms{}, observed_pid{};
    std::uint32_t observed_query_result{}, observed_reserved{};
};
static_assert(sizeof(LaunchRecord) == 112 && std::is_trivially_copyable_v<LaunchRecord>);
inline bool ValidLaunchRecord(const LaunchRecord &value) {
    return value.magic == 0x41524c32 && value.version == 1 && !value.reserved && !value.observed_reserved &&
        value.stage >= unsigned(LaunchStage::Intent) && value.stage <= unsigned(LaunchStage::Returned) &&
        value.issued_ms && value.hash_verified == 1 && value.module_sha256 == LaunchModuleHash &&
        value.before_query_result == ProcessNotFoundResult &&
        (value.observed_ms ? value.returned_ms && value.observed_ms >= value.returned_ms : !value.observed_pid && !value.observed_query_result) &&
        (value.stage == unsigned(LaunchStage::Intent) ? !value.returned_ms && !value.returned_pid &&
            !value.service_init_result && !value.launch_result : value.returned_ms >= value.issued_ms &&
            (value.stage == unsigned(LaunchStage::ServiceFailed) ? value.service_init_result && !value.returned_pid &&
                !value.launch_result : !value.service_init_result));
}
enum class LaunchOutcome { Ignored, ProcessNotAbsent, BinaryNotVerified, IntentNotSaved,
    ServiceFailed, LaunchReturned, FinalReceiptNotSaved };
// One consumed operator edge, not a daemon/retry policy. Only an exact positive
// ProcessNotFound allows launch. Getter/permission failures never mean absent.
class OperatorLaunchTest {
public:
    void ObserveReleased(bool all_test_buttons_released) { if (all_test_buttons_released) released_ = true; }
    template<class Backend> LaunchOutcome Request(bool only_r_pressed, Backend &backend, LaunchRecord &record) {
        if (!only_r_pressed || !released_ || consumed_) return LaunchOutcome::Ignored;
        consumed_ = true; released_ = false;
        record = {};
        std::uint64_t existing_pid{};
        record.before_query_result = backend.QueryProcess(existing_pid);
        if (record.before_query_result != ProcessNotFoundResult || existing_pid) return LaunchOutcome::ProcessNotAbsent;
        if (!backend.VerifyModule(record.module_sha256) || record.module_sha256 != LaunchModuleHash) return LaunchOutcome::BinaryNotVerified;
        record.hash_verified = 1; record.stage = unsigned(LaunchStage::Intent); record.issued_ms = backend.Now();
        if (!record.issued_ms || !backend.CreateIntent(record)) return LaunchOutcome::IntentNotSaved;
        record.service_init_result = backend.InitializeShell();
        if (record.service_init_result) {
            record.stage = unsigned(LaunchStage::ServiceFailed); record.returned_ms = backend.Now();
            return backend.UpdateReceipt(record) ? LaunchOutcome::ServiceFailed : LaunchOutcome::FinalReceiptNotSaved;
        }
        record.launch_result = backend.LaunchDiagnosticModule(record.returned_pid);
        backend.CloseShell();
        record.stage = unsigned(LaunchStage::Returned); record.returned_ms = backend.Now();
        return backend.UpdateReceipt(record) ? LaunchOutcome::LaunchReturned : LaunchOutcome::FinalReceiptNotSaved;
    }
    bool Consumed() const { return consumed_; }
private:
    bool released_{}, consumed_{};
};
}
