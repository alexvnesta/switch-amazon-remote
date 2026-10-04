// Independent passive event observer. GPL-2.0-only.
#include "managed_observer.hpp"
#include <algorithm>

namespace amazon_remote {
namespace {
bool Usable(const ManagedAddress &value) {
    return !std::all_of(value.begin(), value.end(), [](auto b) { return b == 0; }) &&
        !std::all_of(value.begin(), value.end(), [](auto b) { return b == 0xff; });
}
int Hex(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
std::uint32_t U32(const std::uint8_t *value) {
    return std::uint32_t(value[0]) | (std::uint32_t(value[1]) << 8) |
        (std::uint32_t(value[2]) << 16) | (std::uint32_t(value[3]) << 24);
}
void Increment(std::atomic<std::uint32_t> &value) { value.fetch_add(1, std::memory_order_relaxed); }
}
bool StrictParseAddress(const char *text, std::size_t length, ManagedAddress &out) {
    if (!text || (length != 17 && length != 18 && length != 19)) return false;
    if (length == 18 && text[17] != '\n') return false;
    if (length == 19 && (text[17] != '\r' || text[18] != '\n')) return false;
    ManagedAddress parsed{};
    for (std::size_t i = 0; i < 6; ++i) {
        const auto hi = Hex(text[i * 3]), lo = Hex(text[i * 3 + 1]);
        if (hi < 0 || lo < 0 || (i < 5 && text[i * 3 + 2] != ':')) return false;
        parsed[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    if (!Usable(parsed)) return false;
    out = parsed;
    return true;
}
ManagedObserver::ManagedObserver() {
    // Explicitly initialize array atomics even with pre-C++20 standard libraries.
    for (auto &count : type_seen_) count.store(0, std::memory_order_relaxed);
    for (auto &count : scan_status_seen_) count.store(0, std::memory_order_relaxed);
}
bool ManagedObserver::ConfigureTarget(const ManagedAddress &target, TargetCaptureMode mode) {
    // Caller guarantees this initialization is not concurrent with Observe.
    if (observed_.load(std::memory_order_acquire) || target_ready_.load(std::memory_order_acquire) || !Usable(target) ||
        (mode != TargetCaptureMode::StrictSdk && mode != TargetCaptureMode::DiagnosticAllowDeviceZero)) return false;
    target_ = target;
    capture_mode_ = mode;
    target_ready_.store(true, std::memory_order_release);
    return true;
}
bool ManagedObserver::Observe(std::uint32_t event_type, const std::uint8_t *data, std::size_t length) {
    observed_.store(true, std::memory_order_relaxed);
    Increment(seen_);
    if (event_type < type_seen_.size()) Increment(type_seen_[event_type]);
    if (!enabled_.load(std::memory_order_acquire)) { Increment(disabled_); return false; }
    if (!target_ready_.load(std::memory_order_acquire)) { Increment(unconfigured_); return false; }
    const bool scan = event_type == ScanEventType;
    if (!scan && event_type != ConnectionEventType) { Increment(unknown_); return false; }
    if (!data) { Increment(invalid_input_); return false; }
    if (length < (scan ? ScanBytes : ConnectionBytes)) { Increment(short_input_); return false; }
    const auto result = U32(data);
    const auto status = data[4];
    if (scan) {
        Increment(scan_status_seen_[status]);
        // Count independent predicates before the existing short-circuit gate.
        // Non-discovery callbacks may not initialize device/address fields, so
        // never attribute their arbitrary bytes to these three diagnostics.
        if (status != 2) Increment(status_not_newdevice_);
        else {
            if (result) Increment(nonzero_result_);
            if (data[5] != 1 && data[5] != 2) Increment(device_type_bad_);
            if (data[6] > 3) Increment(address_type_bad_);
        }
    }
    if (result || (scan ? status != 2 : (status != 0 && status != 2))) {
        Increment(invalid_fields_); return false;
    }
    const bool diagnostic_mode = capture_mode_ == TargetCaptureMode::DiagnosticAllowDeviceZero;
    const bool device_type_exception = scan && diagnostic_mode && data[5] == 0;
    if (scan && (((data[5] != 1 && data[5] != 2) && !device_type_exception) || data[6] > 3)) {
        Increment(invalid_fields_); return false;
    }
    const auto address_offset = scan ? 7u : 12u;
    if (!std::equal(target_.begin(), target_.end(), data + address_offset)) {
        Increment(other_address_); return false;
    }
    Increment(target_hits_);
    if (scan && data[323] > 10) { Increment(rejected_counts_); return false; }
    ManagedRecord record{};
    record.event_type = event_type;
    record.result = result;
    record.status = status;
    record.diagnostic_only = diagnostic_mode;
    record.device_type_exception = device_type_exception;
    std::copy_n(data + address_offset, record.address.size(), record.address.begin());
    if (scan) {
        record.device_type = data[5];
        record.address_type = data[6];
        record.count = data[323];
        const auto raw_rssi = U32(data + 324);
        record.rssi = static_cast<std::int32_t>(raw_rssi < 0x80000000u ? std::int64_t(raw_rssi) : std::int64_t(raw_rssi) - 0x100000000ll);
        for (std::size_t i = 0; i < record.count; ++i) {
            const auto *raw = data + 13 + i * 31;
            record.ads[i].size = raw[0];
            record.ads[i].type = raw[1];
            std::copy_n(raw + 2, 29, record.ads[i].data.begin());
        }
    } else {
        record.client_if = data[5];
        record.connection_id = U32(data + 8);
        record.reason = std::uint16_t(data[18]) | (std::uint16_t(data[19]) << 8);
    }
    const bool queued = Enqueue(record);
    if (queued && device_type_exception) Increment(diagnostic_zero_captured_);
    return queued;
}
bool ManagedObserver::Enqueue(const ManagedRecord &record) {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (std::uint32_t(head - tail) >= Capacity) { Increment(drops_); return false; }
    records_[head % Capacity] = record;
    head_.store(head + 1, std::memory_order_release);
    Increment(queued_);
    return true;
}
bool ManagedObserver::Pop(ManagedRecord &out) {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = records_[tail % Capacity];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}
ManagedObserverCounters ManagedObserver::Snapshot() const {
    ManagedObserverCounters result;
    SnapshotInto(result);
    return result;
}
void ManagedObserver::SnapshotInto(ManagedObserverCounters &result) const {
#define LOAD(name) result.name = name##_.load(std::memory_order_relaxed)
    LOAD(seen); LOAD(disabled); LOAD(unconfigured); LOAD(unknown); LOAD(short_input); LOAD(invalid_input);
    LOAD(invalid_fields); LOAD(other_address); LOAD(target_hits); LOAD(rejected_counts); LOAD(queued); LOAD(drops);
    LOAD(nonzero_result); LOAD(status_not_newdevice); LOAD(device_type_bad); LOAD(address_type_bad);
    LOAD(diagnostic_zero_captured);
#undef LOAD
    for (std::size_t i = 0; i < type_seen_.size(); ++i) result.type_seen[i] = type_seen_[i].load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < scan_status_seen_.size(); ++i) result.scan_status_seen[i] = scan_status_seen_[i].load(std::memory_order_relaxed);
}
}
