// SPDX-License-Identifier: GPL-2.0-only
// M3 adds selected scalar properties only. No key values or persistent bonding.
#pragma once
#include "target_sighting.hpp"
#include <limits>

namespace amazon_remote::bond_properties {
inline constexpr std::uint32_t PacketMagic=0x314d5241, ExtensionMagic=0x334d5241, WireMagic=0x334e5241;
inline constexpr std::uint32_t EventType=0x6a, ProbeCommand=6;
inline constexpr std::uint64_t WindowMs=15000;
struct Event {
    std::uint64_t boot_ms{}; std::uint32_t kind{},detail{},flags{};
    std::uint8_t security{},key_bytes{},identity_type{},identity_match{},reason{},reserved[3]{};
    std::uint32_t padding{};
};
inline std::uint32_t Flags(unsigned kind,unsigned detail) {
    if(kind==3)return 16;
    if(kind!=12)return 0;
    switch(detail){case 1:case 8:return 3;case 2:return 12;case 4:case 32:return 1;}
    return 0;
}
inline bool PropertiesValid(const Event &e) {
    return e.flags==Flags(e.kind,e.detail) && !e.padding &&
        !e.reserved[0]&&!e.reserved[1]&&!e.reserved[2] &&
        ((e.flags&1)||!e.security) && ((e.flags&2)||!e.key_bytes) &&
        ((e.flags&4)||!e.identity_type) && ((e.flags&8)||!e.identity_match) &&
        ((e.flags&16)||!e.reason) && e.identity_match<=1;
}
struct Wire {
    std::uint32_t magic{WireMagic},version{1},request_nonce{},probe_nonce{};
    std::uint64_t start_ms{},deadline_ms{},probe_ms{};
    std::uint32_t count{},overflow{};
    std::array<Event,8> events{};
};
static_assert(sizeof(Wire)==304 && sizeof(Event)==32);
inline bool Valid(const Wire &w) {
    if(w.magic!=WireMagic || w.version!=1 || w.count>w.events.size() ||
       (w.overflow && w.count!=w.events.size()))return false;
    if(!w.request_nonce) return !w.probe_nonce && !w.start_ms && !w.deadline_ms &&
        !w.probe_ms && !w.count && !w.overflow &&
        std::all_of(w.events.begin(),w.events.end(),[](const auto&e){return !e.boot_ms&&!e.kind&&!e.detail&&!e.flags&&PropertiesValid(e);});
    if(!w.start_ms || w.start_ms>std::numeric_limits<std::uint64_t>::max()-WindowMs ||
       w.deadline_ms!=w.start_ms+WindowMs)return false;
    if(w.probe_nonce && (w.probe_nonce!=w.request_nonce || w.probe_ms<w.start_ms ||
       w.probe_ms>w.deadline_ms))return false;
    if(!w.probe_nonce && (w.probe_ms || w.count || w.overflow))return false;
    for(std::size_t i=0;i<w.events.size();++i) {
        const auto&e=w.events[i];
        if(i>=w.count) { if(e.boot_ms||e.kind||e.detail||e.flags||!PropertiesValid(e))return false; }
        else if(e.boot_ms<w.probe_ms || e.boot_ms>w.deadline_ms ||
            (i && e.boot_ms<w.events[i-1].boot_ms) ||
            (e.kind!=3 && e.kind!=12) || e.detail>255 || !PropertiesValid(e))return false;
    }
    return true;
}
// One request per boot. IPC is the one-time controller; the existing core
// event thread is the sole producer. No blocking, heap or file operations.
class Snapshot {
public:
    Snapshot() { for(auto &w:words_)w.store(0,std::memory_order_relaxed); }
    bool Configure(const SightingAddress &target) {
        if(configured_ || !sighting_detail::Usable(target))return false;
        target_=target;configured_=true;return true;
    }
    bool Begin(std::uint32_t nonce,std::uint64_t now) {
        if(!configured_ || !nonce || !now || now>std::numeric_limits<std::uint64_t>::max()-WindowMs)return false;
        bool expected=false;
        if(!used_.compare_exchange_strong(expected,true))return false;
        start_.store(now,std::memory_order_relaxed);
        nonce_.store(nonce,std::memory_order_release);return true;
    }
    bool Observe(const void *raw,std::size_t size,std::uint64_t now) {
        const auto nonce=nonce_.load(std::memory_order_acquire);
        const auto start=start_.load(std::memory_order_relaxed);
        if(!configured_ || !nonce || !raw || size<32 || now<start || now>start+WindowMs)return false;
        const auto*d=static_cast<const std::uint8_t*>(raw);
        if(sighting_detail::U32(d)!=PacketMagic || !std::equal(target_.begin(),target_.end(),d+4))return false;
        if(sighting_detail::U32(d+16)!=ExtensionMagic)return false;
        Event e{};e.boot_ms=now;e.kind=d[10];e.detail=d[11];e.flags=sighting_detail::U32(d+20);
        e.security=d[24];e.key_bytes=d[25];e.identity_type=d[26];e.identity_match=d[27];e.reason=d[28];
        if(d[29]||d[30]||d[31]||!PropertiesValid(e))return false;
        const auto token=sighting_detail::U32(d+12);
        if(d[10]==0) {
            if(d[11]!=1 || token!=nonce || writer_.probe_nonce)return false;
            writer_.probe_nonce=nonce;writer_.probe_ms=now;
        } else {
            if(!writer_.probe_nonce || token || (d[10]!=3 && d[10]!=12) ||
                now<writer_.probe_ms || (writer_.count && now<writer_.events[writer_.count-1].boot_ms))return false;
            if(writer_.count==writer_.events.size()) {
                if(writer_.overflow!=std::numeric_limits<std::uint32_t>::max())++writer_.overflow;
            } else writer_.events[writer_.count++]=e;
        }
        seq_.store(sequence_+1,std::memory_order_seq_cst);
        for(std::size_t i=0;i<Words;++i){std::uint32_t v{};std::memcpy(&v,reinterpret_cast<const char*>(&writer_)+4*i,4);words_[i].store(v,std::memory_order_seq_cst);}
        sequence_+=2;seq_.store(sequence_,std::memory_order_seq_cst);
        published_.store(true,std::memory_order_release);return true;
    }
    bool Read(Wire &out) const {
        if(!configured_)return false;
        const auto nonce=nonce_.load(std::memory_order_acquire);
        if(!nonce){out=Wire{};return true;}
        for(unsigned attempt=0;attempt<2;++attempt){
            Wire w{};
            if(published_.load(std::memory_order_acquire)){
                const auto before=seq_.load(std::memory_order_seq_cst);if(before&1)continue;
                for(std::size_t i=0;i<Words;++i){const auto v=words_[i].load(std::memory_order_seq_cst);std::memcpy(reinterpret_cast<char*>(&w)+4*i,&v,4);}
                if(before!=seq_.load(std::memory_order_seq_cst))continue;
            }
            w.request_nonce=nonce;w.start_ms=start_.load(std::memory_order_relaxed);w.deadline_ms=w.start_ms+WindowMs;
            if(!Valid(w))continue;
            out=w;return true;
        }
        return false;
    }
private:
    static constexpr std::size_t Words=sizeof(Wire)/4;
    SightingAddress target_{};bool configured_{}; // immutable before threads
    std::atomic<bool> used_{false},published_{false};
    std::atomic<std::uint32_t> nonce_{0},seq_{0};
    std::atomic<std::uint64_t> start_{0};
    std::array<std::atomic<std::uint32_t>,Words> words_{};
    Wire writer_{};std::uint32_t sequence_{};
};
}
