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
        for (unsigned i=0;i<7;++i) assert(record.prefix[i] == value);
        assert(record.prefix[7] == 0); // Never retain the first address byte.
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
    assert(FormatBleHeader(text, sizeof(text), record) && text[448] == 'X');
    assert(std::strstr(text, "interpretation_unverified=1") && std::strstr(text, "foreign_address_name_payload_copied=0"));
    assert(std::strstr(text, "complete_prefix=1"));
    record.copied_size = 2; assert(FormatBleHeader(text, sizeof(text), record));
    assert(std::strstr(text, "complete_prefix=0"));
    assert(!FormatBleHeader(nullptr, 1, record)); assert(!FormatBleHeader(text, 1, record) && text[0] == 0);
    record.copied_size = 8; assert(!FormatBleHeader(text, sizeof(text), record) && text[0] == 0);
    BleHeaderTrace filter; filter.SetEnabled(true);
    for (unsigned size=0;size<=1024;++size) {
        raw.fill(0xa5); const auto before_filter=raw;
        assert(filter.Observe(7,raw.data(),size,1000+size,true));
        assert(filter.Pop(record) && record.event_type==7 && !record.configured_address_hint);
        assert(record.copied_size==std::min(size,8u) && raw==before_filter);
        assert(FormatBleHeader(text,sizeof text,record));
        assert(std::strstr(text,"callback_owner_correlation=none"));
    }
    for (unsigned action=0;action<256;++action) {
        raw.fill(0);raw[4]=action;raw[8]=0xab;
        assert(filter.Observe(7,raw.data(),raw.size(),2000));assert(filter.Pop(record));
        assert(record.prefix[4]==action && record.prefix[7]==0);
        assert(FormatBleHeader(text,sizeof text,record) && std::strstr(text,"sdk_filter_action="));
    }
    record.copied_size=9;assert(!FormatBleHeader(text,sizeof text,record));
    record.event_type=4;record.copied_size=0;assert(!FormatBleHeader(text,sizeof text,record));
    assert(filter.Observe(7,nullptr,1024,2001));assert(filter.Pop(record)&&record.copied_size==0);
    // Exact latest seven-byte trace agrees with the hash-bound stored start
    // emitter. This never turns lifecycle metadata into a usable peer sighting.
    const std::array<std::uint8_t,7> stored_start{0,0,0,0,0,0xff,0};
    assert(ClassifyScanHeader(stored_start.data(),7)==ScanHeaderKind::StoredStartShape);
    for(unsigned length=0;length<7;++length)
        assert(ClassifyScanHeader(stored_start.data(),length)==ScanHeaderKind::Short);
    assert(ClassifyScanHeader(nullptr,7)==ScanHeaderKind::Short);
    for(unsigned offset=0;offset<7;++offset)for(unsigned value=0;value<256;++value){
        auto changed=stored_start;changed[offset]=value;
        assert((ClassifyScanHeader(changed.data(),7)==ScanHeaderKind::StoredStartShape)==(value==stored_start[offset]));
    }
    auto sdk_start=stored_start;sdk_start[4]=0xff;sdk_start[5]=0;
    assert(ClassifyScanHeader(sdk_start.data(),7)==ScanHeaderKind::SdkStart);
    sdk_start[4]=1;
    assert(ClassifyScanHeader(sdk_start.data(),7)==ScanHeaderKind::SdkComplete);
    sdk_start[4]=2;
    assert(ClassifyScanHeader(sdk_start.data(),7)==ScanHeaderKind::NewDeviceHeader);
    sdk_start[5]=3;
    assert(ClassifyScanHeader(sdk_start.data(),7)==ScanHeaderKind::Unknown);
    sdk_start[5]=1;sdk_start[6]=4;
    assert(ClassifyScanHeader(sdk_start.data(),7)==ScanHeaderKind::Unknown);
    sdk_start[0]=1;
    assert(ClassifyScanHeader(sdk_start.data(),7)==ScanHeaderKind::Error);
    BleHeaderTrace lifecycle;lifecycle.SetEnabled(true);
    assert(lifecycle.Observe(6,stored_start.data(),7,UINT64_MAX,true));
    assert(lifecycle.Pop(record));record.sequence=UINT32_MAX;
    assert(FormatBleHeader(text,sizeof text,record));
    assert(std::strstr(text,"scan_kind=stored_start_shape") && std::strstr(text,"sdk_status=0"));
    assert(record.configured_address_hint && std::strstr(text,"interpretation_unverified=1"));
    // Address/AD suffix bytes cannot change diagnostic classification.
    for(unsigned i=0;i<1024;++i){
        raw.fill(i&255);std::copy(stored_start.begin(),stored_start.end(),raw.begin());
        assert(ClassifyScanHeader(raw.data(),raw.size())==ScanHeaderKind::StoredStartShape);
    }
    std::puts("PASS seven-byte-only scan prefix, all enum/result bytes retained, foreign data exclusion, lengths0..1024, bounded FIFO and formatter");
    std::puts("PASS stored start shape, SDK lifecycle/peer headers,1792 single-byte substitutions, truncations and suffix exclusion; diagnostic labels only");
}
