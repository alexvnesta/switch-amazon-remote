// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/core_security_evidence.hpp"
#include "../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace amazon_remote;
using Raw=std::array<std::uint8_t,0x108>;
const SightingAddress target{0x02,0xab,0xcd,0x12,0x34,0x56};
Raw Event(unsigned kind,std::uint32_t detail=0) {
    Raw raw{};
    // Poison names, passkey and reserved bytes. None should reach the wire.
    raw.fill(0xee);
    std::copy(target.begin(),target.end(),raw.begin()+(kind==4 ? 4 : 0));
    if (kind==3) raw[0x102]=static_cast<std::uint8_t>(detail);
    if (kind==4) for (unsigned i=0;i<4;++i) raw[i]=detail>>(8*i);
    return raw;
}
int main() {
    CoreSecuritySnapshot snapshot;
    CoreSecurityWire wire{}; wire.magic=99;
    assert(!snapshot.Read(wire) && wire.magic==99);
    assert(!snapshot.Configure(SightingAddress{}) && snapshot.Configure(target) && !snapshot.Configure(target));
    assert(snapshot.Read(wire) && ValidCoreSecurity(wire) && !wire.target_count);
    for (unsigned kind=2;kind<=4;++kind) {
        const auto raw=Event(kind,0xdeadbeef); const auto saved=raw;
        assert(!snapshot.Observe(kind,nullptr,raw.size(),1,true,true));
        for (unsigned size=0;size<(kind==2 ? 0x102u : 0x108u);++size)
            assert(!snapshot.Observe(kind,raw.data(),size,1,true,true));
        assert(!snapshot.Observe(kind,raw.data(),raw.size(),0,true,true));
        assert(!snapshot.Observe(kind,raw.data(),raw.size(),1,false,true));
        assert(!snapshot.Observe(kind,raw.data(),raw.size(),1,true,false));
        auto foreign=raw; foreign[kind==4 ? 4 : 0]^=1;
        assert(!snapshot.Observe(kind,foreign.data(),foreign.size(),1,true,true));
        assert(snapshot.Observe(kind,raw.data(),raw.size(),100+kind,true,true));
        assert(raw==saved && snapshot.Read(wire) && wire.target_count==kind-1);
        const auto &e=wire.events[kind-2];
        assert(e.boot_ms==100+kind && e.kind==kind);
        assert(e.detail==(kind==2 ? 0 : (kind==3 ? 0xef : 0xdeadbeef)));
    }
    const auto pin=Event(2),ssp=Event(3,1),link=Event(4,2);
    for (unsigned kind:{0u,1u,5u,10u,0x69u,0xffffffffu})
        assert(!snapshot.Observe(kind,ssp.data(),ssp.size(),105,true,true));
    assert(!snapshot.Observe(3,ssp.data(),ssp.size(),103,true,true));
    for (unsigned i=0;i<5;++i) assert(snapshot.Observe(3,ssp.data(),ssp.size(),105+i,true,true));
    for (unsigned i=0;i<20;++i) assert(snapshot.Observe(4,link.data(),link.size(),110+i,true,true));
    assert(snapshot.Read(wire) && ValidCoreSecurity(wire) && wire.target_count==8 && wire.overflow==20);
    assert(wire.events[0].kind==2 && wire.events[7].boot_ms==109); // First events retained.
    auto bad=wire; bad.magic=0; assert(!ValidCoreSecurity(bad));
    bad=wire; bad.version=0; assert(!ValidCoreSecurity(bad));
    bad=wire; bad.target_count=9; assert(!ValidCoreSecurity(bad));
    bad=wire; bad.events[0].detail=1; assert(!ValidCoreSecurity(bad));
    bad=wire; bad.events[1].detail=256; assert(!ValidCoreSecurity(bad));
    bad=wire; bad.events[1].kind=1; assert(!ValidCoreSecurity(bad));
    bad=wire; bad.events[1].boot_ms=101; assert(!ValidCoreSecurity(bad));
    bad=CoreSecurityWire{}; bad.events[7].detail=1; assert(!ValidCoreSecurity(bad));
    bad=CoreSecurityWire{}; bad.overflow=1; assert(!ValidCoreSecurity(bad));

    // Same wire despite changed address-independent secrets/names/reserved data.
    for (unsigned kind=2;kind<=4;++kind) {
        auto a=Event(kind,1),b=a;
        for (unsigned i=(kind==4 ? 10u : 6u);i<b.size();++i)
            if (kind!=3 || i!=0x102) b[i]^=0xff;
        CoreSecuritySnapshot left,right;
        assert(left.Configure(target) && right.Configure(target));
        assert(left.Observe(kind,a.data(),a.size(),1,true,true));
        assert(right.Observe(kind,b.data(),b.size(),1,true,true));
        CoreSecurityWire x{},y{};
        assert(left.Read(x) && right.Read(y) && std::memcmp(&x,&y,sizeof x)==0);
    }
    CoreSecuritySnapshot wrapping(0xfffffffe);
    assert(wrapping.Configure(target) && wrapping.Observe(2,pin.data(),pin.size(),1,true,true));
    assert(wrapping.Read(wire) && ValidCoreSecurity(wire));
    CoreSecuritySnapshot bounded; assert(bounded.Configure(target));
    OneShotCaptureLease lease;
    assert(!bounded.Observe(3,ssp.data(),ssp.size(),100,lease.Active(100),true));
    assert(lease.Arm(100));
    assert(bounded.Observe(3,ssp.data(),ssp.size(),101,lease.Active(101),true));
    assert(!bounded.Observe(3,ssp.data(),ssp.size(),60100,lease.Active(60100),true));
    assert(!lease.Arm(60101)); lease.End();
    assert(!bounded.Observe(3,ssp.data(),ssp.size(),60102,lease.Active(60102),true));
    assert(bounded.Read(wire) && wire.target_count==1); // Retained after expiry/end.
    CoreSecuritySnapshot concurrent; assert(concurrent.Configure(target));
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done.load(std::memory_order_acquire)) {
            CoreSecurityWire value{};
            if (concurrent.Read(value)) {
                assert(ValidCoreSecurity(value));
                for (unsigned i=0;i<value.target_count;++i)
                    assert(value.events[i].kind==3 && value.events[i].detail==1);
            }
        }
    });
    for (unsigned i=1;i<=10000;++i) assert(concurrent.Observe(3,ssp.data(),ssp.size(),i,true,true));
    done.store(true,std::memory_order_release); reader.join();
    assert(concurrent.Read(wire) && wire.target_count==8 && wire.overflow==9992);
    std::puts("PASS passive core metadata: target/layout/lease gates, no secrets or payload mutation, bounds, overflow, wrap and concurrent coherent reads; bond completion NOT inferred");
}
