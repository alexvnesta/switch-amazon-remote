// SPDX-License-Identifier: GPL-2.0-only
// Getter-only fake SDK surface for testing the actual audit backend adapter.
#pragma once
#include <cstdint>
#include <cstddef>
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using s32=std::int32_t;using Result=u32;
struct BtdrvAddress {u8 address[6]{};};
struct BtdrvBleAdvertisePacketParameter {u16 company_id{};u8 pattern_data[6]{};};
enum BtmProfile {BtmProfile_Hid=1};
struct BtmDeviceCondition {BtdrvAddress address;u32 profile{};};
Result btmuInitialize();void btmuExit();
Result btmuBleGetPairedDevices(BtdrvBleAdvertisePacketParameter,BtdrvAddress*,u8,u8*);
Result btmInitialize();void btmExit();
Result btmGetDeviceCondition(BtmProfile,BtmDeviceCondition*,std::size_t,s32*);
Result btmsysInitialize();void btmsysExit();Result btmsysIsGamepadPairingStarted(bool*);
