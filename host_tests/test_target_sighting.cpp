// Independent passive sighting/freshness tests. GPL-2.0-only.
#include "../mc_mitm/source/amazon_remote/target_sighting.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace amazon_remote;
using Scan = std::array<std::uint8_t,328>;
const SightingAddress target{0x02,0xab,0xcd,0x12,0x34,0x56};
Scan MakeScan() {
    Scan value{}; value[4] = 2; value[5] = 0; value[6] = 0; value[323] = 2;
    std::copy(target.begin(),target.end(),value.begin()+7);
    value[13] = 3; value[14] = 3; value[15] = 0x12; value[16] = 0x18;
    value[44] = 10; value[45] = 0xff;
    const std::uint8_t manufacturer[]{0x71,0x01,0x04,0x27,0x02,0x7d,0x9f,0x60,0x15};
    std::copy_n(manufacturer,9,value.begin()+46); return value;
}
void Stamp(Scan &scan,std::uint64_t stamp) { for (unsigned i = 0; i < 8; ++i) scan[300+i] = stamp >> (i*8); }
std::uint64_t Stamp(const Scan &scan) { std::uint64_t stamp = 0; for (unsigned i = 0; i < 8; ++i) stamp |= std::uint64_t(scan[300+i]) << (i*8); return stamp; }
int main() {
    auto scan = MakeScan(); const auto original = scan;
    TargetSightingSnapshot snapshot; SightingWire wire{}; wire.magic = 0xdead;
    assert(!snapshot.Read(wire) && wire.magic == 0xdead);
    assert(snapshot.Configure(target) && !snapshot.Configure(target));
    assert(!snapshot.Observe(6,scan.data(),scan.size(),1000)); // Disabled default.
    snapshot.SetEnabled(true); assert(!snapshot.Read(wire)); // No prior publication.
    assert(snapshot.Observe(6,scan.data(),scan.size(),1000) && scan == original);
    assert(snapshot.Read(wire) && wire.raw == scan && wire.captured_boot_ms == 1000);
    assert(AssessSighting(wire,target,1000,1000) == SightingAssessment::FreshCandidate);
    assert(AssessSighting(wire,target,999,4000) == SightingAssessment::FreshCandidate);
    assert(AssessSighting(wire,target,999,4001) == SightingAssessment::Stale);
    assert(AssessSighting(wire,target,1001,1500) == SightingAssessment::Stale);
    assert(AssessSighting(wire,target,999,999) == SightingAssessment::Future);
    assert(AssessSighting(wire,target,2000,1500) == SightingAssessment::Future);
    auto bad = wire; bad.magic = 0; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::Invalid);
    bad = wire; bad.version = 2; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::Invalid);
    bad = wire; bad.raw[7] ^= 1; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::Invalid);
    auto empty_target = target; empty_target.fill(0); assert(AssessSighting(wire,empty_target,0,1000) == SightingAssessment::Invalid);
    for (auto size : {0u,31u,255u}) { bad = wire; bad.raw[44] = size; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::Invalid); }
    bad = wire; bad.raw[44] = 6; // Sixth prefix byte present in slot, beyond conservative declared payload.
    assert(AssessSighting(bad,target,0,1000) == SightingAssessment::NotCandidate);
    bad = wire; bad.raw[51] ^= 1; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::NotCandidate);
    for (auto size : {1u,2u,4u}) { bad = wire; bad.raw[13] = size; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::Invalid); }
    bad = wire; bad.raw[14] = 2; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::FreshCandidate);
    bad = wire; bad.raw[13] = 5; bad.raw[15] = 0x0f; bad.raw[16] = 0x18; bad.raw[17] = 0x12; bad.raw[18] = 0x18;
    assert(AssessSighting(bad,target,0,1000) == SightingAssessment::FreshCandidate);
    bad = wire; bad.raw[14] = 7; bad.raw[13] = 17; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::NotCandidate);
    bad = wire; bad.raw[323] = 255; assert(AssessSighting(bad,target,0,1000) == SightingAssessment::Invalid);
    for (std::size_t length = 0; length < scan.size(); ++length) assert(!snapshot.Observe(6,scan.data(),length,1000));
    assert(!snapshot.Observe(6,nullptr,328,1000) && !snapshot.Observe(4,scan.data(),328,1000));
    assert(!snapshot.Observe(6,scan.data(),328,0));
    for (unsigned device = 0; device <= 255; ++device) {
        auto value = scan; value[5] = device;
        assert(snapshot.Observe(6,value.data(),328,1000) == (device <= 2));
    }
    for (unsigned address = 0; address <= 255; ++address) {
        auto value = scan; value[6] = address;
        assert(snapshot.Observe(6,value.data(),328,1000) == (address <= 1));
    }
    for (unsigned count = 0; count <= 255; ++count) {
        auto value = scan; value[323] = count;
        assert(snapshot.Observe(6,value.data(),328,1000) == (count <= 10));
    }
    auto foreign = scan; foreign[7] ^= 1; assert(!snapshot.Observe(6,foreign.data(),328,1000));
    auto error = scan; error[0] = 1; assert(!snapshot.Observe(6,error.data(),328,1000));
    auto terminal = scan; terminal[4] = 1; assert(!snapshot.Observe(6,terminal.data(),328,1000));
    snapshot.SetEnabled(false); assert(!snapshot.Read(wire));
    snapshot.SetEnabled(true); assert(!snapshot.Read(wire)); // Old generation cannot resurrect.
    assert(snapshot.Observe(6,scan.data(),328,1100) && snapshot.Read(wire));
    assert(wire.captured_boot_ms == 1100);
    TargetSightingSnapshot wrapping(0xfffffffeu); assert(wrapping.Configure(target)); wrapping.SetEnabled(true);
    assert(wrapping.Observe(6,scan.data(),328,1000) && wrapping.Read(wire)); // Even sequence wraps to0.
    assert(wrapping.Observe(6,scan.data(),328,1001) && wrapping.Read(wire) && wire.captured_boot_ms == 1001);
    // Single producer, concurrent reader and sole controlling thread toggles.
    TargetSightingSnapshot concurrent; assert(concurrent.Configure(target)); concurrent.SetEnabled(true);
    std::atomic<bool> done{false}; std::uint32_t reads = 0;
    std::thread reader([&] {
        auto check = [&](const SightingWire &value) {
            assert(value.magic == SightingMagic && value.version == SightingVersion);
            assert(value.captured_boot_ms == Stamp(value.raw)); ++reads;
            assert(std::equal(target.begin(),target.end(),value.raw.begin()+7));
        };
        while (!done.load(std::memory_order_acquire)) { SightingWire value; if (concurrent.Read(value)) check(value); else std::this_thread::yield(); }
        SightingWire value; if (concurrent.Read(value)) check(value);
    });
    std::thread producer([&] {
        for (std::uint64_t i = 1; i <= 100000; ++i) { auto value = scan; Stamp(value,i); concurrent.Observe(6,value.data(),328,i); }
        done.store(true,std::memory_order_release);
    });
    for (unsigned i = 0; i < 1000; ++i) { concurrent.SetEnabled(false); concurrent.SetEnabled(true); std::this_thread::yield(); }
    producer.join(); reader.join();
    assert(concurrent.Observe(6,scan.data(),328,100001) && concurrent.Read(wire) && wire.captured_boot_ms == 100001);
    std::puts("PASS conservative AD bounds, exact target, freshness/stale/future, malformed config/views, disabled/rearm and sequence wrap");
    std::printf("PASS100000 concurrent atomic-word publications +1000 disable/rearm cycles; coherent reads=%u\n",reads);
}
