// SPDX-License-Identifier: GPL-2.0-only
#include "../../../mc_mitm/source/amazon_remote/early_link_retry.hpp"
#include <cassert>
#include <cstdio>
using namespace amazon_remote;
using D=EarlyLinkRetry::Decision;
struct Fixture {
    EarlyLinkRetry gate;
    ConnectionEvidenceWire events;
    central_bond::HciTrace hci;
    Fixture(){hci.supported=1;assert(gate.Configure(7,10000,70000));assert(gate.Begin(events,hci,10560));}
    void Failure(std::uint64_t request=10560) {
        hci.window_nonce=7;hci.capture_start_ms=10000;
        hci.entries[hci.count++]={request+14,0,0};
        events.events[events.target_count++]={request+30,0,4,0,4,0,0};
        events.events[events.target_count++]={request+126,0x1a,4,2,4,0x3e,0};
    }
    D Observe(std::uint64_t now=10800){return gate.Observe(events,hci,now);}
};
int main(){
    // Replay the failed boot: no overlapping command, one second backoff and
    // two absent views >=250ms apart, then precisely one next request.
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);assert(f.gate.Absent(10800,true)==D::Settle);
     assert(f.gate.Absent(11049,true)==D::Settle);assert(!f.gate.CompletedAbsent());
     assert(f.gate.Absent(11050,true)==D::Settle);assert(f.gate.CompletedAbsent());
     assert(f.gate.Absent(11686,true)==D::Retry);assert(f.gate.Begin(f.events,f.hci,11687));
     assert(!f.gate.Begin(f.events,f.hci,11688));assert(f.gate.Attempts()==2);
     assert(f.Observe(11689)==D::Waiting);f.Failure(11687);assert(f.Observe(12000)==D::Settle);
     assert(f.gate.Absent(12000,true)==D::Settle);assert(f.gate.Absent(13000,true)==D::Retry);
     assert(f.gate.Begin(f.events,f.hci,13001));f.Failure(13001);assert(f.Observe(13300)==D::Settle);
     assert(f.gate.Absent(13300,true)==D::Settle);assert(f.gate.Absent(14500,true)==D::Refuse);
     assert(f.gate.CompletedAbsent());assert(f.gate.Attempts()==3);}
    {Fixture f;assert(f.Observe()==D::Waiting);assert(f.gate.Absent(12000,true)==D::Refuse);}
    {Fixture f;f.Failure();f.events.target_count=1;f.events.events[1]={};assert(f.Observe()==D::Waiting);}
    for(unsigned variant=0;variant<18;++variant){Fixture f;f.Failure();
        switch(variant){
            case 0:f.events.events[1].client_if=5;break;
            case 1:f.events.events[1].connection_id=5;break;
            case 2:f.events.events[1].reason=0x13;break;
            case 3:f.events.events[1].result=1;break;
            case 4:f.events.events[0].boot_ms=10559;break;
            case 5:f.events.events[1].status=0;break;
            case 6:f.hci.entries[0].status=12;break;
            case 7:f.hci.entries[0].own_address_type=1;break;
            case 8:f.hci.window_nonce=8;break;
            case 9:f.hci.entries[0].boot_ms=10559;break;
            case 10:f.hci.entries[0].boot_ms=10600;break;
            case 11:f.hci.count=2;f.hci.entries[1]={10600,0,0};break;
            case 12:f.hci.dropped=1;break;
            case 13:f.events.target_count=3;f.events.events[2]=f.events.events[1];break;
            case 14:f.events.events[1].boot_ms=12000;break;
            case 15:f.events.events[0].reserved=1;break;
            case 16:f.events.events[0].connection_id=0xffffffff;break;
            case 17:f.events.events[0].result=1;break;
        }assert(f.Observe()==D::Refuse);
    }
    {Fixture f;f.Failure();f.hci={};f.hci.supported=1;assert(f.Observe()==D::Waiting);}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);assert(f.gate.Absent(10800,false)==D::Refuse);}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);assert(f.gate.Absent(10799,true)==D::Refuse);}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);f.events.events[0].client_if=9;assert(f.Observe(10900)==D::Refuse);}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);f.hci.entries[0].status=1;assert(f.Observe(10900)==D::Refuse);}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);assert(f.gate.Absent(41000,true)==D::Settle);
     assert(f.gate.Absent(41250,true)==D::Refuse);}
    // A normal connected snapshot does not request recovery or grant input.
    {Fixture f;f.hci.window_nonce=7;f.hci.capture_start_ms=10000;f.hci.entries[f.hci.count++]={10574,0,0};
     f.events.events[f.events.target_count++]={20182,0,4,0,4,0,0};
     assert(f.Observe(20200)==D::Waiting);assert(!f.gate.CompletedAbsent());}
    // Events arriving during journaling/preflight must not become the next
    // request's baseline. No unobserved result can be skipped over.
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);assert(f.gate.Absent(10800,true)==D::Settle);
     assert(f.gate.Absent(12000,true)==D::Retry);f.events.events[f.events.target_count++]=f.events.events[1];
     assert(!f.gate.Begin(f.events,f.hci,12001));}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);assert(f.gate.Absent(10800,true)==D::Settle);
     assert(f.gate.Absent(12000,true)==D::Retry);assert(!f.gate.Begin(f.events,f.hci,40001));}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);f.events={};assert(f.Observe(10900)==D::Refuse);}
    {Fixture f;f.Failure();assert(f.Observe()==D::Settle);f.hci.count=0;f.hci.entries[0]={};assert(f.Observe(10900)==D::Refuse);}
    {Fixture f;f.Failure();assert(f.Observe(70000)==D::Refuse);}
    // A sleep-resume session can have immutable older target callbacks. Only
    // the new suffix can satisfy this attempt, never that previous history.
    {EarlyLinkRetry g;ConnectionEvidenceWire e;central_bond::HciTrace h;h.supported=1;
     e.events[0]={9000,0,1,0,4,0,0};e.events[1]={9500,0,1,2,4,0x13,0};e.target_count=2;
     assert(g.Configure(7,10000,70000));assert(g.Begin(e,h,10560));assert(g.Observe(e,h,10800)==D::Waiting);
     h.window_nonce=7;h.capture_start_ms=10000;h.entries[h.count++]={10574,0,0};
     e.events[e.target_count++]={10590,0,4,0,4,0,0};e.events[e.target_count++]={10686,0x1a,4,2,4,0x3e,0};
     assert(g.Observe(e,h,10900)==D::Settle);assert(g.Absent(10900,true)==D::Settle);assert(g.Absent(12000,true)==D::Retry);}
    std::puts("early link retry: replay, bound, history, identity and timing checks passed");
}
