// SPDX-License-Identifier: GPL-2.0-only
// Copy only the seven-byte pre-address ScanResult prefix. No MAC/name/AD data.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace amazon_remote {
struct BleHeaderRecord {
    std::uint64_t boot_ms{};
    std::uint32_t sequence{}, event_type{};
    std::uint8_t copied_size{};
    std::array<std::uint8_t, 7> prefix{};
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
}
