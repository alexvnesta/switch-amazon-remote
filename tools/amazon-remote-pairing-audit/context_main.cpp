// SPDX-License-Identifier: GPL-2.0-only
#include "backend.hpp"
#include "../amazon-remote-module/config.hpp"
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
namespace {
bool LoadTarget(araudit::Address &target) {
    auto *f=std::fopen("sdmc:/config/amazon-remote/address.txt","rb");if(!f)return false;
    char text[18]{};const bool exact=std::fread(text,1,sizeof text,f)==sizeof text && std::fgetc(f)==EOF && !std::ferror(f);
    const bool closed=std::fclose(f)==0;return exact && closed && armodule::ParseTarget(text,sizeof text,target);
}
bool SaveNew(const araudit::ContextSnapshot &s) {
    char name[192]{};
    const auto n=std::snprintf(name,sizeof name,"sdmc:/switch/amazon-remote-enrollment-context/context-%llu-%llu.bin",
        static_cast<unsigned long long>(s.begin_ms),static_cast<unsigned long long>(armGetSystemTick()));
    if(n<0 || static_cast<unsigned>(n)>=sizeof name)return false;
    const auto fd=::open(name,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool written=::write(fd,&s,sizeof s)==sizeof s && !::fsync(fd);
    const bool closed=::close(fd)==0;if(!written || !closed)return false;
    auto *f=std::fopen(name,"rb");if(!f)return false;
    araudit::ContextSnapshot readback{};
    const bool exact=std::fread(&readback,1,sizeof readback,f)==sizeof readback && std::fgetc(f)==EOF && !std::ferror(f);
    const bool read_closed=std::fclose(f)==0;return exact && read_closed && !std::memcmp(&s,&readback,sizeof s);
}
void Print(const char *label,const araudit::Query &q,std::uint32_t handle=0) {
    std::printf("%s Result=%08lx count=%lu target=%lu invalid=%lu handle=%lu\n",label,
        static_cast<unsigned long>(q.result),static_cast<unsigned long>(q.count),static_cast<unsigned long>(q.matches),
        static_cast<unsigned long>(q.invalid),static_cast<unsigned long>(handle));
}
}
int main(int,char **) {
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool ran=false;const bool supported=hosversionGet()==MAKEHOSVERSION(22,5,0);
    std::printf("Amazon Remote Enrollment Context - READ ONLY v3\n\nA once: compare user/system connection and scoped paired lists,\nand read native pairing-window flag. No pair/scan/connect/events/keys.\nNo reboot or flashing. Leave current navigation/probe apps closed.\n+ exits; return FTPD. Firmware22.5.0: %s\n\n",supported?"PASSED":"REFUSED");
    while(appletMainLoop()) {
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!ran && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A) {
            ran=true;araudit::Address target{};
            if(!supported || !LoadTarget(target))std::printf("REFUSED: supported firmware/valid target required.\n");
            else {
                araudit::Backend backend;const auto s=araudit::CollectContext(backend,target);
                const bool saved=SaveNew(s);
                std::printf("Client results user=%08lx BTM=%08lx SYS=%08lx\n",static_cast<unsigned long>(s.user_open),
                    static_cast<unsigned long>(s.btm_open),static_cast<unsigned long>(s.sys_open));
                Print("User connected",s.user_connections.list,s.user_connections.target_handle);
                Print("System connected",s.system_connections.list,s.system_connections.target_handle);
                Print("User paired (AD-derived scope)",s.user_paired);Print("System paired (same scope)",s.system_paired);
                constexpr const char *relations[]={"INDETERMINATE/error or timing","NEITHER API lists target","USER ONLY","SYSTEM ONLY","SAME TARGET HANDLE","DIFFERENT TARGET HANDLES"};
                std::printf("Connection comparison: %s\nNative pairing flag Result=%08lx active=%lu\nRecord %s\n",
                    relations[unsigned(araudit::Compare(s))],static_cast<unsigned long>(s.pairing_query),
                    static_cast<unsigned long>(s.pairing_started),saved?"SAVED AND READBACK VERIFIED":"NOT VERIFIED; partial preserved");
                std::printf("\nSequential foreground snapshot, NOT the earlier pairing call state.\nSame handle does NOT prove ownership/bonding. Neither is not radio absence.\nPaired scope still experimental. Flag does NOT prove enrollment permission.\nNo target address/device record/key saved. + then FTPD; no reboot.\n");
            }
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
