// SPDX-License-Identifier: GPL-2.0-only
// ScanResult: seven pre-address bytes. ScanFilter: eight result/action bytes.
// No MAC, name, advertisement, pairing response or key data is copied.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace amazon_remote {
inline constexpr bool IsBleHeaderEvent(std::uint32_t type) { return type == 6 || type == 7; }
// Diagnostic prefix labels only. Never use these for peer admission, connection
// or pairing. StoredStartShape is backed by one stored22.5 image; it is NOT a
// live firmware match or proof of RF scanning. No peer bytes are inspected.
enum class ScanHeaderKind { Short, Error, StoredStartShape, SdkStart,
                            SdkComplete, NewDeviceHeader, Unknown };
ScanHeaderKind ClassifyScanHeader(const void *data, std::size_t size);
const char *ScanHeaderKindName(ScanHeaderKind kind);
struct BleHeaderRecord {
    std::uint64_t boot_ms{};
    std::uint32_t sequence{}, event_type{};
    std::uint8_t copied_size{};
    std::array<std::uint8_t, 8> prefix{};
    bool configured_address_hint{}; // Comparison only; no address bytes retained.
};
struct BleHeaderCounters { std::uint32_t seen{}, short_input{}, queued{}, drops{}; };
class BleHeaderTrace {
public:
    static constexpr std::size_t Capacity = 32;
    void SetEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }
    // One BLE-event producer, distinct from the IPC intent producer.
    bool Observe(std::uint32_t type, const void *data, std::size_t size, std::uint64_t boot_ms, bool configured_address_hint = false);
    bool Pop(BleHeaderRecord &out);
    void SnapshotInto(BleHeaderCounters &out) const;
private:
    std::array<BleHeaderRecord, Capacity> records_{};
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint32_t> head_{0}, tail_{0}, seen_{0}, short_input_{0}, queued_{0}, drops_{0};
};
bool FormatBleHeader(char *out, std::size_t capacity, const BleHeaderRecord &record);
// Equality at the SDK-declared offset only; not identity, admission or ownership.
bool MatchesConfiguredAddressAtSdkOffset(const void *data, std::size_t size, const std::array<std::uint8_t, 6> &target);
// A filter completion contains no peer address. Never run the scan-address
// comparison on type7, even if a caller supplies a large declared event size.
inline bool BleHeaderConfiguredAddressHint(std::uint32_t type,const void *data,std::size_t size,
        const std::array<std::uint8_t,6> &target) {
    return type == 6 && MatchesConfiguredAddressAtSdkOffset(data,size,target);
}
}
