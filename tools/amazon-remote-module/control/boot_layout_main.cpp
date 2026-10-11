// SPDX-License-Identifier: GPL-2.0-only
// Getter-only companion to the unhooked L1 loader qualification package.
#include <switch.h>
#include "boot_layout_reader.hpp"
#include "operator_input.hpp"
#include "../io.hpp"
#include <boot_layout_manifest.hpp>
#include <fcntl.h>
namespace {
namespace f=arbootlayout;
struct Backend {
    std::uint32_t Firmware(){return hosversionGet();}
    Result PmOpen(){return pmdmntInitialize();} void PmClose(){pmdmntExit();}
    Result LoaderOpen(){return ldrDmntInitialize();} void LoaderClose(){ldrDmntExit();}
    Result Pid(std::uint64_t &pid){u64 p{};const auto r=pmdmntGetProcessId(&p,0x010000000000000b);if(!r)pid=p;return r;}
    Result ReadModules(std::uint64_t pid,f::Modules &out,std::int32_t &count){
        std::array<LoaderModuleInfo,16> raw{};s32 n=-1;
        const auto result=ldrDmntGetProcessModuleInfo(pid,raw.data(),raw.size(),&n);
        count=n;
        if(!result && n>=0 && n<=16)for(int i=0;i<n;++i){out[i].base=raw[i].base_address;out[i].size=raw[i].size;
            std::memcpy(out[i].id.data(),raw[i].build_id,32);}
        return result;
    }
};
constexpr char Root[]="sdmc:/config/amazon-remote-boot-l1";
bool Prepare(char (&directory)[512],u64 run,u64 context){
    struct stat st{};
    if(::lstat(Root,&st)==0){if(!S_ISDIR(st.st_mode))return false;}
    else if(errno!=ENOENT || ::mkdir(Root,0700))return false;
    const auto n=std::snprintf(directory,sizeof directory,"%s/run-%016llx-%016llx",Root,
        static_cast<unsigned long long>(run),static_cast<unsigned long long>(context));
    return n>0 && std::size_t(n)<sizeof directory && ::mkdir(directory,0700)==0;
}
bool Save(const char *directory,const char *name,const f::Record &r){
    if(!f::Valid(r))return false;
    char path[600];const auto n=std::snprintf(path,sizeof path,"%s/%s",directory,name);
    if(n<=0 || std::size_t(n)>=sizeof path)return false;
    const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool wrote=::write(fd,&r,sizeof r)==ssize_t(sizeof r);
    const bool synced=::fsync(fd)==0,closed=::close(fd)==0;f::Record back{};
    return wrote && synced && closed && armodule::ReadExact(path,back) && f::Valid(back) && !std::memcmp(&r,&back,sizeof r);
}
void Draw(const char *color,const char *status,const char *action){
    consoleClear();std::printf("\n  AMAZON REMOTE / BOOT CHECK L1\n\n\n  %s%s\x1b[0m\n\n\n  %s\n\n\n  Reads loader metadata only. Leave the Amazon remote alone.\n",color,status,action);
    consoleUpdate(nullptr);
}
}
int main(int,char **){
    consoleInit(nullptr);padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for(unsigned i=0;i<arsetupinput::SupportedPlayers;++i)padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;bool consumed=false;
    Draw("\x1b[33;1m","READY","Press Joy-Con A once.  + Exit.");
    while(appletMainLoop()){
        arsetupinput::Frame frame{};
        for(unsigned i=0;i<pads.size();++i){padUpdate(&pads[i]);frame[i]={padGetStyleSet(&pads[i]),
            unsigned(pads[i].active_id_mask)|(unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};}
        const auto sample=input.Update(frame);if(sample.down&HidNpadButton_Plus)break;
        if(!consumed && sample.down==HidNpadButton_A && sample.held==HidNpadButton_A){
            consumed=true;Draw("\x1b[33;1m","CHECKING / SAVING","Please wait.");
            const u64 run=armGetSystemTick();u64 context{};char directory[512]{};bool saved=false;f::Record r{};r.run=run;
            if(run && R_SUCCEEDED(svcGetProcessId(&context,CUR_PROCESS_HANDLE)) && context && Prepare(directory,run,context) &&
               Save(directory,"started.bin",f::Seal(r))){
                Backend backend;r=f::Collect(backend,boot_layout_manifest::Expected,run);
                saved=Save(directory,"report.bin",r);
            }
            if(!saved)Draw("\x1b[31;1m","SAVE INCOMPLETE","Press +, then open FTPD. Do not retry.");
            else if(r.verdict==f::Verdict::Matched)Draw("\x1b[32;1m","LAYOUT MATCHED - SAVED","Press +, then open FTPD.");
            else Draw("\x1b[33;1m","REPORT SAVED - NEEDS REVIEW","Press +, then open FTPD. Do not retry.");
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
