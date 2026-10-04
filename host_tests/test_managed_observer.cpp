// Independent portable passive observer tests. GPL-2.0-only.
#include "../mc_mitm/source/amazon_remote/managed_observer.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
using namespace amazon_remote;
using Scan = std::array<std::uint8_t, 328>;
const ManagedAddress target{0x02,0xab,0xcd,0x12,0x34,0x56};
void Put32(std::uint8_t *out, std::uint32_t x) { for (unsigned i = 0; i < 4; ++i) out[i] = x >> (i*8); }
Scan MakeScan() {
    Scan value{}; value[4] = 2; value[5] = 1; value[6] = 0;
    std::copy(target.begin(),target.end(),value.begin()+7); value[323] = 10;
    for (unsigned i = 0; i < 310; ++i) value[13+i] = i*17u; // Deliberately arbitrary sizes, never parsed.
    Put32(value.data()+324,static_cast<std::uint32_t>(-61)); return value;
}
void Start(ManagedObserver &observer) { assert(observer.ConfigureTarget(target)); observer.SetEnabled(true); }
int main() {
    ManagedAddress parsed{}, old{1,2,3,4,5,6};
    for (const char *value : {"02:AB:CD:12:34:56", "02:ab:cd:12:34:56\n", "02:AB:CD:12:34:56\r\n"}) {
        assert(StrictParseAddress(value,std::strlen(value),parsed)); assert(parsed == target);
    }
    for (const char *value : {"2:AB:CD:12:34:56", " 02:AB:CD:12:34:56", "02:AB:CD:12:34:56 ",
            "02-AB-CD-12-34-56", "02:AB:CD:12:34:5G", "02:AB:CD:12:34:56\r", "02:AB:CD:12:34:56\n\n",
            "00:00:00:00:00:00", "FF:FF:FF:FF:FF:FF"}) {
        parsed = old; assert(!StrictParseAddress(value,std::strlen(value),parsed) && parsed == old);
    }
    assert(!StrictParseAddress(nullptr,17,parsed));
    const std::string full = "02:AB:CD:12:34:56";
    for (std::size_t n = 0; n < 17; ++n) assert(!StrictParseAddress(full.data(),n,parsed));
    auto scan = MakeScan(); const auto unchanged = scan;
    ManagedObserver disabled; assert(disabled.ConfigureTarget(target));
    assert(!disabled.Observe(6,scan.data(),scan.size()) && disabled.Snapshot().disabled == 1);
    ManagedObserver unconfigured; unconfigured.SetEnabled(true);
    assert(!unconfigured.Observe(6,scan.data(),scan.size()) && unconfigured.Snapshot().unconfigured == 1);
    assert(!unconfigured.ConfigureTarget(target));
    ManagedObserver observer; Start(observer); assert(!observer.ConfigureTarget(target));
    ManagedRecord record{}; record.rssi = 123;
    assert(!observer.Pop(record) && record.rssi == 123);
    for (std::size_t n = 0; n < scan.size(); ++n) assert(!observer.Observe(6,scan.data(),n));
    assert(!observer.Observe(6,nullptr,328));
    assert(!observer.Observe(1,nullptr,0)); // Type1 is ServerRegistration, not ClientConnection.
    assert(!observer.Observe(999,nullptr,0));
    assert(observer.Observe(6,scan.data(),scan.size())); assert(scan == unchanged);
    assert(observer.Pop(record) && record.event_type == 6 && record.address == target && record.rssi == -61);
    for (unsigned i = 0; i < 10; ++i) assert(std::memcmp(&record.ads[i],scan.data()+13+i*31,31) == 0);
    auto other = scan; other[7+5] ^= 1; assert(!observer.Observe(6,other.data(),other.size()));
    auto reverse = scan; std::reverse(reverse.begin()+7,reverse.begin()+13); assert(!observer.Observe(6,reverse.data(),reverse.size()));
    for (unsigned count = 0; count <= 255; ++count) {
        scan[323] = count;
        assert(observer.Observe(6,scan.data(),scan.size()) == (count <= 10));
        if (count <= 10) assert(observer.Pop(record) && record.count == count);
    }
    assert(observer.Snapshot().rejected_counts == 245);
    for (unsigned status = 0; status <= 255; ++status) {
        auto value = MakeScan(); value[4] = status;
        assert(observer.Observe(6,value.data(),value.size()) == (status == 2));
        if (status == 2) assert(observer.Pop(record));
    }
    for (unsigned device = 0; device <= 255; ++device) {
        auto value = MakeScan(); value[5] = device;
        assert(observer.Observe(6,value.data(),value.size()) == (device == 1 || device == 2));
        if (device == 1 || device == 2) assert(observer.Pop(record));
    }
    for (unsigned type = 0; type <= 255; ++type) {
        auto value = MakeScan(); value[6] = type;
        assert(observer.Observe(6,value.data(),value.size()) == (type <= 3));
        if (type <= 3) assert(observer.Pop(record));
    }
    auto failed = MakeScan(); failed[0] = 1; assert(!observer.Observe(6,failed.data(),failed.size()));
    std::array<std::uint8_t,20> connection{}; connection[5] = 7;
    Put32(connection.data()+8,0x12345678); std::copy(target.begin(),target.end(),connection.begin()+12);
    connection[18] = 0x34; connection[19] = 0x12;
    for (std::size_t n = 0; n < connection.size(); ++n) assert(!observer.Observe(4,connection.data(),n));
    assert(observer.Observe(4,connection.data(),connection.size())); assert(observer.Pop(record));
    assert(record.client_if == 7 && record.connection_id == 0x12345678 && record.reason == 0x1234 && record.status == 0);
    connection[4] = 2; assert(observer.Observe(4,connection.data(),connection.size())); assert(observer.Pop(record));
    connection[4] = 1; assert(!observer.Observe(4,connection.data(),connection.size()));
    connection[4] = 0; connection[12] ^= 1; assert(!observer.Observe(4,connection.data(),connection.size()));
    ManagedObserver queue; Start(queue);
    for (unsigned i = 0; i < 17; ++i) {
        auto value = MakeScan(); Put32(value.data()+324,i);
        assert(queue.Observe(6,value.data(),value.size()) == (i < 16));
    }
    assert(queue.Snapshot().drops == 1 && queue.Snapshot().queued == 16);
    queue.SetEnabled(false);
    for (unsigned i = 0; i < 16; ++i) assert(queue.Pop(record) && record.rssi == static_cast<std::int32_t>(i));
    assert(!queue.Pop(record) && !queue.Observe(6,scan.data(),scan.size()));
    queue.SetEnabled(true); assert(queue.Observe(6,unchanged.data(),unchanged.size())); assert(queue.Pop(record));
    // All eight overlapping status2 predicates, with exactly the same old
    // admission decision. Only the all-good case reaches MAC/queue handling.
    ManagedObserver diagnostic; Start(diagnostic);
    for (unsigned mask = 0; mask < 8; ++mask) {
        auto value = MakeScan();
        if (mask & 1) Put32(value.data(),0xdeadcafe);
        if (mask & 2) value[5] = 3;
        if (mask & 4) value[6] = 4;
        const auto original = value;
        assert(diagnostic.Observe(6,value.data(),value.size()) == (mask == 0));
        assert(value == original);
        if (mask == 0) assert(diagnostic.Pop(record));
    }
    auto details = diagnostic.Snapshot();
    assert(details.nonzero_result == 4 && details.device_type_bad == 4 && details.address_type_bad == 4);
    assert(details.invalid_fields == 7 && details.target_hits == 1 && details.other_address == 0);
    assert(details.status_not_newdevice == 0 && details.queued == 1);
    // Terminal/non-status2 scans are separate, never treated as evidence for
    // device/address predicates; connection errors are also not scan counters.
    auto terminal = MakeScan(); terminal[0] = 1; terminal[4] = 1; terminal[5] = 0xff; terminal[6] = 0xff;
    assert(!diagnostic.Observe(6,terminal.data(),terminal.size()));
    connection[0] = 1; assert(!diagnostic.Observe(4,connection.data(),connection.size()));
    assert(!diagnostic.Observe(6,terminal.data(),7));
    diagnostic.SetEnabled(false); assert(!diagnostic.Observe(6,terminal.data(),terminal.size()));
    ManagedObserverCounters written{}; diagnostic.SnapshotInto(written);
    assert(written.status_not_newdevice == 1 && written.nonzero_result == 4 && written.device_type_bad == 4 && written.address_type_bad == 4);
    assert(written.invalid_fields == 9 && written.short_input == 1 && written.disabled == 1);
    // Reproduce the observed aggregate shape (14 rejected discoveries + two
    // non-discoveries), NOT the unknown actual device header values.
    ManagedObserver fourteen; Start(fourteen);
    for (unsigned i = 0; i < 14; ++i) {
        auto value = MakeScan();
        if (i < 5) value[0] = 1;
        else if (i < 10) value[5] = 0;
        else value[6] = 0xff;
        assert(!fourteen.Observe(6,value.data(),value.size()));
    }
    for (auto status : {0u,1u}) { auto value = MakeScan(); value[4] = status; assert(!fourteen.Observe(6,value.data(),value.size())); }
    const auto fourteen_counts = fourteen.Snapshot();
    assert(fourteen_counts.invalid_fields == 16 && fourteen_counts.scan_status_seen[2] == 14);
    assert(fourteen_counts.nonzero_result == 5 && fourteen_counts.device_type_bad == 5 && fourteen_counts.address_type_bad == 4);
    assert(fourteen_counts.status_not_newdevice == 2 && fourteen_counts.target_hits == 0 && fourteen_counts.other_address == 0 && fourteen_counts.queued == 0);
    // Explicitly configured diagnostic exception, never an active identity
    // decision. The same observed header is still rejected by default.
    auto observed_zero = MakeScan(); observed_zero[5] = 0;
    ManagedObserver strict_zero; Start(strict_zero);
    assert(!strict_zero.Observe(6,observed_zero.data(),observed_zero.size()));
    assert(strict_zero.Snapshot().device_type_bad == 1 && strict_zero.Snapshot().target_hits == 0);
    ManagedObserver zero_capture;
    assert(!zero_capture.ConfigureTarget(target,static_cast<TargetCaptureMode>(999)));
    assert(zero_capture.ConfigureTarget(target,TargetCaptureMode::DiagnosticAllowDeviceZero));
    zero_capture.SetEnabled(true);
    assert(!zero_capture.ConfigureTarget(target,TargetCaptureMode::StrictSdk));
    const auto unchanged_zero = observed_zero;
    assert(zero_capture.Observe(6,observed_zero.data(),observed_zero.size()) && observed_zero == unchanged_zero);
    assert(zero_capture.Pop(record) && record.diagnostic_only && record.device_type_exception && record.device_type == 0);
    for (unsigned i = 0; i < 10; ++i) assert(std::memcmp(&record.ads[i],observed_zero.data()+13+i*31,31) == 0);
    assert(record.ads[0].size == observed_zero[13]); // Includes malformed/unknown AD sizes verbatim.
    assert(zero_capture.Snapshot().diagnostic_zero_captured == 1 && zero_capture.Snapshot().device_type_bad == 1);
    // Every opted-in record is labeled, even normal SDK types and connections.
    for (auto device : {1u,2u}) {
        auto value = observed_zero; value[5] = device;
        assert(zero_capture.Observe(6,value.data(),value.size()) && zero_capture.Pop(record));
        assert(record.diagnostic_only && !record.device_type_exception);
    }
    connection.fill(0); std::copy(target.begin(),target.end(),connection.begin()+12);
    assert(zero_capture.Observe(4,connection.data(),connection.size()) && zero_capture.Pop(record));
    assert(record.diagnostic_only && !record.device_type_exception);
    for (unsigned device = 3; device <= 255; ++device) {
        auto value = observed_zero; value[5] = device;
        assert(!zero_capture.Observe(6,value.data(),value.size()));
    }
    auto foreign_zero = observed_zero; foreign_zero[7] ^= 1;
    assert(!zero_capture.Observe(6,foreign_zero.data(),foreign_zero.size()) && !zero_capture.Pop(record));
    for (unsigned count = 0; count <= 255; ++count) {
        auto value = observed_zero; value[323] = count;
        assert(zero_capture.Observe(6,value.data(),value.size()) == (count <= 10));
        if (count <= 10) { assert(zero_capture.Pop(record) && record.diagnostic_only && record.count == count); }
    }
    assert(zero_capture.Snapshot().rejected_counts == 245 && zero_capture.Snapshot().diagnostic_zero_captured == 12);
    for (unsigned address_type = 0; address_type <= 255; ++address_type) {
        auto value = observed_zero; value[6] = address_type;
        assert(zero_capture.Observe(6,value.data(),value.size()) == (address_type <= 3));
        if (address_type <= 3) assert(zero_capture.Pop(record) && record.diagnostic_only);
    }
    auto invalid_result = observed_zero; invalid_result[0] = 1;
    auto invalid_status = observed_zero; invalid_status[4] = 1;
    assert(!zero_capture.Observe(6,invalid_result.data(),invalid_result.size()));
    assert(!zero_capture.Observe(6,invalid_status.data(),invalid_status.size()));
    for (std::size_t length = 0; length < observed_zero.size(); ++length)
        assert(!zero_capture.Observe(6,observed_zero.data(),length));
    assert(!zero_capture.Observe(6,nullptr,328) && !zero_capture.Pop(record));
    ManagedObserver optin_overflow;
    assert(optin_overflow.ConfigureTarget(target,TargetCaptureMode::DiagnosticAllowDeviceZero)); optin_overflow.SetEnabled(true);
    for (unsigned i = 0; i < 17; ++i) assert(optin_overflow.Observe(6,observed_zero.data(),328) == (i < 16));
    assert(optin_overflow.Snapshot().diagnostic_zero_captured == 16 && optin_overflow.Snapshot().drops == 1);
    for (unsigned i = 0; i < 16; ++i) assert(optin_overflow.Pop(record) && record.diagnostic_only && record.device_type_exception);
    // A real concurrent SPSC run: producer never retries rejected pushes.
    ManagedObserver concurrent; Start(concurrent);
    std::atomic<bool> done{false}; std::uint32_t consumed = 0;
    std::thread consumer([&] {
        std::int32_t previous = -1;
        while (!done.load(std::memory_order_acquire)) {
            ManagedRecord value;
            if (concurrent.Pop(value)) { assert(value.rssi > previous); previous = value.rssi; ++consumed; }
            else std::this_thread::yield();
        }
        ManagedRecord value;
        while (concurrent.Pop(value)) { assert(value.rssi > previous); previous = value.rssi; ++consumed; }
    });
    std::thread producer([&] {
        for (unsigned i = 0; i < 100000; ++i) {
            auto value = MakeScan(); Put32(value.data()+324,i);
            concurrent.Observe(6,value.data(),value.size());
        }
        done.store(true,std::memory_order_release);
    });
    producer.join(); consumer.join();
    const auto counters = concurrent.Snapshot();
    ManagedObserverCounters direct;
    concurrent.SnapshotInto(direct);
    assert(direct.seen == counters.seen && direct.queued == counters.queued && direct.drops == counters.drops);
    assert(direct.type_seen == counters.type_seen && direct.scan_status_seen == counters.scan_status_seen);
    assert(counters.seen == 100000 && counters.target_hits == 100000 && counters.queued == consumed);
    assert(counters.queued + counters.drops == 100000 && counters.type_seen[6] == 100000 && counters.scan_status_seen[2] == 100000);
    assert(counters.nonzero_result == 0 && counters.status_not_newdevice == 0 && counters.device_type_bad == 0 && counters.address_type_bad == 0);
    std::puts("PASS strict address, scan/connection offsets, untouched raw ADs, all count/status/device/address-type values, bounds and FIFO overflow");
    std::puts("PASS overlapping status2 rejection predicates, non-discovery separation and synthetic14+2 aggregate, admission unchanged");
    std::printf("PASS 100000 concurrent SPSC observations; queued=%u, bounded drops=%u\n",counters.queued,counters.drops);
    // Another real concurrent run alternates dev0/dev1 in explicit capture mode.
    ManagedObserver diagnostic_concurrent;
    assert(diagnostic_concurrent.ConfigureTarget(target,TargetCaptureMode::DiagnosticAllowDeviceZero)); diagnostic_concurrent.SetEnabled(true);
    done.store(false); consumed = 0; std::uint32_t zero_consumed = 0;
    std::thread diagnostic_consumer([&] {
        std::int32_t previous = -1;
        auto accept = [&](const ManagedRecord &value) {
            assert(value.rssi > previous && value.diagnostic_only);
            assert(value.device_type_exception == (value.device_type == 0));
            previous = value.rssi; ++consumed; if (value.device_type_exception) ++zero_consumed;
        };
        while (!done.load(std::memory_order_acquire)) {
            ManagedRecord value; if (diagnostic_concurrent.Pop(value)) accept(value); else std::this_thread::yield();
        }
        ManagedRecord value; while (diagnostic_concurrent.Pop(value)) accept(value);
    });
    std::thread diagnostic_producer([&] {
        for (unsigned i = 0; i < 100000; ++i) {
            auto value = MakeScan(); value[5] = i%2; Put32(value.data()+324,i);
            diagnostic_concurrent.Observe(6,value.data(),value.size());
        }
        done.store(true,std::memory_order_release);
    });
    diagnostic_producer.join(); diagnostic_consumer.join();
    const auto diagnostic_counts = diagnostic_concurrent.Snapshot();
    assert(diagnostic_counts.queued == consumed && diagnostic_counts.queued + diagnostic_counts.drops == 100000);
    assert(diagnostic_counts.diagnostic_zero_captured == zero_consumed && diagnostic_counts.device_type_bad == 50000);
    std::puts("PASS immutable explicit diagnostic-only target capture, strict defaults, raw AD preservation and100000 mixed dev0/dev1 SPSC observations");
}
