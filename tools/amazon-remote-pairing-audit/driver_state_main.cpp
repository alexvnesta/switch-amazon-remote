// SPDX-License-Identifier: GPL-2.0-only
#include "driver_state_reader.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
bool SaveNew(const ardriver::Snapshot &s){
    if(!ardriver::Valid(s))return false;
    constexpr char directory[]="sdmc:/switch/amazon-remote-driver-state";
    if(::mkdir(directory,0700)!=0 && errno!=EEXIST)return false;
    char path[192]{};
    const auto n=std::snprintf(path,sizeof path,"%s/driver-%llu-%llu.bin",directory,
        static_cast<unsigned long long>(s.begin_ms),static_cast<unsigned long long>(armGetSystemTick()));
    if(n<0 || static_cast<std::size_t>(n)>=sizeof path)return false;
    const auto fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool wrote=::write(fd,&s,sizeof s)==sizeof s && !::fsync(fd);
    const bool closed=::close(fd)==0;if(!wrote || !closed)return false; // Preserve partial.
    auto *f=std::fopen(path,"rb");if(!f)return false;
    ardriver::Snapshot back{};
    const bool exact=std::fread(&back,1,sizeof back,f)==sizeof back && std::fgetc(f)==EOF && !std::ferror(f);
    const bool read_closed=std::fclose(f)==0;
    return exact && read_closed && ardriver::Valid(back) && !std::memcmp(&s,&back,sizeof s);
}
}
int main(int,char **){
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool ran=false;const bool supported=hosversionGet()==MAKEHOSVERSION(22,5,0);
    std::printf("Amazon Remote Driver State - READ ONLY\n\nNo reboot or remote pairing mode needed.\nJoy-Con A once: read Bluetooth enabled getter (command 100).\nDoes NOT initialize, scan, connect, pair or read keys/events.\nNo bonding, encryption or reconnect claims from this result.\n+ exits; return to FTPD. Firmware22.5.0: %s\n\n",supported?"PASSED":"REFUSED");
    while(appletMainLoop()){
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!ran && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A){
            ran=true;
            if(!supported)std::printf("REFUSED: unsupported firmware.\n");
            else{
                ardriver::Backend reader;const auto s=ardriver::Collect(reader);const bool saved=SaveNew(s);
                std::printf("Open=%08lx getter=%08lx validation=%08lx\n",
                    static_cast<unsigned long>(s.open_result),static_cast<unsigned long>(s.query_result),
                    static_cast<unsigned long>(s.validation_result));
                if(!s.open_result && !s.query_result && !s.validation_result)
                    std::printf("Bluetooth enabled=%lu\n",static_cast<unsigned long>(s.enabled_raw));
                else std::printf("Enabled state UNKNOWN; not a false response.\n");
                std::printf("Record %s. No identity or security material saved.\n+ then FTPD; do not rerun Navigation17.\n",
                    saved?"SAVED AND READBACK VERIFIED":"NOT VERIFIED; partial preserved");
            }
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
