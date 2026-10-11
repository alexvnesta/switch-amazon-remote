// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-pairing-audit/core_security_reader.hpp"
#include <cassert>
#include <cstdio>
using namespace amazon_remote;
unsigned opens{},queries{},closes{};
Result open_result{},query_result{};CoreSecurityWire response{};
std::uint64_t clock_begin{1000},clock_end{1001};unsigned clock_calls{};
u64 armGetSystemTick(){return clock_calls++?clock_end:clock_begin;}
u64 armTicksToNs(u64 t){return t*1000000;}
Result smGetService(Service *s,const char *name){
    ++opens;assert(std::strcmp(name,"mc")==0);s->active=!open_result;return open_result;
}
bool serviceIsActive(Service *s){return s->active;}
void serviceClose(Service *s){assert(s->active);++closes;s->active=false;}
Result FakeSecurityDispatch(Service *s,unsigned command,FakeSecurityDispatchParams p){
    assert(s->active && command==65005 && p.buffer_attrs[0]==(SfBufferAttr_HipcPointer|SfBufferAttr_Out));
    assert(p.buffers[0].size==sizeof response && p.buffers[0].data);
    ++queries;std::memcpy(p.buffers[0].data,&response,sizeof response);return query_result;
}
void Reset(){opens=queries=closes=clock_calls=0;open_result=query_result=0;response={};clock_begin=1000;clock_end=1001;}
arsecurity::Snapshot Collect(){arsecurity::Backend backend;return arsecurity::Collect(backend);}
int main(){
    Reset();auto s=Collect();assert(arsecurity::Valid(s) && !s.validation_result && !s.evidence.target_count);
    assert(opens==1 && queries==1 && closes==1);
    Reset();response.target_count=1;response.events[0]={999,3,1};s=Collect();
    assert(arsecurity::Valid(s) && s.evidence.target_count==1 && s.evidence.events[0].detail==1);
    Reset();open_result=123;s=Collect();assert(arsecurity::Valid(s) && s.open_result==123);
    assert(opens==1 && !queries && !closes && s.query_result==arsecurity::NotAttempted);
    Reset();query_result=456;response.magic=0;response.events[0]={888,3,0xeeeeeeee};s=Collect();
    assert(arsecurity::Valid(s) && s.query_result==456 && !s.evidence.target_count && !s.evidence.events[0].boot_ms);
    assert(queries==1 && closes==1);
    for(unsigned mode=0;mode<7;++mode){
        Reset();response.target_count=1;response.events[0]={999,3,1};
        if(mode==0)response.magic=0;
        if(mode==1)response.target_count=9;
        if(mode==2)response.events[0].boot_ms=1002;
        if(mode==3)response.events[0].detail=256;
        if(mode==4)clock_begin=0;
        if(mode==5)clock_end=999;
        if(mode==6)clock_end=3001;
        s=Collect();assert(arsecurity::Valid(s) && s.validation_result==arsecurity::InvalidEvidence);
        assert(!s.evidence.target_count && !s.evidence.events[0].boot_ms && queries==1 && closes==1);
    }
    Reset();response.target_count=8;response.overflow=55;
    for(unsigned i=0;i<8;++i)response.events[i]={990+i,4,0xffffffff};
    s=Collect();assert(arsecurity::Valid(s) && s.evidence.overflow==55);
    // Every byte of the checksum-covered frame and reserved tail is protected.
    for(unsigned i=0;i<sizeof s;++i){
        auto bad=s;reinterpret_cast<unsigned char *>(&bad)[i]^=1;assert(!arsecurity::Valid(bad));
    }
    std::puts("PASS actual read-only MC getter adapter: fixed command/buffer, independent errors/close, malformed/future/timing rejection, no partial-data publication, 192-byte corruption checks; no radio/pair/event/key calls. NOT hardware proof.");
}
