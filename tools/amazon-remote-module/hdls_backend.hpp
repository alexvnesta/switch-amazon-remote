// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <switch.h>
#include "controller.hpp"

namespace armodule {
static_assert(HidNpadButton_A == (1ull<<0) && HidNpadButton_B == (1ull<<1));
static_assert(HidNpadButton_Plus == (1ull<<10) && HidNpadButton_Left == (1ull<<12));
static_assert(HidNpadButton_Up == (1ull<<13) && HidNpadButton_Right == (1ull<<14));
static_assert(HidNpadButton_Down == (1ull<<15) && HiddbgNpadButton_Home == (1ull<<18));
class HdlsBackend {
public:
    Result Open() { return hiddbgInitialize(); }
    Result AttachSession() { return hiddbgAttachHdlsWorkBuffer(&session_, buffer_, sizeof buffer_); }
    Result AttachDevice() {
        HiddbgHdlsDeviceInfo info{};
        // Matches the public sys-con HDLS Pro-controller convention. This is a
        // virtual pad, not a claim that the remote is a native Switch device.
        info.deviceType = HidDeviceType_FullKey15;
        info.npadInterfaceType = HidNpadInterfaceType_USB;
        info.singleColorBody = 0x222222ff; info.singleColorButtons = 0xaaaaaaff;
        return hiddbgAttachHdlsVirtualDevice(&handle_, &info);
    }
    Result Set(std::uint64_t buttons) {
        HiddbgHdlsState state{};
        state.buttons = buttons; state.battery_level = 4; state.flags = 1;
        return hiddbgSetHdlsState(handle_, &state);
    }
    Result DetachDevice() { return hiddbgDetachHdlsVirtualDevice(handle_); }
    Result ReleaseSession() { return hiddbgReleaseHdlsWorkBuffer(session_); }
    void Close() { hiddbgExit(); }
private:
    alignas(0x1000) unsigned char buffer_[0x1000]{};
    HiddbgHdlsSessionId session_{};
    HiddbgHdlsHandle handle_{};
};
}
