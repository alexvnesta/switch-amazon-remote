// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "stratosphere.hpp"
struct BtdrvBleAdvertisement{std::uint8_t size,type,data[29];};
struct BtdrvBleAdvertiseFilter{std::uint8_t index;BtdrvBleAdvertisement adv;std::uint8_t mask[29],mask_size;};
enum {BtdrvBleEventType_ScanResult=6,BtdrvBleEventType_ScanFilter=7};
struct BtdrvBleEventInfo {
    struct {std::uint32_t result;std::uint8_t status,device_type,ble_addr_type,address[6];} scan_result;
    struct {std::uint32_t result,action;} scan_filter;
};
