// SPDX-License-Identifier: GPL-2.0-only
#include "ble_header_trace.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace amazon_remote {
bool MatchesConfiguredAddressAtSdkOffset(const void *data, std::size_t size, const std::array<std::uint8_t, 6> &target) {
    if (!data || size < 13 || std::all_of(target.begin(), target.end(), [](auto x) { return x == 0; }) ||
        std::all_of(target.begin(), target.end(), [](auto x) { return x == 0xff; })) return false;
    return std::equal(target.begin(), target.end(), static_cast<const std::uint8_t *>(data) + 7);
}
bool BleHeaderTrace::Observe(std::uint32_t type, const void *data, std::size_t size, std::uint64_t boot_ms, bool configured_address_hint) {
    if (!Enabled() || type != 6) return false;
    const auto sequence = seen_.fetch_add(1, std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_relaxed);
    if (std::uint32_t(head - tail_.load(std::memory_order_acquire)) >= Capacity) {
        drops_.fetch_add(1, std::memory_order_relaxed); return false;
    }
    BleHeaderRecord record{};
    record.boot_ms = boot_ms; record.sequence = sequence; record.event_type = type;
    record.configured_address_hint = configured_address_hint;
    if (data) {
        record.copied_size = static_cast<std::uint8_t>(std::min<std::size_t>(size, record.prefix.size()));
        std::memcpy(record.prefix.data(), data, record.copied_size);
    }
    if (record.copied_size < 7) short_input_.fetch_add(1, std::memory_order_relaxed);
    records_[head % Capacity] = record;
    head_.store(head + 1, std::memory_order_release);
    queued_.fetch_add(1, std::memory_order_relaxed);
    return true; // No result/status/enum/address validation: metadata evidence only.
}
bool BleHeaderTrace::Pop(BleHeaderRecord &out) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = records_[tail % Capacity];
    tail_.store(tail + 1, std::memory_order_release); return true;
}
void BleHeaderTrace::SnapshotInto(BleHeaderCounters &out) const {
    out.seen = seen_.load(std::memory_order_relaxed);
    out.short_input = short_input_.load(std::memory_order_relaxed);
    out.queued = queued_.load(std::memory_order_relaxed);
    out.drops = drops_.load(std::memory_order_relaxed);
}
bool FormatBleHeader(char *out, std::size_t capacity, const BleHeaderRecord &record) {
    if (!out || !capacity) return false;
    if (record.copied_size > 7) { out[0] = 0; return false; }
    char hex[15]{};
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < record.copied_size; ++i) {
        hex[i * 2] = digits[record.prefix[i] >> 4]; hex[i * 2 + 1] = digits[record.prefix[i] & 15];
    }
    const auto &p = record.prefix;
    const auto result = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    const auto limit = std::min<std::size_t>(capacity, 384);
    const auto size = std::snprintf(out, limit,
        "header_boot_ms=%llu sequence=%u event=%u prefix_size=%u complete_prefix=%u raw_prefix=%s sdk_result=%08x sdk_status=%u sdk_device_type=%u sdk_address_type=%u configured_address_at_sdk_offset=%u interpretation_unverified=1 foreign_address_name_payload_copied=0",
        static_cast<unsigned long long>(record.boot_ms), record.sequence, record.event_type, record.copied_size,
        record.copied_size == 7 ? 1u : 0u, hex,
        result, p[4], p[5], p[6], record.configured_address_hint);
    return size >= 0 && static_cast<std::size_t>(size) < limit;
}
}
