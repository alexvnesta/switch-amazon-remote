// SPDX-License-Identifier: GPL-2.0-only
#include "../control/security_capture.hpp"
#include <cassert>
#include <cstdio>
using namespace arsecuritycapture;
struct Fake {
    std::uint64_t now=1000;
    std::uint32_t read_error{},probe_error{},nonce=77;
    unsigned reads{},probes{},saves{},waits{};
    bool known=true,echo=true,save_ok=true,invalid_wire=false;
    Wire wire{};Report saved{};
    auto Now(){return now;}
    auto Nonce(){return nonce;}
    void Wait(){now+=10;++waits;}
    ardriverid::Record Identity(){
        ardriverid::Record r{};r.firmware=ardriverid::Firmware;r.begin_ms=r.end_ms=now;
        r.pid_before=r.pid_after=103;r.pm_init=r.pm_before=r.loader_init=r.modules_result=r.pm_after=0;
        r.count=1;r.matches=known?1:0;r.build_ids[0]=ardriverid::Expected;
        if(!known)r.build_ids[0][0]^=1;
        r.verdict=unsigned(ardriverid::Evaluate(r));r.checksum=ardriverid::Checksum(r);return r;
    }
    std::uint32_t Read(Wire&w){++reads;w=wire;if(invalid_wire)w.magic=0;return read_error;}
    std::uint32_t Probe(std::uint32_t n){
        ++probes;if(probe_error)return probe_error;
        wire.request_nonce=n;wire.start_ms=now;wire.deadline_ms=now+15000;
        if(echo){wire.probe_nonce=n;wire.probe_ms=now;}
        return 0;
    }
    bool Save(const Report&r){++saves;saved=r;assert(Valid(r));return save_ok;}
    void Event(unsigned kind,unsigned detail){
        auto &e=wire.events[wire.count++];e={now,kind,detail};
#ifdef ARMODULE_BOND_PROPERTIES
        e.flags=metadata::Flags(kind,detail);
        if(e.flags&1)e.security=1;
        if(e.flags&2)e.key_bytes=16;
        if(e.flags&8)e.identity_match=1;
#endif
    }
};
void Start(Capture&c,Fake&b){assert(c.Prepare(b));assert(c.BeforeLink(b,42,2));c.Published(b,true);}
int main(){
    { Fake b;Capture c(true);assert(!c.BeforeLink(b,42,2));assert(!b.probes);
      Start(c,b);assert(c.Running()&&b.probes==1);b.now+=3000;b.Event(12,1);c.Poll(b);
      b.now+=5000;b.Event(3,0);c.Poll(b);b.now=16000;c.Poll(b);
      assert(c.Finished()&&c.Saved()&&b.saves==1&&b.saved.outcome==1&&b.saved.evidence.count==2);
      assert(!c.Prepare(b)&&!c.BeforeLink(b,42,3));c.Poll(b);c.Exit(b);assert(b.saves==1);
      for(unsigned i=0;i<sizeof b.saved;++i){auto bad=b.saved;reinterpret_cast<unsigned char*>(&bad)[i]^=1;assert(!Valid(bad));}
      auto bad=b.saved;bad.link_published_ms=999;bad.checksum=Checksum(bad);assert(!Valid(bad));
      bad=b.saved;bad.nonce++;bad.checksum=Checksum(bad);assert(!Valid(bad));
      bad=b.saved;bad.outcome=2;bad.checksum=Checksum(bad);assert(!Valid(bad));
    }
    {Fake b;Capture c(true);Start(c,b);b.now=16000;c.Poll(b);assert(c.Saved()&&b.saved.outcome==2);}
    {Fake b;Capture c(false);assert(!c.Prepare(b)&&!c.BeforeLink(b,42,2));assert(!b.reads&&!b.probes&&!b.saves);}
    {Fake b;b.known=false;Capture c(true);assert(!c.Prepare(b));assert(c.Saved()&&!b.probes&&!b.reads);}
    {Fake b;b.read_error=9;Capture c(true);assert(!c.Prepare(b));assert(c.Saved()&&!b.probes);}
    {Fake b;b.invalid_wire=true;Capture c(true);assert(!c.Prepare(b));assert(!b.probes);}
    {Fake b;b.Probe(99);Capture c(true);assert(!c.Prepare(b));assert(b.probes==1);}
    {Fake b;Capture c(true);assert(c.Prepare(b));b.Probe(99);assert(!c.BeforeLink(b,42,2));assert(b.probes==1);}
    {Fake b;Capture c(true);assert(c.Prepare(b));b.probe_error=9;assert(!c.BeforeLink(b,42,2));assert(c.Saved()&&b.probes==1);}
    {Fake b;Capture c(true);assert(c.Prepare(b));b.nonce=0;assert(!c.BeforeLink(b,42,2));assert(!b.probes);}
    {Fake b;Capture c(true);assert(c.Prepare(b));b.echo=false;assert(!c.BeforeLink(b,42,2));assert(c.Saved()&&b.now<=1210&&b.probes==1);}
    {Fake b;Capture c(true);assert(c.Prepare(b));assert(!c.BeforeLink(b,0,2));assert(!c.BeforeLink(b,42,0));assert(!b.probes);}
    {Fake b;Capture c(true);assert(c.Prepare(b));assert(c.BeforeLink(b,42,2));c.Published(b,false);assert(c.Saved()&&b.saved.outcome==6);}
    {Fake b;Capture c(true);Start(c,b);c.Exit(b);assert(c.Saved()&&b.saved.outcome==5);}
    {Fake b;Capture c(true);assert(c.Prepare(b));c.Exit(b);assert(c.Saved()&&b.saved.outcome==5&&!b.probes);}
    {Fake b;Capture c(true);Start(c,b);b.save_ok=false;b.now=16000;c.Poll(b);assert(c.Finished()&&!c.Saved());c.Exit(b);assert(b.saves==1);}
    for(unsigned fault=0;fault<9;++fault){
        Fake b;Capture c(true);Start(c,b);b.now+=100;b.Event(12,1);c.Poll(b);b.now+=100;
        switch(fault){
            case 0:b.read_error=9;break;
            case 1:b.wire.request_nonce++;break;
            case 2:b.wire.probe_nonce++;break;
            case 3:b.wire.count=0;b.wire.events={};break;
            case 4:b.wire.events[0].detail++;break;
            case 5:b.wire.events[0].boot_ms=b.now+1;break;
            case 6:b.wire.start_ms++;b.wire.deadline_ms++;break;
            case 7:b.wire.magic=0;break;
            case 8:b.wire.events[0].kind=99;break;
        }
        c.Poll(b);assert(c.Finished()&&c.Saved()&&b.saved.outcome==4&&b.saves==1);
    }
    {Fake b;Capture c(true);Start(c,b);c.Poll(b);const auto reads=b.reads;b.now+=99;c.Poll(b);assert(b.reads==reads);}
    {Fake b;Capture c(true);Start(c,b);b.now=900;c.Poll(b);assert(c.Finished()&&!c.Saved());}
    {Fake b;Capture c(true);Start(c,b);b.now=15999;for(unsigned i=0;i<8;++i)b.Event(12,i);b.wire.overflow=3;
      c.Poll(b);assert(c.Running());b.now=16000;c.Poll(b);assert(c.Saved()&&b.saved.evidence.overflow==3);}
#ifdef ARMODULE_BOND_PROPERTIES
    std::puts("PASS M3: inherited capture gates, exact property schema, all976-byte corruption, no secret material or persistence claim.");
#else
    std::puts("PASS M2: exact nonce before Link; one-shot bounded probe; reused boot/report refusal; no-event/metadata outcomes; immutable snapshots; errors/cancellation/save failure; all848-byte corruption. No pairing or persistence claim.");
#endif
}
