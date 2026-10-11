// SPDX-License-Identifier: GPL-2.0-only
// Foreground native-queue test. Does not start the input module or pair.
#include "driver_identity_sdk.hpp"
#include "operator_input.hpp"
#include "../io.hpp"
#include "../../../mc_mitm/source/amazon_remote/native_metadata.hpp"
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

namespace {
using Wire=amazon_remote::native_metadata::Wire;
constexpr char Path[]="sdmc:/switch/amazon-remote-navigation17/native-metadata-m1.bin";
constexpr std::uint32_t Unknown=0xffffffff;
struct Report {
    std::uint32_t magic{0x3150524d},version{1};
    std::uint64_t begin_ms{},end_ms{};
    ardriverid::Record identity{};
    std::uint32_t open_result{Unknown},probe_result{Unknown},read_result{Unknown},outcome{};
    Wire evidence{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(Report)==824 && offsetof(Report,checksum)==816);
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
bool Absent(){struct stat s{};return ::stat(Path,&s)!=0 && errno==ENOENT;}
std::uint32_t Checksum(const Report &r){
    auto*p=reinterpret_cast<const unsigned char*>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(Report,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
bool Save(Report&r){
    r.end_ms=Now();r.checksum=Checksum(r);
    FILE*f=std::fopen(Path,"wbx");if(!f)return false;
    const bool wrote=std::fwrite(&r,1,sizeof r,f)==sizeof r;
    const bool flushed=std::fflush(f)==0;const bool synced=::fsync(::fileno(f))==0;
    const bool closed=std::fclose(f)==0;Report back{};
    return wrote&&flushed&&synced&&closed&&armodule::ReadExact(Path,back)&&
        !std::memcmp(&back,&r,sizeof r)&&back.checksum==Checksum(back);
}
Result Read(Service&s,Wire&w){
    Wire candidate{};
    const auto rc=serviceDispatch(&s,65008,.buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},
        .buffers={{&candidate,sizeof candidate}});
    if(R_FAILED(rc))return rc;
    if(!amazon_remote::native_metadata::Valid(candidate))return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    w=candidate;return 0;
}
}
int main(int,char**){
    consoleInit(nullptr);
    padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for(unsigned i=0;i<arsetupinput::SupportedPlayers;++i)padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;Service mc{};Report report{};
    bool consumed=!Absent(),running=false,saved=false,redraw=true;
    std::uint32_t nonce{};std::uint64_t deadline{},last_second{};
    const char*title=consumed?"REPORT ALREADY EXISTS":"READY FOR DRIVER CHECK";
    const char*action=consumed?"+ Exit, then open FTPD.":"Joy-Con A once. Leave the Amazon remote alone.";
    auto finish=[&](unsigned outcome){
        running=false;report.outcome=outcome;
        if(serviceIsActive(&mc))serviceClose(&mc);
        saved=Save(report);redraw=true;
        title=!saved?"REPORT SAVE FAILED":outcome==1?"DRIVER BRIDGE OK":"CHECK NEEDS REVIEW";
        action="+ Exit, then open FTPD. Do not repeat.";
    };
    while(appletMainLoop()){
        arsetupinput::Frame frame{};
        for(unsigned i=0;i<pads.size();++i){padUpdate(&pads[i]);frame[i]={padGetStyleSet(&pads[i]),
            unsigned(pads[i].active_id_mask)|(unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};}
        const auto sample=input.Update(frame);
        if(sample.down&HidNpadButton_Plus){if(running)finish(6);break;}
        if(!consumed && sample.down==HidNpadButton_A && sample.held==HidNpadButton_A){
            consumed=true;report.begin_ms=Now();ardriverid::Backend backend;report.identity=ardriverid::Collect(backend);
            if(!ardriverid::Valid(report.identity) || report.identity.verdict!=unsigned(ardriverid::Verdict::KnownBuildMetadata))finish(3);
            else{
                report.open_result=smGetService(&mc,"mc");
                if(report.open_result)finish(4);
                else{
                    report.read_result=Read(mc,report.evidence);
                    if(report.read_result || report.evidence.request_nonce)finish(4);
                    else{
                        nonce=std::uint32_t(armGetSystemTick())^0x6d31724a;if(!nonce)nonce=1;
                        report.probe_result=serviceDispatchIn(&mc,65007,nonce);
                        if(report.probe_result)finish(4);
                        else{running=true;deadline=Now()+8000;title="CHECKING DRIVER EVENT PATH";
                            action="Wait up to 8 seconds. No remote button presses.";redraw=true;}
                    }
                }
            }
        }
        if(running){
            report.read_result=Read(mc,report.evidence);
            if(report.read_result)finish(4);
            else if(report.evidence.probe_nonce==nonce && report.evidence.request_nonce==nonce &&
                report.evidence.start_ms>=report.begin_ms && report.evidence.probe_ms<=Now())finish(1);
            else if(Now()>=deadline)finish(2);
            else if(Now()/1000!=last_second){last_second=Now()/1000;redraw=true;}
        }
        if(redraw){
            consoleClear();
            std::printf("\n  AMAZON REMOTE SETUP / SECURITY BRIDGE M1\n\n\n  %s%s\x1b[0m\n\n\n  NEXT: %s\n\n\n  %s\n\n  This checks the native driver-to-observer event path.\n  It does NOT pair, connect, save keys or start navigation.\n\n  + Exit\n",
                saved&&report.outcome==1?"\x1b[32;1m":"\x1b[33;1m",title,action,
                saved?"REPORT SAVED AND READBACK VERIFIED":"Reboot once after M1 installation, before this check.");
            redraw=false;
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    if(running)finish(6);
    if(serviceIsActive(&mc))serviceClose(&mc);
    consoleExit(nullptr);return 0;
}
