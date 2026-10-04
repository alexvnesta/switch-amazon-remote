// SPDX-License-Identifier: GPL-2.0-only
#include "intent_trace.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace amazon_remote {
bool IsIntentCommand(std::uint32_t command) {
    return (command >= 47 && command <= 49) || (command >= 55 && command <= 64);
}
std::size_t ExpectedIntentSize(std::uint32_t command) {
    if (command == 57 || command == 58) return 62;
    if (command == 59 || command == 61 || command == 63) return 1;
    if (command == 62) return 20;
    return 0;
}
bool IsIntentMode(const char *data, std::size_t size) {
    return data && ((size == 7 && !std::memcmp(data, "intents", 7)) ||
        (size == 8 && !std::memcmp(data, "intents\n", 8)) ||
        (size == 9 && !std::memcmp(data, "intents\r\n", 9)));
}
bool IntentTrace::Observe(std::uint32_t command, std::uint64_t program, std::uint64_t process,
    std::uint64_t boot_ms, const void *data, std::size_t size) {
    const auto sequence = seen_.fetch_add(1, std::memory_order_relaxed);
    if (!Enabled()) { disabled_.fetch_add(1, std::memory_order_relaxed); return false; }
    if (!IsIntentCommand(command) || (program != HidProgram && program != BtmProgram)) {
        rejected_.fetch_add(1, std::memory_order_relaxed); return false;
    }
    const auto head = head_.load(std::memory_order_relaxed);
    if (std::uint32_t(head - tail_.load(std::memory_order_acquire)) >= Capacity) {
        drops_.fetch_add(1, std::memory_order_relaxed); return false;
    }
    IntentRecord record{};
    record.command = command; record.sequence = sequence; record.program_id = program;
    record.process_id = process; record.boot_ms = boot_ms; record.reported_size = size;
    if (size != ExpectedIntentSize(command)) record.flags |= IntentUnexpectedSize;
    if (!data && size) record.flags |= IntentNullPayload;
    if (size > record.payload.size()) record.flags |= IntentTruncated;
    if (data) {
        record.copied_size = static_cast<std::uint8_t>(std::min(size, record.payload.size()));
        std::memcpy(record.payload.data(), data, record.copied_size);
    }
    if (command == 62 && record.copied_size >= 4) {
        const auto *p = record.payload.data();
        const auto uuid_size = std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
            (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
        if (uuid_size != 2 && uuid_size != 4 && uuid_size != 16) record.flags |= IntentInvalidUuidSize;
    }
    records_[head % Capacity] = record;
    head_.store(head + 1, std::memory_order_release);
    queued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}
bool IntentTrace::Pop(IntentRecord &out) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = records_[tail % Capacity];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}
void IntentTrace::SnapshotInto(IntentCounters &out) const {
    out.seen = seen_.load(std::memory_order_relaxed);
    out.disabled = disabled_.load(std::memory_order_relaxed);
    out.rejected = rejected_.load(std::memory_order_relaxed);
    out.queued = queued_.load(std::memory_order_relaxed);
    out.drops = drops_.load(std::memory_order_relaxed);
}
bool FormatIntent(char *out, std::size_t capacity, const IntentRecord &record) {
    if (!out || !capacity) return false;
    char hex[125]{};
    constexpr char digits[] = "0123456789abcdef";
    if (record.copied_size > record.payload.size()) { out[0] = 0; return false; }
    for (std::size_t i = 0; i < record.copied_size; ++i) {
        hex[i * 2] = digits[record.payload[i] >> 4];
        hex[i * 2 + 1] = digits[record.payload[i] & 15];
    }
    const auto limit = std::min<std::size_t>(capacity, 512);
    const int size = std::snprintf(out, limit,
        "intent_boot_ms=%llu sequence=%u command=%u program=%016llx process=%llu reported_size=%llu copied_size=%u flags=%08x payload=%s driver_result=unobserved callback_correlation=none ownership=unproven",
        static_cast<unsigned long long>(record.boot_ms), record.sequence, record.command,
        static_cast<unsigned long long>(record.program_id), static_cast<unsigned long long>(record.process_id),
        static_cast<unsigned long long>(record.reported_size), record.copied_size, record.flags, hex);
    return size >= 0 && static_cast<std::size_t>(size) < limit;
}
}
