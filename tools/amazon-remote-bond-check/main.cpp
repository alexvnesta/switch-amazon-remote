// SPDX-License-Identifier: GPL-2.0-only
#include <switch.h>
#include "check.hpp"
#include "../amazon-remote-module/config.hpp"
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
namespace {
struct Backend {
    using Parameter=BtdrvBleAdvertisePacketParameter;
    Result Open() {return btmuInitialize();}
    Result ParameterFor(std::uint32_t id,Parameter &out) {return btmuGetBleScanFilterParameter(id,&out);}
    Result Paired(Parameter parameter,std::array<arbond::Address,10> &out,std::uint8_t &count) {
        std::array<BtdrvAddress,10> addresses{};
        const auto rc=btmuBleGetPairedDevices(parameter,addresses.data(),addresses.size(),&count);
        if(!rc && count<=addresses.size())
            for(unsigned i=0;i<count;++i)std::memcpy(out[i].data(),addresses[i].address,out[i].size());
        return rc;
    }
    void Close() {btmuExit();}
};
bool LoadTarget(arbond::Address &target) {
    auto *f=std::fopen("sdmc:/config/amazon-remote/address.txt","rb");if(!f)return false;
    char text[18]{};const bool exact=std::fread(text,1,sizeof text,f)==sizeof text && std::fgetc(f)==EOF && !std::ferror(f);
    const bool closed=std::fclose(f)==0;return exact && closed && armodule::ParseTarget(text,sizeof text,target);
}
bool SaveNew(const arbond::Snapshot &s,char (&name)[144]) {
    std::snprintf(name,sizeof name,"sdmc:/switch/amazon-remote-bond-check/bond-%llu-%llu.bin",
        static_cast<unsigned long long>(s.boot_ms),static_cast<unsigned long long>(armGetSystemTick()));
    const auto fd=::open(name,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    const bool written=::write(fd,&s,sizeof s)==sizeof s && !::fsync(fd);
    const bool closed=::close(fd)==0;return written && closed;
}
}
int main(int,char **) {
    consoleInit(nullptr);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    arbond::Snapshot snapshot{};bool ran=false,saved=false;char path[144]{};
    const bool supported=hosversionGet()==MAKEHOSVERSION(22,5,0);
    std::printf("Amazon Remote: read-only paired-list check\n\nA once: query existing system filters and paired-address lists.\nNo scan, pairing, connection, bond changes or event consumption.\nRemote need not be flashing. No reboot needed.\nNo remote MAC or keys are printed/saved.\n\nFirmware22.5.0 guard: %s\n",supported?"PASSED":"REFUSED");
    while(appletMainLoop()) {
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!ran && down==HidNpadButton_A && padGetButtons(&pad)==HidNpadButton_A) {
            ran=true;arbond::Address target{};
            if(!supported || !LoadTarget(target))std::printf("REFUSED: supported firmware/valid configured target required.\n");
            else {
                Backend backend;snapshot=arbond::Collect(backend,target,armTicksToNs(armGetSystemTick())/1000000);
                saved=SaveNew(snapshot,path);
                std::printf("Open Result=%08lx\n",static_cast<unsigned long>(snapshot.open_result));
                for(const auto &q:snapshot.queries)std::printf("Scope=%04lx param=%08lx query=%08lx count=%lu target_matches=%lu\n",
                    static_cast<unsigned long>(q.parameter_id),static_cast<unsigned long>(q.parameter_result),
                    static_cast<unsigned long>(q.query_result),static_cast<unsigned long>(q.returned_count),static_cast<unsigned long>(q.target_matches));
                std::printf("\nRecord: %s\n",saved?"SAVED":"NOT SAVED");
                std::printf("Match = listed in this query, NOT verified encryption/durable bond.\nNo match = inconclusive outside these filter scopes, NOT unpaired.\n+ exits. Return to FTPD; no re-pair/reboot required.\n");
            }
        }
        consoleUpdate(nullptr);svcSleepThread(20000000);
    }
    consoleExit(nullptr);return 0;
}
