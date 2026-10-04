// SPDX-License-Identifier: GPL-2.0-only
// Minimal host model ONLY for testing the actual HDLS adapter's requests.
// Not an SDK ABI/layout or service-permission proof; the real SDK build is separate.
#pragma once
#include <cstddef>
#include <cstdint>
using Result = std::uint32_t;
inline constexpr std::uint64_t HidNpadButton_A=1ull<<0, HidNpadButton_B=1ull<<1,
    HidNpadButton_Plus=1ull<<10, HidNpadButton_Left=1ull<<12, HidNpadButton_Up=1ull<<13,
    HidNpadButton_Right=1ull<<14, HidNpadButton_Down=1ull<<15, HiddbgNpadButton_Home=1ull<<18;
inline constexpr unsigned HidDeviceType_FullKey15=3, HidNpadInterfaceType_USB=1;
struct HiddbgHdlsSessionId { std::uint64_t id{}; };
struct HiddbgHdlsHandle { std::uint64_t handle{}; };
struct HiddbgHdlsDeviceInfo {
    unsigned deviceType{}, npadInterfaceType{}, singleColorBody{}, singleColorButtons{};
};
struct HiddbgHdlsState {
    std::uint64_t buttons{};
    unsigned battery_level{}, flags{};
    struct { int x{}, y{}; } analog_stick_l{}, analog_stick_r{};
};
Result hiddbgInitialize();
Result hiddbgAttachHdlsWorkBuffer(HiddbgHdlsSessionId *, void *, std::size_t);
Result hiddbgAttachHdlsVirtualDevice(HiddbgHdlsHandle *, const HiddbgHdlsDeviceInfo *);
Result hiddbgSetHdlsState(HiddbgHdlsHandle, const HiddbgHdlsState *);
Result hiddbgDetachHdlsVirtualDevice(HiddbgHdlsHandle);
Result hiddbgReleaseHdlsWorkBuffer(HiddbgHdlsSessionId);
void hiddbgExit();
