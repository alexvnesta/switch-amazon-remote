// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/bond_properties.hpp"
#include <cassert>
#include <thread>
#include <cstdio>
using namespace amazon_remote;
using namespace amazon_remote::bond_properties;
constexpr SightingAddress Target{0x02,0xab,0xcd,0x12,0x34,0x56};
auto Packet(unsigned kind,unsigned detail,unsigned nonce=0) {
    std::array<std::uint8_t,32> d{};
    auto magic=PacketMagic;std::memcpy(d.data(),&magic,4);std::copy(Target.begin(),Target.end(),d.begin()+4);
    d[10]=kind;d[11]=detail;std::memcpy(d.data()+12,&nonce,4);
    const auto extension=ExtensionMagic,flags=Flags(kind,detail);
    std::memcpy(d.data()+16,&extension,4);std::memcpy(d.data()+20,&flags,4);
    if(flags&1)d[24]=1;if(flags&2)d[25]=16;if(flags&8)d[27]=1;
    return d;
}
int main(){
    Snapshot s;Wire w{};auto p=Packet(0,1,123);
    assert(!s.Read(w));assert(!s.Begin(123,1));assert(!s.Configure({}));assert(s.Configure(Target));assert(!s.Configure(Target));
    assert(s.Read(w)&&Valid(w)&&!w.request_nonce);
    assert(!s.Begin(0,100));assert(!s.Begin(123,0));assert(!s.Begin(123,UINT64_MAX));
    assert(!s.Observe(p.data(),32,100));assert(s.Begin(123,100));assert(!s.Begin(456,101));
    assert(s.Read(w)&&Valid(w)&&w.request_nonce==123&&!w.probe_nonce);
    assert(!s.Observe(nullptr,32,100));assert(!s.Observe(p.data(),31,100));
    assert(!s.Observe(p.data(),32,99));assert(!s.Observe(p.data(),32,15101));
    for(auto offset:{0,4,9,10,11,12,16,20,24,25,26,27,28,29,30,31}){auto bad=p;bad[offset]^=0x80;assert(!s.Observe(bad.data(),32,101));}
    auto key=Packet(12,1);assert(!s.Observe(key.data(),32,101));
    assert(s.Observe(p.data(),32,102));assert(!s.Observe(p.data(),32,103));
    assert(s.Observe(key.data(),32,103));auto auth=Packet(3,1);assert(s.Observe(auth.data(),32,104));
    assert(!s.Observe(key.data(),32,103));auto bad=Packet(12,1,1);assert(!s.Observe(bad.data(),32,105));
    for(unsigned i=0;i<10;++i)assert(s.Observe(key.data(),32,106+i));
    assert(s.Read(w)&&Valid(w)&&w.probe_nonce==123&&w.count==8&&w.overflow==4);
    assert(!s.Observe(key.data(),32,15101));
    auto wrong=w;wrong.events[0].kind=0;assert(!Valid(wrong));wrong=w;wrong.probe_nonce=456;assert(!Valid(wrong));
    wrong=w;wrong.start_ms=UINT64_MAX;assert(!Valid(wrong));
    Snapshot concurrent;assert(concurrent.Configure(Target));assert(concurrent.Begin(123,100));
    std::atomic<bool> done{false};
    std::thread producer([&]{assert(concurrent.Observe(p.data(),32,102));for(unsigned i=0;i<10000;++i)assert(concurrent.Observe(key.data(),32,103+i));done=true;});
    do{Wire copy{};if(concurrent.Read(copy))assert(Valid(copy));}while(!done.load());producer.join();
    assert(concurrent.Read(w)&&w.count==8&&w.overflow==9992);
    // Every legacy selector and scalar presence mask is checked; no union is admitted.
    for(unsigned kind=0;kind<256;++kind) {
        Snapshot check;assert(check.Configure(Target)&&check.Begin(123,100));
        assert(check.Observe(p.data(),32,101));auto value=Packet(12,kind);
        assert(check.Observe(value.data(),32,102)&&check.Read(w)&&Valid(w));
        assert(w.events[0].flags==Flags(12,kind));
        auto bad=value;bad[20]^=0x80;assert(!check.Observe(bad.data(),32,103));
    }
    std::puts("PASS M3: one-shot nonce, time bounds, exact target, metadata-only schema, malformed/stale/replay rejection, overflow and concurrent snapshot");
}
