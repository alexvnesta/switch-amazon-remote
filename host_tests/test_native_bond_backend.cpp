// SPDX-License-Identifier: GPL-2.0-only
#define ARMODULE_NAV17
#define ARMODULE_NAV16
#define ARMODULE_NAV15
#include "../tools/amazon-remote-pairing-audit/native_bond_backend.hpp"
#include <cassert>
#include <cstdio>
namespace {
using namespace arnativebond;
BtdrvAddress target{{2,0xab,0xcd,0x12,0x34,0x56}};
unsigned system_opens{},system_closes{},system_getters{},user_getters{},driver_opens{},driver_getters{},requests{},change_second_getter{};
Result system_open_result{},system_getter_result{};u8 count=1;
std::array<BtdrvBleConnectionInfo,16> connections{};
void Reset(){
    system_opens=system_closes=system_getters=user_getters=driver_opens=driver_getters=requests=change_second_getter=0;
    system_open_result=system_getter_result=0;count=1;connections={};connections[0].addr=target;connections[0].connection_handle=4;
}
struct Platform {
    std::uint64_t time=10000,pid=7;Result pm_init{},pm_query{},status_result{};
    bool binary=true,storage=true,start_read=true,context_read=true,aruid_zero=false,supported=true;
    unsigned intents{},reads{},change_second{};bool persist=true;
    armodule::Status s{};armodule::StartupRecord p{};amazon_remote::EnrollmentContextRecord c{};
    Platform(){
        s.instance=42;s.now_ms=10000;s.state=unsigned(armodule::State::Ready);s.output_attached=s.successful_writes=1;
        p.instance=42;p.now_ms=9000;p.stage=unsigned(armodule::StartupStage::Status);
        c.instance=42;c.expected_handle=4;c.snapshot.begin_ms=9990;c.snapshot.end_ms=9991;
        c.snapshot.user_open=0;c.snapshot.user_connections.list.result=0;c.snapshot.user_connections.list.count=1;
        c.snapshot.user_connections.list.matches=1;c.snapshot.user_connections.target_handle=4;Sign();
        c.snapshot.btm_open=0;c.snapshot.system_connections=c.snapshot.user_connections;Sign();
    }
    void Sign(){s.reserved=armodule::StatusChecksum(s);p.checksum=armodule::StartupChecksum(p);c.snapshot.checksum=araudit::Checksum(c.snapshot);c.checksum=amazon_remote::EnrollmentContextChecksum(c);}
    std::uint64_t Now(){return ++time;}std::uint32_t Firmware(){return 0x160500;}
    bool Supported(){return supported;}bool AruidZero(){return aruid_zero;}
    bool VerifyBinary(){return binary;}
    void QueryProcess(std::uint32_t &init,std::uint32_t &query,std::uint64_t &out){init=pm_init;if(!init){query=pm_query;out=pid;}}
    bool StorageReady(){return storage;}
    bool ReadStartup(armodule::StartupRecord &out){out=p;return start_read;}
    bool ReadContext(amazon_remote::EnrollmentContextRecord &out){out=c;return context_read;}
    Result ReadStatus(armodule::Status &out,std::uint64_t now){
        ++reads;if(reads==2 && change_second){s.state=unsigned(armodule::State::Stopped);Sign();}
        if(status_result)return status_result;
        if(!armodule::FreshStatus(s,now)){return 0x402;}out=s;return 0;
    }
    bool PersistIntent(const Record &r){++intents;assert(Valid(r) && !requests);return persist;}
};
}
Result btmInitialize(){++system_opens;return system_open_result;}
void btmExit(){++system_closes;}
// Reproduce Guard1's app-scoped empty list. New backend must never call it.
Result btmuBleGetConnectionState(BtdrvBleConnectionInfo *,u8,u8 *total){++user_getters;*total=0;return 0;}
Result btmBleGetConnectionState(BtdrvBleConnectionInfo *out,u8 capacity,u8 *total){
    ++system_getters;assert(capacity==16);if(system_getter_result)return system_getter_result;
    if(system_getters==2 && change_second_getter==4)return 58;
    *total=(system_getters==2 && change_second_getter==1)?0:count;
    for(unsigned i=0;i<*total && i<capacity;++i)out[i]=connections[i];
    if(system_getters==2 && *total){
        if(change_second_getter==2)out[0].connection_handle=5;
        if(change_second_getter==3)out[0].addr.address[0]^=1;
    }
    return 0;
}
Result smGetService(Service *s,const char *name){assert(!std::strcmp(name,"btdrv"));++driver_opens;s->active=true;return 0;}
bool serviceIsActive(Service *s){return s->active;}
void serviceClose(Service *s){s->active=false;}
Result FakeDriverStateDispatch(Service *s,unsigned cmd,void *out,std::size_t n){assert(s->active && cmd==100 && n==1);++driver_getters;*static_cast<u8*>(out)=1;return 0;}
Result FakeNativeBondDispatch(Service *s,unsigned cmd,const void *in,std::size_t n){
    assert(s->active && cmd==10 && n==12);assert(!std::memcmp(in,target.address,6));++requests;return 0;
}
int main(int argc,char **argv){
    using namespace arnativebond;
    Reset();Platform success;GuardDiagnostic passing{};
    {GuardBackend b(success);Trial t;const auto r=t.Attempt(b,target);
        assert(Valid(r) && r.request_issued && requests==1 && success.intents==1);
        assert(b.diagnostic_count==2 && system_opens==1 && system_getters==2 && system_closes==1 && !user_getters);
        for(unsigned i=0;i<2;++i){const auto &d=b.diagnostics[i];assert(ValidDiagnostic(d) && d.call==i+1 && d.stage==unsigned(GuardStage::Passed));
            assert(d.flags==127 && d.aruid_zero==0 && d.count==1 && d.matches==1 && !d.invalid && d.live_handle==4 && !d.status.link_ready);}
        passing=b.diagnostics[0];
        const auto again=t.Attempt(b,target);assert(!again.request_issued && requests==1 && b.diagnostic_count==2);
    }
    for(unsigned i=0;i<sizeof passing;++i){auto bad=passing;reinterpret_cast<unsigned char*>(&bad)[i]^=1;assert(!ValidDiagnostic(bad));}
    for(unsigned scenario=0;scenario<39;++scenario){
        Reset();Platform p;auto expected=GuardStage::ConnectedReady;bool bad_checksum=false;
        switch(scenario){
            case 0:p.binary=false;expected=GuardStage::Binary;break;
            case 1:p.pm_init=55;expected=GuardStage::ProcessInit;break;
            case 2:p.pm_query=56;expected=GuardStage::ProcessQuery;break;
            case 3:p.pid=0;expected=GuardStage::ProcessQuery;break;
            case 4:p.storage=false;expected=GuardStage::Storage;break;
            case 5:p.start_read=false;expected=GuardStage::StartupRead;break;
            case 6:bad_checksum=true;expected=GuardStage::StartupInvalid;break;
            case 7:p.p.stage=unsigned(armodule::StartupStage::Exit);expected=GuardStage::StartupInvalid;break;
            case 8:p.context_read=false;expected=GuardStage::ContextRead;break;
            case 9:p.status_result=0xe02;expected=GuardStage::StatusRead;break;
            case 10:p.s.state=unsigned(armodule::State::Stopped);break;
            case 11:p.s.output_attached=0;break;
            case 12:p.s.buttons=1;break;
            case 13:p.s.acknowledged_buttons=1;break;
            case 14:p.s.pad_cleanup_result=1;break;
            case 15:p.s.successful_writes=0;break;
            case 16:p.p.instance=43;break;
            case 17:system_open_result=57;expected=GuardStage::ConnectionOpen;break;
            case 18:system_getter_result=58;expected=GuardStage::ConnectionGetter;break;
            case 19:count=16;expected=GuardStage::ListBounds;break;
            case 20:connections[0].connection_handle=0;expected=GuardStage::ListInvalid;break;
            case 21:count=2;connections[1]=connections[0];expected=GuardStage::ListInvalid;break;
            case 22:count=0;expected=GuardStage::TargetMissing;break;
            case 23:connections[0].addr.address[0]^=1;expected=GuardStage::TargetMissing;break;
            case 24:p.c.instance=43;expected=GuardStage::ContextBinding;break;
            case 25:p.c.expected_handle=5;expected=GuardStage::ContextBinding;break;
            case 26:p.c.before_binding=1;expected=GuardStage::ContextBinding;break;
            case 27:p.c.after_binding=1;expected=GuardStage::ContextBinding;break;
            case 28:p.c.snapshot.end_ms=20000;expected=GuardStage::ContextBinding;break;
            case 29:p.time=70000;p.s.now_ms=70000;p.c.snapshot.begin_ms=1;p.c.snapshot.end_ms=2;expected=GuardStage::ContextBinding;break;
            case 30:p.c.snapshot.btm_open=59;expected=GuardStage::ContextBinding;break;
            case 31:p.c.snapshot.system_connections.list.result=60;expected=GuardStage::ContextBinding;break;
            case 32:p.c.snapshot.system_connections.list.count=0;expected=GuardStage::ContextBinding;break;
            case 33:p.c.snapshot.system_connections.list.count=16;expected=GuardStage::ContextBinding;break;
            case 34:p.c.snapshot.system_connections.list.matches=0;expected=GuardStage::ContextBinding;break;
            case 35:p.c.snapshot.system_connections.list.matches=2;expected=GuardStage::ContextBinding;break;
            case 36:p.c.snapshot.system_connections.list.invalid=1;expected=GuardStage::ContextBinding;break;
            case 37:p.c.snapshot.system_connections.target_handle=5;expected=GuardStage::ContextBinding;break;
            case 38:p.c.snapshot.user_connections.list.count=0;expected=GuardStage::ContextBinding;break;
        }
        p.Sign();if(bad_checksum)++p.p.checksum;
        {GuardBackend b(p);Trial t;const auto r=t.Attempt(b,target);
            assert(Valid(r) && !r.request_issued && !requests && !driver_opens && !driver_getters && !p.intents);
            assert(b.diagnostic_count==1 && ValidDiagnostic(b.diagnostics[0]));
            const auto &d=b.diagnostics[0];assert(d.stage==unsigned(expected) && d.result==r.readiness_result);
            assert(!std::strcmp(b.reason,GuardReason(expected)));
            if(scenario==22)assert(d.count==0 && d.matches==0 && d.context.snapshot.user_connections.list.count==1 && d.aruid_zero==0);
            if(scenario==18)assert(d.connection_getter==58 && d.count==Unknown && !(d.flags&ConnectionsRead));
        }
        assert(system_closes==(system_opens && !system_open_result?1u:0u) && !user_getters);
    }
    Reset();Platform changed;changed.change_second=1;
    {GuardBackend b(changed);Trial t;const auto r=t.Attempt(b,target);assert(Valid(r) && !r.request_issued && !requests && changed.intents==1 && b.diagnostic_count==2);
        assert(b.diagnostics[0].stage==unsigned(GuardStage::Passed) && b.diagnostics[1].stage==unsigned(GuardStage::ConnectedReady));}
    for(unsigned change=1;change<=4;++change){
        Reset();change_second_getter=change;Platform p;
        {GuardBackend b(p);Trial t;const auto r=t.Attempt(b,target);
            assert(Valid(r) && !r.request_issued && !requests && p.intents==1 && b.diagnostic_count==2);
            assert(b.diagnostics[0].stage==unsigned(GuardStage::Passed));
            const auto expected=change==2?GuardStage::ContextBinding:change==4?GuardStage::ConnectionGetter:GuardStage::TargetMissing;
            assert(b.diagnostics[1].stage==unsigned(expected));}
        assert(system_closes==1 && !user_getters);
    }
    Reset();Platform zero;zero.aruid_zero=true;
    {GuardBackend b(zero);Trial t;const auto r=t.Attempt(b,target);assert(Valid(r) && r.request_issued && requests==1 && !user_getters);
        assert(b.diagnostics[0].aruid_zero==1);}
    // Optional PRIVATE400-byte Guard1 input: reuse the actual nested records,
    // not a fabricated READY model. Only the live system getter is modeled.
    if(argc==2){
        std::array<unsigned char,400> raw{};auto *f=std::fopen(argv[1],"rb");assert(f);
        assert(std::fread(raw.data(),1,raw.size(),f)==raw.size() && std::fgetc(f)==EOF && !std::fclose(f));
        std::uint32_t header[8]{};std::memcpy(header,raw.data(),sizeof header);
        std::uint32_t hash=2166136261;for(unsigned i=0;i<392;++i)hash=(hash^raw[i])*16777619;
        std::uint32_t stored{};std::memcpy(&stored,raw.data()+392,4);
        assert(header[0]==0x41524744 && header[1]==1 && header[3]==14 && header[5]==127 && hash==stored);
        Reset();Platform actual;std::memcpy(&actual.time,raw.data()+32,8);
        std::memcpy(&actual.p,raw.data()+96,sizeof actual.p);std::memcpy(&actual.s,raw.data()+136,sizeof actual.s);
        std::memcpy(&actual.c,raw.data()+224,sizeof actual.c);
        connections[0].connection_handle=actual.c.expected_handle;
        GuardBackend b(actual);Trial t;const auto r=t.Attempt(b,target);
        assert(Valid(r) && r.request_issued && requests==1 && actual.intents==1 && !user_getters);
        assert(b.diagnostic_count==2 && b.diagnostics[0].stage==unsigned(GuardStage::Passed) && b.diagnostics[1].stage==unsigned(GuardStage::Passed));
        std::puts("PASS captured Guard1 READY/context replay with MODEL system target; not live system getter or pairing proof.");
    }else assert(argc==1);
    std::puts("PASS actual system guard backend:39 failure scenarios,4 second-getter changes, user-empty/system-target regression, ARUID metadata only, one-shot/cleanup/400-byte checks. NOT device pairing proof.");
}
