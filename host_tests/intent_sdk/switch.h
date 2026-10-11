// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "../filter_sdk/switch.h"
struct BtdrvGattAttributeUuid { std::uint8_t bytes[20]; };
enum { BtdrvBleEventType_ScanResult=6 };
struct BtdrvBleEventInfo {
    struct { std::uint32_t result;std::uint8_t status,device_type,ble_addr_type,address[6]; } scan_result;
};
