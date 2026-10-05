// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/connection_evidence.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace amazon_remote;
using Raw=std::array<std::uint8_t,20>;
const SightingAddress target{0x02,0xab,0xcd,0x12,0x34,0x56};
Raw Event(std::uint32_t result=0,std::uint8_t status=2) {
    Raw raw{};for (unsigned i=0;i<4;++i) raw[i]=result>>(i*8);
    raw[4]=status;raw[5]=9;raw[8]=4;raw[18]=0x3e;raw[19]=0x12;
    std::copy(target.begin(),target.end(),raw.begin()+12);return raw;
}
int main() {
    ConnectionEvidenceSnapshot snapshot;ConnectionEvidenceWire wire{};wire.magic=99;
    assert(!snapshot.Read(wire) && wire.magic==99);
    assert(snapshot.Configure(target) && !snapshot.Configure(target));
    assert(snapshot.Read(wire) && ValidConnectionEvidence(wire) && !wire.target_count);
    auto raw=Event(0xdeadbeef,255);const auto saved=raw;
    assert(!snapshot.Observe(6,raw.data(),20,1));
    assert(!snapshot.Observe(4,nullptr,20,1));
    for (unsigned size=0;size<20;++size) assert(!snapshot.Observe(4,raw.data(),size,1));
    assert(!snapshot.Observe(4,raw.data(),20,0));
    auto foreign=raw;foreign[12]^=1;assert(!snapshot.Observe(4,foreign.data(),20,1));
    assert(snapshot.Observe(4,raw.data(),20,100));
    assert(raw==saved && snapshot.Read(wire) && wire.target_count==1);
    const auto &first=wire.events[0];
    assert(first.result==0xdeadbeef && first.status==255 && first.reason==0x123e && first.client_if==9 && first.connection_id==4);
    assert(!snapshot.Observe(4,raw.data(),20,99));
    for (unsigned i=1;i<8;++i) {
        raw=Event(i,i);assert(snapshot.Observe(4,raw.data(),20,100+i));
    }
    for (unsigned i=0;i<20;++i) assert(snapshot.Observe(4,raw.data(),20,200+i));
    assert(snapshot.Read(wire) && ValidConnectionEvidence(wire) && wire.target_count==8 && wire.overflow==20);
    assert(wire.events[0].result==0xdeadbeef && wire.events[7].boot_ms==107); // Never overwrite first failure.
    auto bad=wire;bad.target_count=9;assert(!ValidConnectionEvidence(bad));
    bad=wire;bad.events[1].boot_ms=99;assert(!ValidConnectionEvidence(bad));
    bad=wire;bad.events[0].reserved=1;assert(!ValidConnectionEvidence(bad));
    bad=wire;bad.reserved0=1;assert(!ValidConnectionEvidence(bad));
    bad=wire;bad.magic=0;assert(!ValidConnectionEvidence(bad));
    bad=ConnectionEvidenceWire{};bad.events[7].reason=1;assert(!ValidConnectionEvidence(bad));
    bad=ConnectionEvidenceWire{};bad.overflow=1;assert(!ValidConnectionEvidence(bad));
    ConnectionEvidenceSnapshot wrapping(0xfffffffe);assert(wrapping.Configure(target));
    assert(wrapping.Observe(4,saved.data(),20,1) && wrapping.Read(wire) && ValidConnectionEvidence(wire));
    ConnectionEvidenceSnapshot concurrent;assert(concurrent.Configure(target));std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            ConnectionEvidenceWire value{};
            if (concurrent.Read(value)) {
                assert(ValidConnectionEvidence(value));
                for (unsigned i=0;i<value.target_count;++i) assert(value.events[i].result==0xdeadbeef && value.events[i].reason==0x123e);
            }
        }
    });
    for (unsigned i=1;i<=10000;++i) assert(concurrent.Observe(4,saved.data(),20,i));
    done.store(true,std::memory_order_release);reader.join();
    assert(concurrent.Read(wire) && wire.target_count==8 && wire.overflow==9992);
    std::puts("PASS target connection evidence: errors/status retained; privacy, bounds, overflow, sequence wrap and concurrent coherent reads");
}
