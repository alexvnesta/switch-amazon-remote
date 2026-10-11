// SPDX-License-Identifier: GPL-2.0-only
#define ARMODULE_NAV15
#define ARMODULE_NAV17
#define ARMODULE_RECONNECT_CHECK
#define ARMODULE_NATIVE_HCI_C2
#define ARMODULE_AUTOMATIC_A1
#include "../tools/amazon-remote-module/control/controller_query_isolation.hpp"
#include <cassert>
#include <cstdio>
#include <string>
using namespace arqueryq2;
struct Backend {
    unsigned reads{},pids{},leases{},ends{};
    bool isolation=true,second_isolation=true,changed_intent{},changed_boot{},present_after{};
    std::uint32_t pm_result=ProcessNotFound,lease_result{},end_result{};
    std::uint64_t pid{};
    Intent intent{};armodule::StartupRecord startup{};
    amazon_remote::CaptureDiagnosticsWire before{},after{};
    Backend() {
        intent.previous_instance=100;intent.staging_tag=42;intent.checksum=Checksum(intent);
        startup.instance=200;startup.now_ms=100;startup.stage=unsigned(armodule::StartupStage::Enable);
        startup.result=DisabledResult;startup.checksum=armodule::StartupChecksum(startup);
        before.magic=amazon_remote::CaptureDiagnosticMagic;before.version=1;before.now_boot_ms=1001;before.flags=1;
        after=before;after.state=3;after.first_close_reason=1;after.first_close_ms=1001;after.flags=5;
    }
    std::uint64_t Now(){return 1002;}
    bool ReadIsolation(Intent &i,armodule::StartupRecord &b) {
        ++reads;i=intent;b=startup;
        if(reads==2 && changed_intent){++i.staging_tag;i.checksum=Checksum(i);}
        if(reads==2 && changed_boot){++b.instance;b.checksum=armodule::StartupChecksum(b);}
        return isolation && (reads!=2 || second_isolation);
    }
    std::uint32_t ProcessId(std::uint64_t &out){++pids;out=pid;return pids==2 && present_after?0:pm_result;}
    std::uint32_t ReadLease(amazon_remote::CaptureDiagnosticsWire &out){out=++leases==1?before:after;return lease_result;}
    std::uint32_t EndLease(){assert(reads==1 && pids==1 && leases==1);++ends;return end_result;}
};
Record New(){Record r;r.started_ms=1000;r.nonce=7;return r;}
void RefuseBeforeEnd(Backend sdk) {
    auto r=New();assert(!Prepare(sdk,r));assert(!sdk.ends);
}
int main() {
    Backend good;auto r=New();assert(Prepare(good,r));
    assert(r.stage==Stage::Prepared && good.ends==1 && good.pids==2 && good.reads==2 && good.leases==2);
    for(unsigned field=0;field<6;++field) {
        Backend b;
        if(field==0)b.intent.previous_instance=b.startup.instance;
        if(field==1)b.startup.stage=13;
        if(field==2)b.startup.result=0;
        if(field==3)b.startup.now_ms=1003;
        if(field==4)b.intent.staging_tag=0;
        if(field==5)b.isolation=false;
        b.intent.checksum=Checksum(b.intent);b.startup.checksum=armodule::StartupChecksum(b.startup);
        RefuseBeforeEnd(b);
    }
    for(unsigned field=0;field<3;++field) {
        Backend b;
        if(field==0)b.pm_result=0;
        if(field==1)b.pm_result=8;
        if(field==2)b.pid=99;
        RefuseBeforeEnd(b);
    }
    for(unsigned state=1;state<=5;++state){Backend b;b.before.state=state;RefuseBeforeEnd(b);}
    for(unsigned field=0;field<5;++field) {
        Backend b;
        if(field==0)b.before.flags=0;
        if(field==1)b.before.deadline_boot_ms=9;
        if(field==2)b.before.first_close_ms=999;
        if(field==3)b.before.now_boot_ms=1003;
        if(field==4)b.lease_result=9;
        RefuseBeforeEnd(b);
    }
    for(unsigned field=0;field<8;++field) {
        Backend b;auto bad=New();
        if(field==0)b.end_result=7;
        if(field==1)b.after.state=0;
        if(field==2)b.after.first_close_ms=999;
        if(field==3)b.after.first_close_reason=2;
        if(field==4)b.second_isolation=false;
        if(field==5)b.changed_intent=true;
        if(field==6)b.changed_boot=true;
        if(field==7)b.present_after=true;
        assert(!Prepare(b,bad) && b.ends==1);
    }
    cq::Query q;assert(q.Begin(r.nonce,1002));assert(q.Dispatch(1002));q.Submission(9,1002);
    r.wire=q.Read(1002);r.finished_ms=1003;r.result=Result::Completed;r.checksum=Checksum(r);
    assert(Valid(r));
    for(unsigned i=0;i<sizeof(r);++i){auto bad=r;reinterpret_cast<unsigned char*>(&bad)[i]^=1;assert(!Valid(bad));}
    auto stale=r;stale.intent.previous_instance=stale.startup.instance;
    stale.intent.checksum=Checksum(stale.intent);stale.checksum=Checksum(stale);assert(!Valid(stale));
    auto failed=r;failed.result=Result::PreconditionFailed;failed.stage=Stage::Initial;
    failed.checksum=Checksum(failed);assert(Valid(failed));
    std::puts("PASS Q2 isolated diagnostic: no query before fresh disabled boot, absent process and unused lease; exact close readback, races and report integrity");
}
