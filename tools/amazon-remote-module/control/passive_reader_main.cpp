// SPDX-License-Identifier: GPL-2.0-only
// Offline candidate. Reads only the matched companion's metadata cache.
#include <switch.h>
#include "operator_input.hpp"
#include "passive_read_io.hpp"
#include <cstdio>

namespace {
namespace f=arpassiveread;
// Expected companion artifact, recorded for provenance only. This is NOT
// runtime code attestation or permission to use a native driver transport.
constexpr std::array<std::uint8_t,32> Manifest{
    0x50,0x98,0x65,0x3f,0x0f,0x8a,0x47,0x4f,0x55,0xfc,0xe2,0xb2,0x34,0xad,0xfc,0x45,
    0x19,0x62,0x92,0xba,0x59,0xf3,0x1d,0xca,0x8e,0x9a,0xaf,0x5d,0x11,0x23,0x23,0x7d};
constexpr char Root[]="sdmc:/config/amazon-remote-passive-p1";
struct Clock {std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}};
struct Backend {
    std::uint32_t Read(f::p::Snapshot &snapshot) {
        Service mc{};auto result=smGetService(&mc,"mc");
        if(R_FAILED(result))return result;
        result=serviceDispatch(&mc,f::p::ReadCommand,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},
            .buffers={{&snapshot,sizeof snapshot}});
        serviceClose(&mc);return result;
    }
};
bool Prepare(char (&directory)[512],std::uint64_t run,std::uint64_t context) {
    struct stat entry{};
    if(::lstat(Root,&entry)==0){if(!S_ISDIR(entry.st_mode))return false;}
    else if(errno!=ENOENT || ::mkdir(Root,0700)!=0)return false;
    const auto count=std::snprintf(directory,sizeof directory,"%s/run-%016llx-%016llx",Root,
        static_cast<unsigned long long>(run),static_cast<unsigned long long>(context));
    // Exclusive new directory. No cleanup or overwrite if it already exists.
    return count>0 && std::size_t(count)<sizeof directory && ::mkdir(directory,0700)==0;
}
}
int main(int,char **) {
    consoleInit(nullptr);
    padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for(unsigned i=0;i<arsetupinput::SupportedPlayers;++i)padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;
    bool consumed=false,redraw=true;
    const char *status="READY TO READ CACHED METADATA",*color="\x1b[33;1m";
    const char *action="Joy-Con A once: save report.  + Exit.";
    const char *detail="This cannot connect, pair, query or reset the radio.";
    while(appletMainLoop()) {
        arsetupinput::Frame frame{};
        for(unsigned i=0;i<pads.size();++i){padUpdate(&pads[i]);frame[i]={padGetStyleSet(&pads[i]),
            unsigned(pads[i].active_id_mask)|(unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};}
        const auto sample=input.Update(frame);
        if(sample.down&HidNpadButton_Plus)break;
        if(!consumed && sample.down==HidNpadButton_A && sample.held==HidNpadButton_A) {
            consumed=true;redraw=true;action="+ Exit and open FTPD. Do not retry or re-pair.";
            Clock clock;Backend backend;f::Record record{};char directory[512]{};
            u64 context{};const auto run=armGetSystemTick();
            auto outcome=f::Outcome::PersistenceFailed;
            if(R_SUCCEEDED(svcGetProcessId(&context,CUR_PROCESS_HANDLE)) && run && context && Prepare(directory,run,context)) {
                f::FileStore store(directory);outcome=f::Collect(store,backend,clock,run,context,Manifest,record);
            }
            color="\x1b[33;1m";
            if(outcome==f::Outcome::PersistenceFailed){status="SAVE INCOMPLETE - DO NOT RETRY";color="\x1b[31;1m";detail="Any partial checkpoints were preserved.";}
            else if(outcome==f::Outcome::ReadErrorSaved){status="READ ERROR SAVED";detail="The companion cache was not available. No radio query was sent.";}
            else if(outcome==f::Outcome::InvalidReplySaved){status="INVALID REPLY RECORDED";detail="Unknown reply bytes were not persisted.";}
            else {
                const auto &s=record.snapshot;
                if(!s.attempts){status="NO CAPABILITY DATA OBSERVED";detail="Report saved. Missing data does not mean wake is unsupported.";}
                else if(s.conflicting || s.clock_error || s.rejected || s.limit_reached || s.first.outcome!=f::j::cap::Outcome::Reported){status="REPORT SAVED - NEEDS REVIEW";detail="Incomplete, conflicting or unsuccessful observations were retained.";}
                else {status="CAPABILITY METADATA SAVED";color="\x1b[32;1m";detail="Historical metadata only. Deep-sleep wake remains unproven.";}
            }
        }
        if(redraw){consoleClear();std::printf("\n  AMAZON REMOTE / PASSIVE REPORT P1\n\n\n  %s%s\x1b[0m\n\n\n  %s\n\n\n  %s\n",color,status,action,detail);redraw=false;}
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
