// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "controller_passive.hpp"

namespace amazon_remote::controller_passive {
inline constexpr std::uint32_t SnapshotMagic=0x31505341, ReadCommand=65020, Limit=64;
// Historical observations since this companion instance, not current radio
// state, a reset epoch, controller identity, or permission to configure wake.
struct Snapshot {
    std::uint32_t magic{SnapshotMagic},version{1};
    std::uint64_t instance_ms{};
    std::uint32_t attempts{},accepted{},rejected{},limit_reached{};
    std::uint64_t first_ms{},last_ms{};
    controller_capabilities::Reply first{};
    std::uint32_t conflicting{},clock_error{},reserved0{},reserved1{};
};
static_assert(sizeof(Snapshot)==112 && offsetof(Snapshot,first)==48);

inline bool ValidFirst(const controller_capabilities::Reply &r) {
    namespace c=controller_capabilities;
    if(r.outcome==c::Outcome::NotObserved)return r==c::Reply{};
    if(r.hci_version || r.hci_revision || r.lmp_version || r.manufacturer || r.lmp_subversion ||
       r.length>252 || r.status>255)return false;
    if(r.outcome==c::Outcome::Malformed)return !r.status && !r.filtering && !r.max_filters &&
        !r.scan_storage_bytes && !r.uninterpreted_extension_bytes;
    if(r.outcome==c::Outcome::ControllerError)return r.length && r.status && !r.filtering &&
        !r.max_filters && !r.scan_storage_bytes && !r.uninterpreted_extension_bytes;
    return r.outcome==c::Outcome::Reported && !r.status &&
        (r.length==9 || (r.length>=11 && r.length<=252)) && r.filtering<=1 &&
        r.max_filters<=255 && r.scan_storage_bytes<=65535 &&
        r.uninterpreted_extension_bytes==r.length-9;
}
inline bool Valid(const Snapshot &s) {
    if(s.magic!=SnapshotMagic || s.version!=1 || !s.instance_ms || s.attempts>Limit ||
       s.accepted>s.attempts || s.rejected!=s.attempts-s.accepted || s.limit_reached>1 ||
       (s.limit_reached && s.attempts!=Limit) || s.conflicting>1 || s.clock_error>1 ||
       s.reserved0 || s.reserved1 || !ValidFirst(s.first))return false;
    if(!s.accepted)return !s.first_ms && !s.last_ms && !s.conflicting &&
        s.first==controller_capabilities::Reply{} && (!s.clock_error || s.rejected);
    return s.first.outcome!=controller_capabilities::Outcome::NotObserved &&
        s.first_ms>=s.instance_ms && s.last_ms>=s.first_ms &&
        (!s.conflicting || s.accepted>=2) && (!s.clock_error || s.rejected);
}
// Must be serialized by its owner. No SDK, I/O, allocation, reset, commands,
// or callback-pointer retention. It deliberately has no Clear/retry operation.
class Cache {
public:
    bool Start(std::uint64_t now) {
        if(!now || state_.instance_ms)return false;
        state_.instance_ms=now;return true;
    }
    void Observe(const void *data,std::size_t size,std::uint64_t now) {
        if(!state_.instance_ms)return;
        if(state_.attempts==Limit){state_.limit_reached=1;return;}
        ++state_.attempts;
        if(state_.clock_error || now<state_.instance_ms || (state_.last_ms && now<state_.last_ms)) {
            state_.clock_error=1;++state_.rejected;return;
        }
        controller_capabilities::Reply reply{};
        if(!Decode(data,size,reply)){++state_.rejected;return;}
        if(!state_.accepted){state_.first=reply;state_.first_ms=now;}
        else if(!(state_.first==reply))state_.conflicting=1;
        ++state_.accepted;state_.last_ms=now;
    }
    Snapshot Read() const{return state_;}
private:
    Snapshot state_{};
};
inline controller_capabilities::Reply SummaryReply(const Snapshot &s) {
    // Preserve uncertain details in the complete snapshot, never flatten a
    // conflict/error/overflow into a clean successful checkpoint summary.
    if(!Valid(s) || s.conflicting || s.clock_error || s.rejected || s.limit_reached)return {};
    return s.first;
}
}
