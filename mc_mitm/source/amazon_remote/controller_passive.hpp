// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "controller_capabilities.hpp"
#include <array>
#include <cstring>
namespace amazon_remote::controller_passive {
// Dedicated metadata event, not a command interface. The driver callback
// selects VendorCapabilities before constructing it. No addresses or keys.
inline constexpr std::uint32_t EventType=0x6c, Magic=0x31504341;
struct Event {
    std::uint32_t magic{Magic},version{1},parameter_length{},copied{};
    std::array<std::uint8_t,12> prefix{};
    std::uint32_t reserved{};
};
static_assert(sizeof(Event)==32 && offsetof(Event,prefix)==16);
inline bool Decode(const void *bytes,std::size_t size,controller_capabilities::Reply &out) {
    if(!bytes || size<sizeof(Event)) return false;
    Event event{};std::memcpy(&event,bytes,sizeof event);
    if(event.magic!=Magic || event.version!=1 || event.reserved || event.parameter_length>65535 ||
       (event.copied!=0 && event.copied!=1 && event.copied!=9)) return false;
    for(std::size_t i=event.copied;i<event.prefix.size();++i) if(event.prefix[i]) return false;
    if(event.copied==1 && (!event.prefix[0] || !event.parameter_length || event.parameter_length>252)) return false;
    if(event.copied==9 && (event.prefix[0] || event.parameter_length<9 ||
       event.parameter_length==10 || event.parameter_length>252)) return false;
    return controller_capabilities::Decode(controller_capabilities::VendorCapabilities,
        event.copied ? event.prefix.data() : nullptr,event.parameter_length,out);
}
}
