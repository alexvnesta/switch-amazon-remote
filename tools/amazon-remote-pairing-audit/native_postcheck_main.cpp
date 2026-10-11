// SPDX-License-Identifier: GPL-2.0-only
// Post1 replaces the request frontend with getters only. No command publication.
#include "native_postcheck.hpp"
#include "backend.hpp"
#include "../amazon-remote-module/navigation_sdk.hpp"
#include "../amazon-remote-module/status_reader.hpp"
#include "../amazon-remote-module/enrollment_storage.hpp"
#include "../amazon-remote-module/config.hpp"
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
namespace {
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
bool Absent(const char *path){struct stat s{};errno=0;return ::stat(path,&s)!=0 && errno==ENOENT;}
bool LoadTarget(araudit::Address &target){
    auto *f=std::fopen("sdmc:/config/amazon-remote/address.txt","rb");if(!f)return false;
    char text[18]{};const bool exact=std::fread(text,1,sizeof text,f)==sizeof text && std::fgetc(f)==EOF && !std::ferror(f);
    const bool closed=std::fclose(f)==0;return exact && closed && armodule::ParseTarget(text,sizeof text,target);
}
bool Load(arnativebond::OwnerResult &o,arnativebond::Record &i,armodule::Status &s){
    auto *fs=fsdevGetDeviceFileSystem("sdmc");
    return fs && !armodule::ReadStatusFrame(*fs,s,Now()) &&
        armodule::ReadExact(arnativebond::OwnerResultPath,o) && armodule::ReadExact(arnativebond::IntentPath,i) &&
        Absent(armodule::CommandPath) && Absent(armodule::CommandTemp);
}
}
int main(int,char **){
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool released=false,ran=false,saved=false,existing=!Absent(arpostcheck::Path);
    std::uint64_t last_draw{};arpostcheck::Record record{};arpostcheck::Collector collector;
    while(appletMainLoop()){
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad),buttons=padGetButtons(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!buttons)released=true;
        if(!existing && !ran && released && down==HidNpadButton_A && buttons==HidNpadButton_A){
            ran=true;arnativebond::OwnerResult owner{};arnativebond::Record intent{};armodule::Status status{};
            araudit::Address target{};std::array<std::uint8_t,32> hash{};
            const bool preflight=hosversionGet()==MAKEHOSVERSION(22,5,0) && armodule::VerifyNavigationBinary(hash) &&
                Absent(arpostcheck::Path) && LoadTarget(target) && Load(owner,intent,status);
            araudit::Backend backend;record=collector.Once(backend,target,owner,intent,status,Now(),preflight);
            // Revalidate after getters; a changed session/evidence is saved but not interpreted.
            if(!record.guard){
                arnativebond::OwnerResult after{};arnativebond::Record after_intent{};armodule::Status after_status{};
                araudit::Address after_target{};
                if(!Load(after,after_intent,after_status) || !LoadTarget(after_target) || after_target!=target ||
                   std::memcmp(&owner,&after,sizeof owner) || std::memcmp(&intent,&after_intent,sizeof intent) ||
                   after_status.last_sequence!=status.last_sequence || arpostcheck::Check(after,after_intent,after_status,Now())!=arpostcheck::Guard::Passed)
                    record.guard=unsigned(arpostcheck::Guard::Changed);
                record.checksum=arpostcheck::Checksum(record);
            }
            saved=arpostcheck::Valid(record) && armodule::SaveNativeExclusive(arpostcheck::Path,record);
        }
        const auto now=Now();
        if(!last_draw || now-last_draw>=250 || down){
            consoleClear();std::printf("\n  AMAZON REMOTE / READ-ONLY POSTCHECK\n\n");
            if(existing)std::printf("  \x1b[33;1mCHECK ALREADY EXISTS - PRESERVED\x1b[0m\n\n  NEXT: + Exit; open FTPD.\n");
            else if(!ran)std::printf("  \x1b[33;1mREAD-ONLY PAIRED-DEVICE CHECK\x1b[0m\n\n  NEXT: Release buttons; press Joy-Con A ONCE.\n\n  No reboot. No remote flashing/pairing mode.\n  Do not open Setup or repeat bonding.\n");
            else if(!saved)std::printf("  \x1b[31;1mSAVE NOT VERIFIED - DO NOT RETRY\x1b[0m\n\n  NEXT: + Exit; open FTPD.\n\n  Any partial record is preserved.\n");
            else std::printf("  \x1b[32;1mSAVED AND READBACK VERIFIED\x1b[0m\n\n  NEXT: + Exit; open FTPD.\n\n  %s\n",record.guard?"Guard refused/changed; no conclusion.":"Getter results saved, including any API errors.");
            std::printf("\n  No pairing requests, radio changes, events or keys.\n  Experimental scoped lists are NOT bond proof.\n\n  +: Exit     Hold R: Technical details\n");
            if(ran && (buttons&HidNpadButton_R)){
                const auto &s=record.snapshot;
                std::printf("\n  Guard=%lu Timing valid=%u\n  Opens user=%08lx system=%08lx\n  Paired user=%08lx target=%lu\n  Paired system=%08lx target=%lu\n",static_cast<unsigned long>(record.guard),arpostcheck::TimedAfterRequest(record),
                    static_cast<unsigned long>(s.user_open),static_cast<unsigned long>(s.btm_open),
                    static_cast<unsigned long>(s.user_paired.result),static_cast<unsigned long>(s.user_paired.matches),
                    static_cast<unsigned long>(s.system_paired.result),static_cast<unsigned long>(s.system_paired.matches));
            }
            last_draw=now;
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
