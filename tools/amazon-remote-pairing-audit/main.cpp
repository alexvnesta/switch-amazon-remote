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
bool SaveNew(const araudit::Snapshot &s) {
    char name[144]{};
    std::snprintf(name,sizeof name,"sdmc:/switch/amazon-remote-pairing-audit/audit-%llu-%llu.bin",
        static_cast<unsigned long long>(s.boot_ms),static_cast<unsigned long long>(armGetSystemTick()));
    const auto fd=::open(name,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool written=::write(fd,&s,sizeof s)==sizeof s && !::fsync(fd);
    const bool closed=::close(fd)==0;return written && closed;
}
void Print(const char *label,const araudit::Query &q) {
    std::printf("%s: result=%08lx count=%lu target_matches=%lu invalid=%lu\n",label,
        static_cast<unsigned long>(q.result),static_cast<unsigned long>(q.count),
        static_cast<unsigned long>(q.matches),static_cast<unsigned long>(q.invalid));
}
}
int main(int,char **) {
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool ran=false;const bool supported=hosversionGet()==MAKEHOSVERSION(22,5,0);
    std::printf("Amazon Remote Pairing Audit v2 - READ ONLY\n\nA once: Amazon-profile paired-list query, current native HID list,\nand native pairing-window getter. No scan/connect/pair/unpair.\nNo keys, addresses, names or device records are saved.\nNo reboot or flashing remote required. + exits.\n\nFirmware22.5.0: %s\n",supported?"PASSED":"REFUSED");
    while(appletMainLoop()) {
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!ran && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A) {
            ran=true;araudit::Address target{};
            if(!supported || !LoadTarget(target))std::printf("REFUSED: supported firmware/valid target required.\n");
            else {
                araudit::Backend backend;const auto s=araudit::Collect(backend,target,armTicksToNs(armGetSystemTick())/1000000);
                const bool saved=SaveNew(s);
                std::printf("Client open results BLE=%08lx BTM=%08lx SYS=%08lx\n",static_cast<unsigned long>(s.ble_open),static_cast<unsigned long>(s.btm_open),static_cast<unsigned long>(s.system_open));
                Print("Observed Amazon profile",s.ble);Print("Current native HID",s.hid);
                std::printf("Pairing-window result=%08lx active=%lu\nRecord %s\n",static_cast<unsigned long>(s.pairing_query),static_cast<unsigned long>(s.pairing_started),saved?"SAVED":"NOT SAVED");
                std::printf("\nProfile is derived from captured AD bytes, NOT validated for enrollment.\nNo match is inconclusive; a match is not encryption/durable-bond proof.\nCurrent HID list is NOT the saved-bond database.\nExit with + and reopen FTPD. No further pairing/reboot needed.\n");
            }
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
