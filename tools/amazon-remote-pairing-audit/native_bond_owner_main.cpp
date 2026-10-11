// SPDX-License-Identifier: GPL-2.0-only
// GUI sends one bounded command. It never opens btm/btmu/btdrv itself.
#include "native_bond_owner.hpp"
#include "core_security_reader.hpp"
#include "../amazon-remote-module/navigation_sdk.hpp"
#include "../amazon-remote-module/status_reader.hpp"
#include "../amazon-remote-module/enrollment_storage.hpp"
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
namespace {
std::uint64_t Now(){return armTicksToNs(armGetSystemTick())/1000000;}
constexpr char Directory[]="sdmc:/switch/amazon-remote-native-bond-trial";
template<class T> bool SaveNamed(const char *kind,const T &r){
    char path[192]{};const auto n=std::snprintf(path,sizeof path,"%s/%s-%llu-%llu.bin",Directory,kind,
        static_cast<unsigned long long>(Now()),static_cast<unsigned long long>(armGetSystemTick()));
    return n>0 && std::size_t(n)<sizeof path && armodule::SaveNativeExclusive(path,r);
}
bool Ready(armodule::Status &s){
    auto *fs=fsdevGetDeviceFileSystem("sdmc");if(!fs || armodule::ReadStatusFrame(*fs,s,Now()))return false;
    amazon_remote::EnrollmentContextRecord c{};armodule::StartupRecord p{};
    return armodule::ReadExact(armodule::EnrollmentContextPath,c) && armodule::ReadExact(armodule::StartupPath,p) &&
        arnativebond::SystemReadyBinding(s,p,c,Now(),c.expected_handle);
}
bool Absent(const char *path){struct stat v{};errno=0;return ::stat(path,&v)!=0 && errno==ENOENT;}
}
int main(int,char **){
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool released=false,ran=false,published=false,received=false,saved=false,audited=false,core_saved=false;
    std::uint64_t instance{},sequence{},sent{},last_read{},last_draw{};
    arnativebond::OwnerResult outcome{};const char *reason="No command sent.";
    while(appletMainLoop()){
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad),buttons=padGetButtons(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!buttons)released=true;
        if(!ran && released && down==HidNpadButton_A && buttons==HidNpadButton_A){
            ran=true;armodule::Status status{};std::array<std::uint8_t,32> hash{};u64 pid{};
            const auto pm=pmdmntInitialize();Result query=arnativebond::Unknown;
            if(!pm){query=pmdmntGetProcessId(&pid,armodule::ModuleProgramId);pmdmntExit();}
            if(hosversionGet()!=MAKEHOSVERSION(22,5,0) || !armodule::VerifyNavigationBinary(hash))reason="Wrong firmware or Setup module revision.";
            else if(pm || query || !pid || !Ready(status))reason="Setup connection is not ready. No command sent.";
            else if(!Absent(arnativebond::IntentPath) || !Absent(arnativebond::OwnerResultPath))reason="A previous attempt exists. Preserve it; do not retry.";
            else if(!Absent(armodule::CommandPath) || !Absent(armodule::CommandTemp) || status.last_sequence==UINT64_MAX)reason="Command channel is busy. No command sent.";
            else if(::mkdir(Directory,0700)!=0 && errno!=EEXIST)reason="Trial folder is not available.";
            else{
                armodule::Command command{};command.kind=unsigned(armodule::CommandKind::NativeBond);
                command.instance=instance=status.instance;command.sequence=sequence=status.last_sequence+1;
                command.issued_ms=sent=Now();
                // Existing explicit command channel; never retry publication.
                published=armodule::Publish(armodule::CommandTemp,armodule::CommandPath,command);
                reason=published?"Command sent. Waiting for owner result.":"Command publication failed. Do not retry.";
            }
        }
        const auto now=Now();
        if(published && !received && (!last_read || now-last_read>=250)){
            arnativebond::OwnerResult result{};
            if(armodule::ReadExact(arnativebond::OwnerResultPath,result) && arnativebond::MatchesOwnerCommand(result,instance,sequence)){
                outcome=result;received=true;saved=SaveNamed("trial",result.trial);
            }
            last_read=now;
        }
        const bool timed_out=published && !received && now>=sent && now-sent>=15000;
        const bool audit_allowed=!published || timed_out || (received &&
            (!outcome.trial.request_issued || outcome.trial.request_result ||
             (now>=outcome.trial.end_ms && now-outcome.trial.end_ms>=15000)));
        if(ran && !audited && audit_allowed && down==HidNpadButton_Y && buttons==HidNpadButton_Y){
            audited=true;arsecurity::Backend reader;const auto snapshot=arsecurity::Collect(reader);
            core_saved=arsecurity::Valid(snapshot) && SaveNamed("core",snapshot);
        }
        if(!last_draw || now-last_draw>=250 || down){
            consoleClear();std::printf("\n  AMAZON REMOTE / OWNER BOND TRIAL\n\n");
            if(!ran){armodule::Status s{};const bool ready=Ready(s);
                std::printf("  %s%s\x1b[0m\n\n  NEXT: %s\n\n  One native request, through Setup's existing client.\n",
                    ready?"\x1b[33;1m":"\x1b[31;1m",ready?"READY FOR ONE BONDING REQUEST":"CONNECTION CHECK NOT READY",
                    ready?"Release all buttons, then Joy-Con A ONCE.":"+ Exit; use Y in Setup to save the check.");
            }else if(audited)std::printf("  %s%s\x1b[0m\n\n  NEXT: + Exit; open Setup; X stops it.\n\n  Then + exits Setup; open FTPD.\n  Do not repeat this trial.\n",core_saved?"\x1b[32;1m":"\x1b[31;1m",core_saved?"DIAGNOSTICS SAVED":"DIAGNOSTICS SAVE FAILED");
            else if(!published)std::printf("  \x1b[31;1mNO COMMAND SENT\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  %s\n  Do not retry A.\n",reason);
            else if(timed_out)std::printf("  \x1b[31;1mOWNER RESULT NOT RECEIVED\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  Whether a request was sent is UNKNOWN.\n  Do not retry A.\n");
            else if(!received)std::printf("  \x1b[33;1mCOMMAND SENT - WAITING FOR OWNER\x1b[0m\n\n  NEXT: Wait; do not press A again.\n\n  A command is NOT proof of a bonding request.\n");
            else if(!outcome.trial.request_issued)std::printf("  \x1b[31;1mNO BONDING REQUEST SENT\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  Owner's live guard refused. Do not retry A.\n");
            else if(outcome.trial.request_result)std::printf("  \x1b[31;1mBONDING REQUEST RETURNED AN ERROR\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  Do not retry A.\n");
            else if(now>=outcome.trial.end_ms && now-outcome.trial.end_ms<15000)std::printf("  \x1b[33;1mREQUEST SENT - WAITING\x1b[0m\n\n  NEXT: Wait 15 seconds, then Joy-Con Y.\n\n  Bond completion is NOT confirmed.\n");
            else std::printf("  \x1b[33;1mREQUEST SENT - OUTCOME NOT CONFIRMED\x1b[0m\n\n  NEXT: Joy-Con Y: Save diagnostics.\n\n  No bond or persistence has been confirmed.\n");
            std::printf("\n  +: Exit     Hold R: Technical details\n");
            if(received && (buttons&HidNpadButton_R))std::printf("\n  Checks=%lu State=%lu Guard=%08lx Request=%08lx\n  Owner result verified; extra trial copy saved=%u\n",
                static_cast<unsigned long>(outcome.count),static_cast<unsigned long>(outcome.trial.state),static_cast<unsigned long>(outcome.trial.readiness_result),static_cast<unsigned long>(outcome.trial.request_result),saved);
            last_draw=now;
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
