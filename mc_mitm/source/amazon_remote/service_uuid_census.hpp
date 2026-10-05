// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace amazon_remote {
// A non-secret profile UUID identified in the verified stored BTM image.
// Diagnostic comparison only: never substitutes a UUID in a device request.
inline constexpr std::array<std::uint8_t,16> BtmManagedProfileUuid{
    0x6c,0x1c,0xe0,0xb4,0x5d,0x73,0x6a,0x8a,0x1a,0x4c,0xa5,0x4a,0x26,0x3e,0xdc,0xad};
struct ServiceUuidView {
    bool valid_length{}, managed_profile_match{};
    std::uint32_t size{};
    std::array<std::uint8_t,16> bytes{};
};
inline ServiceUuidView InspectServiceUuid(std::uint32_t size,const std::uint8_t *bytes) {
    ServiceUuidView out{};out.size=size;
    out.valid_length=bytes && (size==2 || size==4 || size==16);
    if(!out.valid_length)return out; // No out-of-range access or partial UUID claim.
    for(std::size_t i=0;i<size;++i)out.bytes[i]=bytes[i];
    out.managed_profile_match=size==16 && out.bytes==BtmManagedProfileUuid;
    return out;
}
}
