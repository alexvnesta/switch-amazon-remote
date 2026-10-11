// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "stratosphere.hpp"
struct BtdrvBleAdvertisement {std::uint8_t size,type,data[29];};
struct BtdrvBleAdvertiseFilter {std::uint8_t index;BtdrvBleAdvertisement adv;std::uint8_t mask[29],mask_size;};
inline std::uint64_t armGetSystemTick(){return fake_filter::now*1000000;}
inline std::uint64_t armTicksToNs(std::uint64_t tick){return tick;}
