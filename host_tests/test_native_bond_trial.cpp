// SPDX-License-Identifier: GPL-2.0-only
#define ARMODULE_NAV17
#define ARMODULE_NAV16
#define ARMODULE_NAV15
#include "../tools/amazon-remote-pairing-audit/native_bond_trial.hpp"
#include "../tools/amazon-remote-pairing-audit/native_bond_guard.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <vector>
namespace {
Result open_result{},getter_result{},request_result{};std::uint8_t enabled=1;
unsigned opens{},getters{},requests{},closes{};
BtdrvAddress target{{0x02,0xab,0xcd,0x12,0x34,0x56}};
void Reset(){open_result=getter_result=request_result=0;enabled=1;opens=getters=requests=closes=0;}
struct Backend:arnativebond::Driver {
    std::uint64_t time=1000;bool supported=true,confirmed=true,persist=true;
    unsigned guards{},intents{},bad_guard{},change_binding{};std::uint64_t delay{};
    std::uint64_t Now(){return time+=10;}
    bool Supported(){return supported;}bool OperatorConfirmed(){return confirmed;}
    Result Guard(BtdrvAddress t,arnativebond::Binding &b){
        assert(!std::memcmp(&t,&target,sizeof t));++guards;b={42,4};
        if(guards==2){time+=delay;if(change_binding==1)++b.instance;if(change_binding==2)++b.handle;}
        return guards==bad_guard?44:0;
    }
    bool PersistIntent(const arnativebond::Record &r){
        ++intents;assert(arnativebond::Valid(r) && !r.request_issued && r.request_result==arnativebond::Unknown && !requests);return persist;
    }
};
void Sign(armodule::Status &s,armodule::StartupRecord &p,amazon_remote::EnrollmentContextRecord &c){
    s.reserved=armodule::StatusChecksum(s);p.checksum=armodule::StartupChecksum(p);
    c.snapshot.checksum=araudit::Checksum(c.snapshot);c.checksum=amazon_remote::EnrollmentContextChecksum(c);
}
}
Result smGetService(Service *s,const char *name){assert(!std::strcmp(name,"btdrv"));++opens;s->active=!open_result;return open_result;}
bool serviceIsActive(Service *s){return s->active;}
void serviceClose(Service *s){assert(s->active);s->active=false;++closes;}
Result FakeDriverStateDispatch(Service *s,unsigned cmd,void *out,std::size_t n){
    assert(s->active && cmd==100 && n==1);++getters;*static_cast<std::uint8_t *>(out)=enabled;return getter_result;
}
Result FakeNativeBondDispatch(Service *s,unsigned cmd,const void *in,std::size_t n){
    assert(s->active && cmd==10 && n==12 && getters==1);++requests;
    const auto *bytes=static_cast<const std::uint8_t *>(in);
    assert(!std::memcmp(bytes,target.address,6) && !bytes[6] && !bytes[7]);
    std::uint32_t transport{};std::memcpy(&transport,bytes+8,4);assert(transport==2);return request_result;
}
int main(){
    Reset();Backend b;arnativebond::Trial trial;auto r=trial.Attempt(b,target);
    assert(arnativebond::Valid(r) && r.request_issued==1 && r.state==unsigned(arnativebond::State::AcceptedOutcomeUnknown));
    assert(opens==1 && getters==1 && requests==1 && closes==1 && b.guards==2 && b.intents==1);
    const auto second=trial.Attempt(b,target);assert(arnativebond::Valid(second) && !second.request_issued && requests==1);
    for(unsigned i=0;i<sizeof r;++i){auto bad=r;reinterpret_cast<unsigned char *>(&bad)[i]^=1;assert(!arnativebond::Valid(bad));}
    for(unsigned scenario=0;scenario<10;++scenario){
        Reset();Backend x;arnativebond::Trial t;
        if(scenario==0)x.supported=false;if(scenario==1)x.confirmed=false;
        if(scenario==2)x.bad_guard=1;if(scenario==3)open_result=55;
        if(scenario==4)getter_result=66;if(scenario==5)enabled=0;
        if(scenario==6)x.persist=false;if(scenario==7)x.bad_guard=2;
        if(scenario==8)x.change_binding=1;if(scenario==9)x.change_binding=2;
        r=t.Attempt(x,target);assert(arnativebond::Valid(r) && !r.request_issued && !requests);
    }
    for(unsigned raw=2;raw<=255;++raw){Reset();enabled=raw;Backend x;arnativebond::Trial t;r=t.Attempt(x,target);assert(arnativebond::Valid(r) && !requests && !r.enabled);}
    Reset();Backend delayed;delayed.delay=2001;arnativebond::Trial dt;r=dt.Attempt(delayed,target);assert(arnativebond::Valid(r) && !requests);
    Reset();request_result=77;Backend failed;arnativebond::Trial ft;r=ft.Attempt(failed,target);assert(arnativebond::Valid(r) && r.state==unsigned(arnativebond::State::RequestFailed) && requests==1 && closes==1);
    for(bool broadcast:{false,true}){Reset();BtdrvAddress bad{};if(broadcast)std::memset(&bad,255,sizeof bad);Backend x;arnativebond::Trial t;r=t.Attempt(x,bad);assert(arnativebond::Valid(r) && !opens && !requests && !x.guards);}
    armodule::Status s{};s.instance=42;s.now_ms=10000;s.state=unsigned(armodule::State::Ready);
    // Real Nav17 publisher: sighting() returns false outside SCANNING.
    s.link_ready=0;s.output_attached=s.successful_writes=1;
    armodule::StartupRecord p{};p.instance=42;p.now_ms=9000;p.stage=unsigned(armodule::StartupStage::Status);
    amazon_remote::EnrollmentContextRecord c{};c.instance=42;c.expected_handle=4;c.snapshot.begin_ms=9990;c.snapshot.end_ms=9991;
    c.snapshot.user_open=0;c.snapshot.user_connections.list.result=0;c.snapshot.user_connections.list.count=1;c.snapshot.user_connections.list.matches=1;c.snapshot.user_connections.target_handle=4;
    Sign(s,p,c);assert(arnativebond::ConnectedReady(s,p,10001));
    assert(arnativebond::ReadyBinding(s,p,c,10001,4));
    auto historical_scan=s;historical_scan.link_ready=1;Sign(historical_scan,p,c);
    assert(arnativebond::ReadyBinding(historical_scan,p,c,10001,4));
    for(unsigned scenario=0;scenario<21;++scenario){
        auto a=s;auto start=p;auto context=c;std::uint64_t now=10001;std::uint32_t handle=4;
        switch(scenario){
            case 0: a.state=unsigned(armodule::State::Stopped);break;
            case 1: a.result=1;break;case 2:a.state=unsigned(armodule::State::Scanning);a.link_ready=1;break;case 3:a.output_attached=0;break;
            case 4:a.buttons=1;break;case 5:a.acknowledged_buttons=1;break;case 6:a.pad_cleanup_result=1;break;
            case 7:a.successful_writes=0;break;case 8:start.instance=43;break;case 9:start.result=1;break;
            case 10:start.stage=unsigned(armodule::StartupStage::Exit);break;case 11:context.instance=43;break;
            case 12:context.before_binding=1;break;case 13:context.after_binding=1;break;case 14:handle=5;break;
            case 15:context.snapshot.user_connections.list.matches=0;break;case 16:context.snapshot.user_connections.list.invalid=1;break;
            case 17:context.snapshot.user_connections.list.count=16;break;case 18:context.snapshot.user_connections.target_handle=5;break;
            case 19:now=13001;break;case 20:context.snapshot.end_ms=10002;break;
        }
        Sign(a,start,context);assert(!arnativebond::ReadyBinding(a,start,context,now,handle));
    }
    c.snapshot.begin_ms=1;c.snapshot.end_ms=2;Sign(s,p,c);s.now_ms=70000;Sign(s,p,c);assert(!arnativebond::ReadyBinding(s,p,c,70001,4));
    std::puts("PASS native request adapter/policy: exact12-byte command10, command100 getter, durable one-shot intent, revalidation, refusal/failure/no retries;21 READY/context negative scenarios. NOT native SMP/persistence proof.");
}
