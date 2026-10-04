// Independent passive event observer. GPL-2.0-only.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {
using ManagedAddress = std::array<std::uint8_t, 6>;
enum class TargetCaptureMode { StrictSdk, DiagnosticAllowDeviceZero };
struct RawAdvertisement {
    std::uint8_t size{}, type{};
    std::array<std::uint8_t, 29> data{};
};
static_assert(sizeof(RawAdvertisement) == 31);
struct ManagedRecord {
    std::uint32_t event_type{}, result{}, connection_id{};
    std::uint8_t status{}, device_type{}, address_type{}, client_if{}, count{};
    ManagedAddress address{};
    std::uint16_t reason{};
    std::int32_t rssi{};
    // Opt-in capture data can NEVER serve as pair/output/identity authorization.
    bool diagnostic_only{}, device_type_exception{};
    std::array<RawAdvertisement, 10> ads{};
};
struct ManagedObserverCounters {
    std::uint32_t seen{}, disabled{}, unconfigured{}, unknown{}, short_input{}, invalid_input{};
    std::uint32_t invalid_fields{}, other_address{}, target_hits{}, rejected_counts{}, queued{}, drops{};
    // Scan-only, complete-view diagnostics. All non-status2 scans increment
    // status_not_newdevice. The other three predicates apply ONLY to status2
    // discoveries and overlap; their sum is not a rejected-event count.
    std::uint32_t nonzero_result{}, status_not_newdevice{}, device_type_bad{}, address_type_bad{};
    std::uint32_t diagnostic_zero_captured{};
    std::array<std::uint32_t, 14> type_seen{};
    std::array<std::uint32_t, 256> scan_status_seen{};
};
// Exactly six colon-separated two-digit hex octets, optionally LF or CRLF;
// output unchanged on failure.
bool StrictParseAddress(const char *text, std::size_t length, ManagedAddress &out);
class ManagedObserver {
public:
    ManagedObserver();
    static constexpr std::size_t Capacity = 16;
    static constexpr std::uint32_t ScanEventType = 6, ConnectionEventType = 4;
    static constexpr std::size_t ScanBytes = 328, ConnectionBytes = 20;
    // Must be called once, before any Observe call/producer starts. No reconfigure.
    // Default unchanged. The opt-in mode ONLY captures raw target diagnostics;
    // it does not assert device0 means BLE or repair BTM's separate filtering.
    bool ConfigureTarget(const ManagedAddress &target, TargetCaptureMode mode = TargetCaptureMode::StrictSdk);
    // An observation already in flight may finish after disable. No input writes.
    void SetEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }
    // One producer only. Bounded work, no heap/locks/IPC/logging. Input untouched.
    bool Observe(std::uint32_t event_type, const std::uint8_t *data, std::size_t length);
    // One consumer only. Can drain already captured records after disabling.
    bool Pop(ManagedRecord &out);
    // Independent atomic counts; snapshot is approximate while producer runs.
    ManagedObserverCounters Snapshot() const;
    // Avoid a large return-value temporary on the sysmodule's small Main stack.
    void SnapshotInto(ManagedObserverCounters &out) const;
private:
    bool Enqueue(const ManagedRecord &record);
    ManagedAddress target_{};
    TargetCaptureMode capture_mode_{TargetCaptureMode::StrictSdk};
    std::atomic<bool> target_ready_{false}, observed_{false}, enabled_{false};
    std::array<ManagedRecord, Capacity> records_{};
    std::atomic<std::uint32_t> head_{0}, tail_{0};
    std::atomic<std::uint32_t> seen_{0}, disabled_{0}, unconfigured_{0}, unknown_{0}, short_input_{0}, invalid_input_{0};
    std::atomic<std::uint32_t> invalid_fields_{0}, other_address_{0}, target_hits_{0}, rejected_counts_{0}, queued_{0}, drops_{0};
    std::atomic<std::uint32_t> nonzero_result_{0}, status_not_newdevice_{0}, device_type_bad_{0}, address_type_bad_{0};
    std::atomic<std::uint32_t> diagnostic_zero_captured_{0};
    std::array<std::atomic<std::uint32_t>, 14> type_seen_{};
    std::array<std::atomic<std::uint32_t>, 256> scan_status_seen_{};
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
// Observation proves neither remote identity/pairing nor ownership of any client.
// Raw AD size/type are retained, never interpreted as canonical TLV lengths.
}
