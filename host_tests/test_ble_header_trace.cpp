// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/ble_header_trace.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace amazon_remote;
int main() {
    BleHeaderTrace trace; BleHeaderRecord record{};
    std::array<std::uint8_t, 1024> raw{}; raw.fill(0xaa);
    assert(!trace.Observe(6, raw.data(), raw.size(), 1)); trace.SetEnabled(true);
    assert(!trace.Observe(4, raw.data(), raw.size(), 1));
    for (unsigned size = 0; size <= 1024; ++size) {
        assert(trace.Observe(6, raw.data(), size, size)); assert(trace.Pop(record));
        assert(record.copied_size == (size < 7 ? size : 7));
        for (unsigned i = 0; i < record.copied_size; ++i) assert(record.prefix[i] == 0xaa);
    }
    // Every raw result/status/device/address enum value survives unchanged.
    for (unsigned value = 0; value <= 255; ++value) {
        raw.fill(value); assert(trace.Observe(6, raw.data(), raw.size(), value)); assert(trace.Pop(record));
        for (auto p : record.prefix) assert(p == value);
    }
    const auto before = raw;
    const std::array<std::uint8_t,6> target{0x02,0xab,0xcd,0x12,0x34,0x56};
    std::copy(target.begin(), target.end(), raw.begin() + 7);
    assert(MatchesConfiguredAddressAtSdkOffset(raw.data(), raw.size(), target));
    for (unsigned size = 0; size < 13; ++size) assert(!MatchesConfiguredAddressAtSdkOffset(raw.data(), size, target));
    assert(!MatchesConfiguredAddressAtSdkOffset(nullptr, 13, target));
    std::array<std::uint8_t,6> zero{}, broadcast{}; broadcast.fill(0xff);
    assert(!MatchesConfiguredAddressAtSdkOffset(raw.data(), raw.size(), zero));
    assert(!MatchesConfiguredAddressAtSdkOffset(raw.data(), raw.size(), broadcast));
    assert(trace.Observe(6, raw.data(), raw.size(), 2, true)); assert(trace.Pop(record));
    assert(record.configured_address_hint);
    raw = before;
    assert(trace.Observe(6, raw.data(), raw.size(), 2)); assert(trace.Pop(record)); assert(raw == before);
    // Changing EVERY foreign address/name/AD byte cannot affect captured evidence.
    for (unsigned i = 7; i < raw.size(); ++i) raw[i] ^= 0xff;
    assert(trace.Observe(6, raw.data(), raw.size(), 2)); BleHeaderRecord same{}; assert(trace.Pop(same));
    assert(record.prefix == same.prefix && record.copied_size == same.copied_size);
    assert(trace.Observe(6, nullptr, 1024, 3)); assert(trace.Pop(record) && record.copied_size == 0);
    for (unsigned i = 0; i < 33; ++i) assert(trace.Observe(6, raw.data(), raw.size(), i) == (i < 32));
    trace.SetEnabled(false);
    for (unsigned i = 0; i < 32; ++i) assert(trace.Pop(record) && record.boot_ms == i);
    assert(!trace.Pop(record)); BleHeaderCounters counters{}; trace.SnapshotInto(counters);
    assert(counters.drops == 1 && counters.short_input == 8);
    char text[512]; std::memset(text, 'X', sizeof(text));
    assert(FormatBleHeader(text, sizeof(text), record) && text[384] == 'X');
    assert(std::strstr(text, "interpretation_unverified=1") && std::strstr(text, "foreign_address_name_payload_copied=0"));
    assert(std::strstr(text, "complete_prefix=1"));
    record.copied_size = 2; assert(FormatBleHeader(text, sizeof(text), record));
    assert(std::strstr(text, "complete_prefix=0"));
    assert(!FormatBleHeader(nullptr, 1, record)); assert(!FormatBleHeader(text, 1, record) && text[0] == 0);
    record.copied_size = 8; assert(!FormatBleHeader(text, sizeof(text), record) && text[0] == 0);
    std::puts("PASS seven-byte-only scan prefix, all enum/result bytes retained, foreign data exclusion, lengths0..1024, bounded FIFO and formatter");
}
