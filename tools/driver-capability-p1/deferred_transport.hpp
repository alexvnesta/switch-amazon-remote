// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "deferred_endpoint.hpp"

namespace amazon_remote::deferred_transport {
namespace d=deferred_capability;
inline constexpr std::uint32_t RequestCode=11,RequestMagic=0x31524341;
inline constexpr std::uint32_t EventType=0x6d,EventMagic=0x314c4341;
struct Request {
    std::uint32_t command{RequestCode},magic{RequestMagic};
    std::uint64_t nonce{};
};
struct Event {
    std::uint32_t magic{EventMagic},version{1};
    std::uint64_t nonce{};
    d::Record record{};
};
static_assert(sizeof(Request)==16 && offsetof(Request,nonce)==8);
static_assert(sizeof(Event)==80 && offsetof(Event,record)==16);
inline bool Decode(const void *bytes,std::size_t size,std::uint64_t expected_nonce,Event &out) {
    if(!bytes || size!=sizeof(Event) || !expected_nonce)return false;
    Event event{};std::memcpy(&event,bytes,sizeof event);
    d::Read state{};controller_capabilities::Reply reply{};
    if(event.magic!=EventMagic || event.version!=1 || event.nonce!=expected_nonce ||
       !d::DecodeRecord(&event.record,sizeof event.record,state,reply))return false;
    out=event;return true;
}
}
