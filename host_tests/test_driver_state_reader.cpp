// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-pairing-audit/driver_state_reader.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace {
Result open_result{},query_result{};std::uint8_t enabled{};
unsigned opens{},queries{},closes{},clock_reads{};
std::uint64_t begin=100000000,end=101000000;
void Reset(){open_result=query_result=0;enabled=1;opens=queries=closes=clock_reads=0;begin=100000000;end=101000000;}
ardriver::Snapshot Read(){ardriver::Backend backend;return ardriver::Collect(backend);}
}
u64 armGetSystemTick(){return clock_reads++?end:begin;}
u64 armTicksToNs(u64 ticks){return ticks;}
Result smGetService(Service *s,const char *name){assert(!std::strcmp(name,"btdrv"));++opens;s->active=!open_result;return open_result;}
bool serviceIsActive(Service *s){return s->active;}
void serviceClose(Service *s){assert(s->active);s->active=false;++closes;}
Result FakeDriverStateDispatch(Service *s,unsigned command,void *out,std::size_t size){
    assert(s->active && command==100 && size==1);++queries;
    *static_cast<std::uint8_t *>(out)=enabled;return query_result;
}
int main(){
    Reset();auto s=Read();assert(ardriver::Valid(s) && ardriver::EnabledObserved(s));
    assert(opens==1 && queries==1 && closes==1 && clock_reads==2);
    for(unsigned i=0;i<sizeof(s);++i){auto bad=s;reinterpret_cast<unsigned char *>(&bad)[i]^=1;assert(!ardriver::Valid(bad));}
    Reset();enabled=0;s=Read();assert(ardriver::Valid(s) && !ardriver::EnabledObserved(s) && !s.validation_result);
    Reset();open_result=44;s=Read();assert(ardriver::Valid(s) && s.query_result==ardriver::NotAttempted && !s.enabled_raw);
    assert(opens==1 && queries==0 && closes==0 && !ardriver::EnabledObserved(s));
    Reset();query_result=55;s=Read();assert(ardriver::Valid(s) && s.query_result==55 && !s.enabled_raw && closes==1);
    assert(!ardriver::EnabledObserved(s));
    for(unsigned raw=2;raw<=255;++raw){Reset();enabled=raw;s=Read();assert(ardriver::Valid(s) && s.validation_result==ardriver::InvalidEvidence && !s.enabled_raw && !ardriver::EnabledObserved(s));}
    for(const auto wrong_end:{99999999ull,2101000001ull}){Reset();end=wrong_end;s=Read();assert(!ardriver::Valid(s) && !ardriver::EnabledObserved(s));}
    Reset();begin=0;s=Read();assert(!ardriver::Valid(s) && !ardriver::EnabledObserved(s));
    Reset();s=Read();s.enabled_raw=2;s.checksum=ardriver::Checksum(s);assert(!ardriver::Valid(s));
    s=Read();s.command=10;s.checksum=ardriver::Checksum(s);assert(!ardriver::Valid(s));
    std::puts("PASS driver-state getter: owned session, command100/one byte only, errors/malformed values inconclusive, clocks/checksum and every byte checked");
}
