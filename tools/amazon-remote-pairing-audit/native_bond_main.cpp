// SPDX-License-Identifier: GPL-2.0-only
#include "native_bond_trial.hpp"
#include "native_bond_guard.hpp"
#include "native_bond_backend.hpp"
#include "core_security_reader.hpp"
#include "../amazon-remote-module/navigation_sdk.hpp"
#include "../amazon-remote-module/status_reader.hpp"
#include "../amazon-remote-module/config.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
constexpr char Directory[]="sdmc:/switch/amazon-remote-native-bond-trial";
constexpr char Intent[]="sdmc:/switch/amazon-remote-native-bond-trial/intent.bin";
template<class T> bool SaveExclusive(const char *path,const T &record){
    const auto fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool wrote=::write(fd,&record,sizeof record)==sizeof record && !::fsync(fd);
    const bool closed=::close(fd)==0;if(!wrote || !closed)return false;
    T back{};return armodule::ReadExact(path,back) && !std::memcmp(&record,&back,sizeof record);
}
template<class T> bool SaveNamed(const char *kind,std::uint64_t time,const T &record){
    if(::mkdir(Directory,0700)!=0 && errno!=EEXIST)return false;
    char path[192]{};const auto n=std::snprintf(path,sizeof path,"%s/%s-%llu-%llu.bin",Directory,kind,
        static_cast<unsigned long long>(time),static_cast<unsigned long long>(armGetSystemTick()));
    return n>0 && std::size_t(n)<sizeof path && SaveExclusive(path,record);
}
struct Platform {
    std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
    std::uint32_t Firmware(){return hosversionGet();}
    bool Supported(){return hosversionGet()==MAKEHOSVERSION(22,5,0);}
    bool AruidZero(){return appletGetAppletResourceUserId()==0;}
    bool VerifyBinary(){
        std::array<std::uint8_t,32> hash{};
        return armodule::VerifyNavigationBinary(hash);
    }
    void QueryProcess(std::uint32_t &opened,std::uint32_t &query,std::uint64_t &pid){
        opened=pmdmntInitialize();
        if(!opened){query=pmdmntGetProcessId(&pid,armodule::ModuleProgramId);pmdmntExit();}
    }
    bool StorageReady(){return fsdevGetDeviceFileSystem("sdmc")!=nullptr;}
    bool ReadStartup(armodule::StartupRecord &out){return armodule::ReadExact(armodule::StartupPath,out);}
    bool ReadContext(amazon_remote::EnrollmentContextRecord &out){return armodule::ReadExact(armodule::EnrollmentContextPath,out);}
    Result ReadStatus(armodule::Status &out,std::uint64_t now){
        auto *fs=fsdevGetDeviceFileSystem("sdmc");
        return fs?armodule::ReadStatusFrame(*fs,out,now):arnativebond::Refusal;
    }
    bool PersistIntent(const arnativebond::Record &record){
        return arnativebond::Valid(record) && SaveExclusive(Intent,record);
    }
};
bool Target(BtdrvAddress &out){
    auto *f=std::fopen("sdmc:/config/amazon-remote/address.txt","rb");if(!f)return false;
    char text[19]{};const auto n=std::fread(text,1,sizeof text,f);
    const bool exact=n==18 && !std::ferror(f);const bool closed=std::fclose(f)==0;
    std::array<std::uint8_t,6> target{};
    if(!exact || !closed || !armodule::ParseTarget(text,n,target))return false;
    std::memcpy(out.address,target.data(),target.size());return true;
}
}
int main(int,char **){
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool released=false,ran=false,audit_ran=false,record_saved=false,core_saved=false,guard_saved=false;
    unsigned guard_count{};
    arnativebond::Trial trial;arnativebond::Record outcome{};
    const char *reason="No request has been sent.";std::uint64_t last_draw{};
    while(appletMainLoop()){
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!padGetButtons(&pad))released=true;
        if(!ran && released && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A){
            ran=true;BtdrvAddress target{};
            if(!Target(target) || (::mkdir(Directory,0700)!=0 && errno!=EEXIST))reason="Target configuration or SD storage is not ready.";
            else{
                Platform platform;arnativebond::GuardBackend backend(platform);
                const auto record=trial.Attempt(backend,target);
                // No extra SD writes inside the request's two-second guard.
                guard_count=backend.diagnostic_count;guard_saved=guard_count>0;
                for(unsigned i=0;i<guard_count;++i){
                    const auto &d=backend.diagnostics[i];
                    guard_saved=(arnativebond::ValidDiagnostic(d) && SaveNamed("guard",d.begin_ms,d)) && guard_saved;
                }
                outcome=record;record_saved=arnativebond::Valid(record) && SaveNamed("trial",record.begin_ms,record);
                reason=backend.reason;
                if(!record.readiness_result && !record.request_issued)reason="Driver or one-shot intent check failed. Do not retry.";
            }
        }
        if(ran && !audit_ran && down==HidNpadButton_Y && padGetButtons(&pad)==HidNpadButton_Y){
            audit_ran=true;arsecurity::Backend reader;const auto snapshot=arsecurity::Collect(reader);
            core_saved=arsecurity::Valid(snapshot) && SaveNamed("core",snapshot.begin_ms,snapshot);
        }
        const auto now=armTicksToNs(armGetSystemTick())/1000000;
        if(!last_draw || now-last_draw>=250 || down){
            consoleClear();std::printf("\n  AMAZON REMOTE / BOND TRIAL\n\n");
            if(!ran){
                amazon_remote::EnrollmentContextRecord context{};armodule::Status state{};armodule::StartupRecord startup{};
                const bool usable=armodule::ReadExact(armodule::EnrollmentContextPath,context) &&
                    armodule::ReadExact(armodule::StatusPath,state) && armodule::ReadExact(armodule::StartupPath,startup) &&
                    arnativebond::SystemReadyBinding(state,startup,context,now,context.expected_handle);
                if(usable)std::printf("  \x1b[33;1mREADY FOR ONE BONDING REQUEST\x1b[0m\n\n  NEXT: Release all buttons, then Joy-Con A ONCE.\n\n  This sends a real native bonding request.\n");
                else std::printf("  \x1b[31;1mCONNECTION CHECK NOT READY\x1b[0m\n\n  NEXT: + Exit; return to Setup.\n\n  Setup must show green CHECK SAVED first.\n");
            }else if(audit_ran){
                std::printf("  %s%s\x1b[0m\n\n  NEXT: + Exit; open Setup; X stops it.\n\n  Then + exits Setup; open FTPD.\n  Do not repeat this trial.\n",
                    core_saved?"\x1b[32;1m":"\x1b[31;1m",core_saved?"DIAGNOSTICS SAVED":"DIAGNOSTICS SAVE FAILED");
            }else if(!outcome.request_issued){
                std::printf("  \x1b[31;1mNO BONDING REQUEST SENT\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  %s\n  Guard log: %s\n  Do not retry A.\n",reason,guard_saved?"SAVED":"NOT SAVED - photograph this screen");
            }else if(!record_saved){
                std::printf("  \x1b[31;1mREQUEST SENT / LOG SAVE FAILED\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  Do not retry the request.\n");
            }else if(outcome.request_result){
                std::printf("  \x1b[31;1mBONDING REQUEST RETURNED AN ERROR\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  Do not retry. Hold R for the error code.\n");
            }else if(now>=outcome.end_ms && now-outcome.end_ms<15000){
                std::printf("  \x1b[33;1mREQUEST SENT - WAITING\x1b[0m\n\n  NEXT: Wait %llus, then press Joy-Con Y.\n\n  Bond completion is NOT confirmed.\n",
                    static_cast<unsigned long long>((15000-(now-outcome.end_ms)+999)/1000));
            }else std::printf("  \x1b[33;1mREQUEST SENT - OUTCOME NOT CONFIRMED\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  No bond or persistence has been confirmed.\n");
            if(ran)std::printf("\n  Guard diagnostics: %s (%u check%s)\n",guard_saved?"SAVED":"NOT SAVED",guard_count,guard_count==1?"":"s");
            std::printf("\n  +: Exit     Hold R: Technical details\n");
            if(ran && (padGetButtons(&pad)&HidNpadButton_R))std::printf("\n  State=%lu Open=%08lx Guard=%08lx Getter=%08lx\n  Issued=%lu Request=%08lx Record_saved=%u\n",
                static_cast<unsigned long>(outcome.state),static_cast<unsigned long>(outcome.open_result),
                static_cast<unsigned long>(outcome.readiness_result),static_cast<unsigned long>(outcome.getter_result),
                static_cast<unsigned long>(outcome.request_issued),static_cast<unsigned long>(outcome.request_result),record_saved);
            last_draw=now;
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
