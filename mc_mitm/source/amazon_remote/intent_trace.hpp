// SPDX-License-Identifier: GPL-2.0-only
// Passive IPC intent metadata. This is not a callback/ownership ledger.
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {
inline constexpr std::uint64_t HidProgram = 0x0100000000000013ull;
inline constexpr std::uint64_t BtmProgram = 0x010000000000002aull;
enum IntentFlags : std::uint32_t {
    IntentNullPayload = 1, IntentTruncated = 2,
    IntentUnexpectedSize = 4, IntentInvalidUuidSize = 8,
};
struct IntentRecord {
    std::uint64_t boot_ms{}, program_id{}, process_id{}, reported_size{};
    std::uint32_t sequence{}, command{}, flags{};
    std::uint8_t copied_size{};
    std::array<std::uint8_t, 62> payload{};
};
struct IntentCounters {
    std::uint32_t seen{}, disabled{}, rejected{}, queued{}, drops{};
};
bool IsIntentCommand(std::uint32_t command);
std::size_t ExpectedIntentSize(std::uint32_t command);
bool IsIntentMode(const char *data, std::size_t size);
// Absolute boot-time window, not renewed by late module start or callbacks.
inline constexpr std::uint64_t IntentWindowMs = 120000;
constexpr std::uint64_t IntentDeadline(std::uint64_t start) {
    return start > UINT64_MAX - IntentWindowMs ? UINT64_MAX : start + IntentWindowMs;
}
constexpr bool IntentWindowOpen(std::uint64_t now, std::uint64_t deadline) {
    return deadline >= IntentWindowMs && now >= deadline - IntentWindowMs && now < deadline;
}
// Requires configured lifetime single producer/single consumer. No heap/locks/IO.
class IntentTrace {
public:
    static constexpr std::size_t Capacity = 16;
    void SetEnabled(bool enabled) { enabled_.store(enabled, std::memory_order_release); }
    bool Enabled() const { return enabled_.load(std::memory_order_acquire); }
    bool Observe(std::uint32_t command, std::uint64_t program, std::uint64_t process,
        std::uint64_t boot_ms, const void *data, std::size_t size);
    bool Pop(IntentRecord &out);
    void SnapshotInto(IntentCounters &out) const;
private:
    std::array<IntentRecord, Capacity> records_{};
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint32_t> head_{0}, tail_{0};
    std::atomic<std::uint32_t> seen_{0}, disabled_{0}, rejected_{0}, queued_{0}, drops_{0};
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
// Fixed max512-byte line; only formats bounded metadata, never claims success.
bool FormatIntent(char *out, std::size_t capacity, const IntentRecord &record);
}
