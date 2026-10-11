// SPDX-License-Identifier: GPL-2.0-only
#include "../tools/amazon-remote-module/arm_startup_diagnostic.hpp"
#include "../tools/amazon-remote-module/control/setup_view.hpp"
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdio>
using namespace armodule;
using namespace amazon_remote;
struct Fake {
    unsigned reads{},arms{},initializes{},scans{},fail{},log_fail{};
    std::uint32_t before{},after{1};bool live{true};std::vector<unsigned> stages;
    std::uint32_t Invalid(){return 0x1759;}
    std::uint32_t Read(CaptureLeaseWire &wire) {
        ++reads;wire={CaptureLeaseMagic,1,reads==1?before:after,0,1000,reads==1?0u:61000u};
        return fail==(reads==1?1u:4u)?0x999:0;
    }
    std::uint32_t Arm(){++arms;return fail==3?0x999:0;}
    bool Live(const CaptureLeaseWire &){return live;}
    std::uint32_t Initialize(){++initializes;return fail==6?0x999:0;}
    std::uint32_t Scan(){++scans;return fail==7?0x999:0;}
    bool Checkpoint(ArmStage stage,std::uint32_t,const CaptureLeaseWire &,bool){
        stages.push_back(unsigned(stage));return log_fail!=unsigned(stage);
    }
};
ArmDiagnostic Record(){
    ArmDiagnostic r{};r.stage=unsigned(ArmStage::RequireAvailable);r.result=0x1759;
    r.instance=42;r.now_ms=1001;r.lease_valid=1;r.lease_state=3;r.lease_now_ms=1000;
    r.checksum=ArmChecksum(r);return r;
}
int main(){
    for(unsigned failure=0;failure<=7;++failure){
        Fake b;b.fail=failure;if(failure==2)b.before=3;if(failure==5)b.live=false;
        const auto rc=RunArmStartup(b);assert(bool(rc)==bool(failure));
        const unsigned last=failure?failure:7;assert(b.stages.size()==last);
        for(unsigned i=0;i<last;++i)assert(b.stages[i]==i+1);
        assert(b.reads==1u+unsigned(last>=4) && b.arms==unsigned(last>=3) &&
            b.initializes==unsigned(last>=6) && b.scans==unsigned(last>=7));
    }
    for(unsigned state:{1u,2u,3u,4u}){
        Fake b;b.before=state;assert(RunArmStartup(b)==0x1759);
        assert(b.reads==1 && !b.arms && !b.initializes && !b.scans);
    }
    for(unsigned stage=1;stage<=7;++stage){
        Fake b;b.log_fail=stage;assert(RunArmStartup(b)==0x1759 && b.stages.size()==stage);
        assert(b.scans==unsigned(stage==7));
    }
    auto r=Record();assert(ValidArmDiagnostic(r));Status s{};
    s.instance=42;s.now_ms=2000;s.state=unsigned(State::Stopped);s.result=0x1759;
    assert(CaptureClosed(r,s));
    for(unsigned offset=0;offset<sizeof r;++offset){
        auto bad=r;reinterpret_cast<unsigned char*>(&bad)[offset]^=1;assert(!ValidArmDiagnostic(bad));
    }
    for(unsigned test=0;test<20;++test){
        auto bad=r;auto status=s;
        switch(test){
            case 0:bad.instance=43;break;case 1:bad.now_ms=2001;break;case 2:bad.lease_valid=0;break;
            case 3:bad.lease_state=0;break;case 4:bad.lease_state=1;bad.lease_deadline_ms=61000;break;
            case 5:bad.lease_state=4;break;case 6:bad.stage=unsigned(ArmStage::ReadBefore);break;
            case 7:bad.stage=unsigned(ArmStage::InitializeIdentity);break;case 8:bad.result=1;break;
            case 9:status.output_attached=1;break;case 10:status.buttons=1;break;
            case 11:status.acknowledged_buttons=1;break;case 12:status.pad_cleanup_result=1;break;
            case 13:status.state=unsigned(State::Ready);break;case 14:status.link_ready=1;break;
            case 15:bad.lease_state=2;bad.lease_deadline_ms=61000;break;
            case 16:bad.lease_now_ms=1002;break;case 17:bad.reserved=1;break;
            case 18:status.successful_writes=1;break;case 19:status.capture_deadline=61000;break;
        }
        bad.checksum=ArmChecksum(bad);assert(!CaptureClosed(bad,status));
    }
    r.lease_state=2;r.lease_deadline_ms=r.lease_now_ms=r.now_ms=61000;r.checksum=ArmChecksum(r);
    s.now_ms=61001;assert(CaptureClosed(r,s));r.stage=unsigned(ArmStage::RequireLive);r.checksum=ArmChecksum(r);assert(CaptureClosed(r,s));
    arsetupview::Input input{};input.fresh=true;input.state=4;input.result=0x1759;input.capture_closed=true;
    const auto closed=arsetupview::SelectStartupTrace(input);
    assert(!std::strcmp(closed.status,"CAPTURE WINDOW CLOSED") && std::strstr(closed.action,"reboot") && closed.tone==arsetupview::Tone::Error);
    assert(std::strlen(closed.action)<70 && std::strlen(closed.note)<70);
    for(unsigned test=0;test<6;++test){
        auto bad=input;
        switch(test){case 0:bad.fresh=false;break;case 1:bad.state=3;break;case 2:bad.result=0;break;
            case 3:bad.capture_closed=false;break;case 4:bad.cleanup=1;break;case 5:bad.pad_attached=true;break;}
        assert(std::strcmp(arsetupview::SelectStartupTrace(bad).status,"CAPTURE WINDOW CLOSED"));
    }
    std::puts("PASS S3 original7-stage order,8 outcomes,4 non-available refusals,7 log failures;64 corruption and20 resealed identity/time/stage/state failures; ended/expired UI only, stale/generic failures stay generic. NO device proof.");
}
