// SPDX-License-Identifier: GPL-2.0-only
// Matched local candidate only. Manifest is generated from exact reviewed
// artifacts outside the public repository. Build metadata is not attestation.
#include <switch.h>
#include "operator_input.hpp"
#include "deferred_reader_io.hpp"
#include "deferred_reader_identity.hpp"
#include <deferred_build_manifest.hpp>
#include <limits>

namespace {
namespace f=ardeferredreader;
namespace m=deferred_build_manifest;
#ifdef ARMODULE_DEFERRED_D2_READER
constexpr char Root[]="sdmc:/config/amazon-remote-deferred-d2";
constexpr char Label[]="AMAZON REMOTE / PASSIVE REPORT D2";
constexpr std::uint64_t AddonSize=0x4000;
#else
constexpr char Root[]="sdmc:/config/amazon-remote-deferred-d1";
constexpr char Label[]="AMAZON REMOTE / DEFERRED REPORT D1";
constexpr std::uint64_t AddonSize=0x3000;
#endif
struct Clock {std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}};
struct Backend {
    Service mc{};
    ~Backend(){if(serviceIsActive(&mc))serviceClose(&mc);}
    std::uint32_t CheckIdentity() {
        if(hosversionGet()!=m::Firmware)return f::IdentityMismatch;
        auto result=pmdmntInitialize();if(result)return result;
        result=ldrDmntInitialize();
        if(!result) {
            std::array<LoaderModuleInfo,16> modules{};s32 count=-1;u64 before{},after{};
            result=pmdmntGetProcessId(&before,0x010000000000000b);
            if(!result)result=ldrDmntGetProcessModuleInfo(before,modules.data(),modules.size(),&count);
            if(!result)result=pmdmntGetProcessId(&after,0x010000000000000b);
            if(!result && !f::MatchedDriver(modules,count,before,after,m::Main,m::Addon,m::MainSize,AddonSize))
                result=f::IdentityMismatch;
            count=-1;modules={};before=after=0;
            if(!result)result=pmdmntGetProcessId(&before,0x010000000000bd00);
            if(!result)result=ldrDmntGetProcessModuleInfo(before,modules.data(),modules.size(),&count);
            if(!result)result=pmdmntGetProcessId(&after,0x010000000000bd00);
            if(!result && !f::MatchedCompanion(modules,count,before,after,m::Companion))result=f::IdentityMismatch;
            ldrDmntExit();
        }
        pmdmntExit();return result;
    }
    std::uint32_t Begin(std::uint64_t nonce) {
        auto result=smGetService(&mc,"mc");if(result)return result;
        return serviceDispatchIn(&mc,f::d::BeginCommand,nonce);
    }
    std::uint32_t Read(f::d::Snapshot &snapshot) {
        return serviceDispatch(&mc,f::d::ReadCommand,
            .buffer_attrs={SfBufferAttr_HipcPointer|SfBufferAttr_Out},.buffers={{&snapshot,sizeof snapshot}});
    }
    void Wait(){svcSleepThread(50000000);}
};
bool Prepare(char (&directory)[512],u64 run,u64 context) {
    struct stat entry{};
    if(::lstat(Root,&entry)==0){if(!S_ISDIR(entry.st_mode))return false;}
    else if(errno!=ENOENT || ::mkdir(Root,0700)!=0)return false;
    const auto count=std::snprintf(directory,sizeof directory,"%s/run-%016llx-%016llx",Root,
        static_cast<unsigned long long>(run),static_cast<unsigned long long>(context));
    return count>0 && std::size_t(count)<sizeof directory && ::mkdir(directory,0700)==0;
}
void Draw(const char *color,const char *status,const char *action,const char *detail) {
    consoleClear();std::printf("\n  %s\n\n\n  %s%s\x1b[0m\n\n\n  %s\n\n\n  %s\n",Label,color,status,action,detail);
    consoleUpdate(nullptr);
}
}
int main(int,char **) {
    consoleInit(nullptr);padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for(unsigned i=0;i<arsetupinput::SupportedPlayers;++i)padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;bool consumed=false;
    Draw("\x1b[33;1m","READY TO READ CAPTURED METADATA","Joy-Con A once.  + Exit.","No pairing or radio commands. Wake remains unproven.");
    while(appletMainLoop()) {
        arsetupinput::Frame frame{};
        for(unsigned i=0;i<pads.size();++i){padUpdate(&pads[i]);frame[i]={padGetStyleSet(&pads[i]),
            unsigned(pads[i].active_id_mask)|(unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};}
        const auto sample=input.Update(frame);if(sample.down&HidNpadButton_Plus)break;
        if(!consumed && sample.down==HidNpadButton_A && sample.held==HidNpadButton_A) {
            consumed=true;Draw("\x1b[33;1m","READING / SAVING REPORT","Please wait. Do not re-pair or press A again.","Checking the matched build before requesting stored metadata.");
            Clock clock;Backend backend;f::Record record{};char directory[512]{};u64 context{};const u64 run=armGetSystemTick();
            auto outcome=f::Outcome::PersistenceFailed;
            if(R_SUCCEEDED(svcGetProcessId(&context,CUR_PROCESS_HANDLE)) && run && context && Prepare(directory,run,context)) {
                f::FileStore store(directory);outcome=f::Collect(store,backend,clock,run,context,m::Manifest,record);
            }
            const char *status="REPORT SAVED - NEEDS REVIEW",*color="\x1b[33;1m";
            const char *detail="This does not confirm remote wake support.";
            if(outcome==f::Outcome::PersistenceFailed){status="SAVE INCOMPLETE - DO NOT RETRY";color="\x1b[31;1m";detail="Partial records retained. Open FTPD so they can be checked.";}
            else if(outcome==f::Outcome::ReadErrorSaved){status="READ BLOCKED OR FAILED - REPORT SAVED";detail="Do not retry. The report records the last completed stage.";}
            else if(outcome==f::Outcome::InvalidReplySaved){status="UNEXPECTED REPLY - REPORT SAVED";detail="Unknown reply bytes were not saved. Do not retry.";}
            else if(record.snapshot.state==f::d::State::Complete) {
                f::t::Event event{};
                if(f::t::Decode(record.snapshot.event.data(),80,run,event)) {
                    if(event.record.state==2){status="CAPABILITY RECORD SAVED";color="\x1b[32;1m";}
                    else {status="NO COMPLETE CAPABILITY RECORD";detail="A missing record does not mean wake is unsupported.";}
                }
            }
            Draw(color,status,"+ Exit, then open FTPD. Do not retry or re-pair.",detail);
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
