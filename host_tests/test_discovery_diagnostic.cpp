// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/discovery_diagnostic.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace amazon_remote;
using namespace armodule;
const SightingAddress Target{2,3,4,5,6,7};
std::vector<std::string> Lines;
unsigned Scenarios{};
void Log(const char *line){assert(std::string(line).size()<352);Lines.emplace_back(line);}
bool Live(const CaptureLeaseWire &l,std::uint64_t now){return l.state==1 && now>=l.now_boot_ms && l.deadline_boot_ms>now && l.deadline_boot_ms-now<=60000;}
SightingWire Candidate() {
    SightingWire v{};v.magic=SightingMagic;v.version=1;v.captured_boot_ms=1000;
    v.raw[4]=2;std::copy(Target.begin(),Target.end(),v.raw.begin()+7);v.raw[323]=2;
    v.raw[13]=3;v.raw[14]=3;v.raw[15]=0x12;v.raw[16]=0x18;
    v.raw[44]=7;v.raw[45]=0xff;
    const std::uint8_t prefix[]{0x71,1,4,0x27,2,0x7d};std::copy_n(prefix,6,v.raw.begin()+46);return v;
}
struct Reader {
    std::uint32_t lease_rc{},sighting_rc{};
    CaptureLeaseWire lease{CaptureLeaseMagic,1,1,0,1000,61000};
    SightingWire wire=Candidate();
    std::array<std::uint64_t,3> clock{1500,1500,1500};
    std::string calls;unsigned index{};
    std::uint32_t Lease(CaptureLeaseWire &out){calls+='L';out=lease;return lease_rc;}
    std::uint32_t Sighting(SightingWire &out){calls+='S';out=wire;return sighting_rc;}
    std::uint64_t Clock(){calls+='C';assert(index<clock.size());return clock[index++];}
};
bool Original(Reader &r,CaptureLeaseWire &lease,SightingWire &wire) {
    auto result=r.Lease(lease);if(result || !Live(lease,r.Clock()))return false;
    result=r.Sighting(wire);
    return !result && Live(lease,r.Clock()) && AssessSighting(wire,Target,1000,r.Clock())==SightingAssessment::FreshCandidate;
}
void Check(Reader reader,DiscoveryStep expected,bool permitted=false) {
    ++Scenarios;
    auto old=reader;CaptureLeaseWire a{},b{};SightingWire x{},y{};DiscoveryDiagnostic d;
    Lines.clear();const auto observed=ObserveDiscovery(reader,Live,Target,1000,a,x,d,Log);
    assert(observed==permitted && observed==Original(old,b,y));
    assert(reader.calls==old.calls && reader.index==old.index);
    assert(d.Count(expected)==1);d.Finish(Log);
    for(auto &line:Lines)assert(line.find("DISCOVERY_AD slot=")==std::string::npos || expected==DiscoveryStep::NotCandidate);
}
int main() {
    Reader r;Check(r,DiscoveryStep::FreshCandidate,true);
    r.lease_rc=99;Check(r,DiscoveryStep::LeaseReadError);r={};
    for(unsigned state:{0u,2u,3u,4u}){r.lease.state=state;Check(r,DiscoveryStep::LeaseInactiveBefore);}r={};
    r.sighting_rc=55;Check(r,DiscoveryStep::SightingReadError);r={};
    r.clock[1]=61000;Check(r,DiscoveryStep::LeaseInactiveAfter);r={};
    r.wire={};Check(r,DiscoveryStep::NoSnapshot);r={};
    r.wire.magic=0;Check(r,DiscoveryStep::Invalid);r={};
    r.wire.raw[7]^=1;Check(r,DiscoveryStep::Invalid);r={};
    r.clock[2]=4001;Check(r,DiscoveryStep::Stale);r={};
    r.wire.captured_boot_ms=2000;Check(r,DiscoveryStep::Future);r={};
    r.wire.raw[51]^=1;Check(r,DiscoveryStep::NotCandidate);r={};
    r.wire.raw[14]=7;r.wire.raw[13]=17;Check(r,DiscoveryStep::NotCandidate);r={}; // HID128-only fixture.
    r.wire.raw[323]=0;Check(r,DiscoveryStep::NotCandidate);r={};
    r.wire.raw[44]=31;Check(r,DiscoveryStep::Invalid);r={};
    // Differential testing: diagnostics must not change admission, output wire,
    // clock sampling or exact getter count/order for arbitrary callback bytes.
    std::uint32_t random=17;
    for(unsigned trial=0;trial<4096;++trial) {
        r={};random=random*1664525u+1013904223u;
        r.wire.raw[random%r.wire.raw.size()]^=std::uint8_t(random>>24);
        r.clock[2]=1000+(random%64000);
        auto old=r;CaptureLeaseWire a{},b{};SightingWire x{},y{};DiscoveryDiagnostic d;
        const bool observed=ObserveDiscovery(r,Live,Target,1000,a,x,d,nullptr);
        assert(observed==Original(old,b,y) && r.calls==old.calls && r.index==old.index);
        assert(!std::memcmp(&a,&b,sizeof a) && !std::memcmp(&x,&y,sizeof x));
    }
    DiscoveryDiagnostic bounded;Lines.clear();DiscoverySample sample{};
    for(unsigned i=0;i<1000;++i){sample.step=i%2?DiscoveryStep::NoSnapshot:DiscoveryStep::Stale;bounded.Record(sample,Log);}
    assert(Lines.size()==DiscoveryDiagnostic::TransitionLimit);bounded.Finish(Log);assert(Lines.size()==10);
    const auto count=Lines.size();bounded.Finish(Log);bounded.Record(sample,Log);assert(Lines.size()==count);
    DiscoveryDiagnostic dump;Lines.clear();auto wire=Candidate();wire.raw[323]=10;
    sample.step=DiscoveryStep::NotCandidate;
    dump.Record(sample,Log,&wire);dump.Record(sample,Log,&wire);dump.Finish(Log);
    unsigned slots{};for(const auto &line:Lines)if(line.find("DISCOVERY_AD slot=")!=std::string::npos)++slots;
    assert(slots==10 && Lines.size()==14);
    std::printf("PASS discovery diagnostics:%u stage/AD scenarios,4096 differential admission/getter/clock/byte checks; bounded8 transitions, one10-slot sample, terminal summary; no SDK clients or radio calls.\n",Scenarios);
}
