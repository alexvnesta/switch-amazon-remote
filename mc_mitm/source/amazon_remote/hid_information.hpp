// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "hogp_client.hpp"

namespace amazon_remote {
// HID Service, HID Information (0x2A4A). Flags describe the peripheral, not
// whether this host/controller can wake from suspend. Unknown bits stay visible.
struct HidInformation {
    std::uint16_t bcd_hid{};
    std::uint8_t country{}, flags{};
    bool RemoteWake() const { return flags & 1; }
    bool NormallyConnectable() const { return flags & 2; }
    std::uint8_t OtherFlags() const { return flags & 0xfc; }
};
inline bool DecodeHidInformation(const std::uint8_t *data, std::size_t size, HidInformation &out) {
    if (!data || size != 4) return false;
    out = {std::uint16_t(data[0] | (std::uint16_t(data[1]) << 8)), data[2], data[3]};
    return true;
}
// Do not infer a value from declaration properties or invent a missing handle.
inline bool SelectHidInformation(const Attribute &hid, const Attribute *records,
                                 std::size_t count, Attribute &out) {
    if (hid.kind != Kind::Service || hid.uuid != 0x1812 || !hid.primary ||
        !hid.handle || hid.end <= hid.handle || !records || !count || count >= 100) return false;
    const Attribute *selected = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        const auto &a = records[i];
        if (a.uuid != 0x2a4a) continue;
        if (selected || a.kind != Kind::Characteristic || a.handle <= hid.handle || a.handle > hid.end) return false;
        selected = &a;
    }
    if (!selected) return false;
    out = *selected;
    return true;
}
}
