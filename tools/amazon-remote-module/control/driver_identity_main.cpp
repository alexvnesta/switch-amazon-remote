// SPDX-License-Identifier: GPL-2.0-only
// Temporary read-only mode in the existing Setup slot, not a second pairing app.
#include "driver_identity_sdk.hpp"
#include "operator_input.hpp"
#include "../io.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace {
constexpr char Path[]="sdmc:/switch/amazon-remote-navigation17/driver-identity-i1.bin";
bool Absent() { struct stat s{}; return ::stat(Path,&s)!=0 && errno==ENOENT; }
bool Save(const ardriverid::Record &r) {
    if (!ardriverid::Valid(r)) return false;
    FILE *f=std::fopen(Path,"wbx"); if (!f) return false;
    const bool wrote=std::fwrite(&r,1,sizeof r,f)==sizeof r;
    const bool flushed=std::fflush(f)==0;
    const bool synced=::fsync(::fileno(f))==0;
    const bool closed=std::fclose(f)==0;
    ardriverid::Record back{};
    return wrote && flushed && synced && closed && armodule::ReadExact(Path,back) &&
        ardriverid::Valid(back) && !std::memcmp(&r,&back,sizeof r);
}
}
int main(int,char **) {
    consoleInit(nullptr);
    static_assert(HidNpadIdType_No1==0 && HidNpadIdType_No8==7);
    static_assert(arsetupinput::JoyConStyles==(HidNpadStyleTag_NpadHandheld |
        HidNpadStyleTag_NpadJoyDual | HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight));
    padConfigureInput(arsetupinput::SupportedPlayers,HidNpadStyleSet_NpadStandard);
    std::array<PadState,arsetupinput::InputSources> pads{};
    for (unsigned i=0;i<arsetupinput::SupportedPlayers;++i) padInitializeWithMask(&pads[i],1ull<<i);
    padInitialize(&pads[arsetupinput::SupportedPlayers],HidNpadIdType_Handheld);
    arsetupinput::OperatorInput input;
    bool consumed=!Absent(), saved=false;
    const char *title=consumed?"EXISTING REPORT - DO NOT REPEAT":"READY FOR READ-ONLY CHECK";
    const char *action=consumed?"+ Exit and open FTPD.":"Joy-Con A once: read driver build metadata.";
    const char *detail="No reboot. Leave the Amazon remote alone.";
    bool redraw=true; ardriverid::Record record{};
    while (appletMainLoop()) {
        arsetupinput::Frame frame{};
        for (unsigned i=0;i<pads.size();++i) {
            padUpdate(&pads[i]);
            frame[i]={padGetStyleSet(&pads[i]),unsigned(pads[i].active_id_mask) |
                (unsigned(pads[i].active_handheld)<<8),padGetButtons(&pads[i])};
        }
        const auto sample=input.Update(frame);
        if (sample.down&HidNpadButton_Plus) break;
        if (!consumed && sample.down==HidNpadButton_A && sample.held==HidNpadButton_A) {
            consumed=true; ardriverid::Backend backend; record=ardriverid::Collect(backend);
            saved=Save(record);
            title=saved?"SAVED AND READBACK VERIFIED":"REPORT SAVE FAILED";
            action="+ Exit and open FTPD. Do not repeat.";
            detail=record.verdict==unsigned(ardriverid::Verdict::KnownBuildMetadata)
                ? "Known build metadata reported. Live code integrity is NOT verified."
                : "Driver identity needs review. No Bluetooth operation was attempted.";
            redraw=true;
        }
        if (redraw) {
            consoleClear();
            std::printf("\n  AMAZON REMOTE SETUP / DRIVER ID I1\n\n\n  %s%s\x1b[0m\n\n\n  NEXT: %s\n\n  %s\n\n\n  No pairing, key access, driver patch or remote connection.\n\n  + Exit\n",
                saved?"\x1b[32;1m":"\x1b[33;1m",title,action,detail);
            redraw=false;
        }
        consoleUpdate(nullptr); svcSleepThread(20000000);
    }
    consoleExit(nullptr); return 0;
}
