// SPDX-License-Identifier: GPL-2.0-only
#include "core_security_reader.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace {
bool SaveNew(const arsecurity::Snapshot &s){
    if(!arsecurity::Valid(s))return false;
    constexpr char directory[]="sdmc:/switch/amazon-remote-security-audit";
    if(::mkdir(directory,0700)!=0 && errno!=EEXIST)return false;
    char path[192]{};
    const auto n=std::snprintf(path,sizeof path,"%s/core-%llu-%llu.bin",directory,
        static_cast<unsigned long long>(s.begin_ms),static_cast<unsigned long long>(armGetSystemTick()));
    if(n<0 || static_cast<std::size_t>(n)>=sizeof path)return false;
    const auto fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool wrote=::write(fd,&s,sizeof s)==sizeof s && !::fsync(fd);
    const bool closed=::close(fd)==0;if(!wrote || !closed)return false; // Preserve partial.
    auto *f=std::fopen(path,"rb");if(!f)return false;
    arsecurity::Snapshot back{};
    const bool exact=std::fread(&back,1,sizeof back,f)==sizeof back && std::fgetc(f)==EOF && !std::ferror(f);
    const bool read_closed=std::fclose(f)==0;
    return exact && read_closed && arsecurity::Valid(back) && !std::memcmp(&s,&back,sizeof s);
}
}
int main(int,char **){
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    bool ran=false;const bool supported=hosversionGet()==MAKEHOSVERSION(22,5,0);
    std::printf("Amazon Remote Security Audit - READ ONLY\n\nA once: read retained observer-v7 metadata. No radio/pair/key calls.\nDoes NOT arm capture. Run after the reviewed connection test,\nwithout rebooting between the test and this reader.\nOnly requests/raw link subtypes; NEVER bond/encryption proof.\n+ exits; return to FTPD. Firmware22.5.0: %s\n\n",supported?"PASSED":"REFUSED");
    while(appletMainLoop()){
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!ran && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A){
            ran=true;
            if(!supported)std::printf("REFUSED: unsupported firmware.\n");
            else{
                arsecurity::Backend reader;const auto s=arsecurity::Collect(reader);const bool saved=SaveNew(s);
                std::printf("Open=%08lx getter=%08lx validation=%08lx\nTarget metadata count=%lu overflow=%lu\n",
                    static_cast<unsigned long>(s.open_result),static_cast<unsigned long>(s.query_result),
                    static_cast<unsigned long>(s.validation_result),static_cast<unsigned long>(s.evidence.target_count),
                    static_cast<unsigned long>(s.evidence.overflow));
                for(unsigned i=0;i<s.evidence.target_count;++i){
                    const auto &e=s.evidence.events[i];
                    const char *kind=e.kind==2?"PIN request":(e.kind==3?"SSP request":"Raw link subtype");
                    std::printf("%s time=%llu detail=%08lx\n",kind,static_cast<unsigned long long>(e.boot_ms),static_cast<unsigned long>(e.detail));
                }
                std::printf("\nRecord %s. No addresses/names/passkeys/keys saved.\nZero count/error is inconclusive, NOT 'unbonded'. + then FTPD.\n",
                    saved?"SAVED AND READBACK VERIFIED":"NOT VERIFIED; partial preserved");
            }
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
