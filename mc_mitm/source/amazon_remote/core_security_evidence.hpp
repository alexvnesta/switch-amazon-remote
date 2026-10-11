// SPDX-License-Identifier: GPL-2.0-only
// Passive core-event metadata. A request/link event is NOT bond completion.
#pragma once
#include "target_sighting.hpp"
#include <limits>

namespace amazon_remote {
inline constexpr std::uint32_t CoreSecurityMagic=0x41525345, CoreSecurityVersion=1;
inline constexpr std::size_t CoreSecurityCapacity=8;
// Public SDK core-event values/layouts on 12.0.0+. Keep link detail raw:
// the SDK/wiki connection subtype labels disagree; neither is bond proof.
enum class CoreSecurityKind : std::uint32_t { PinRequest=2, SspRequest=3, LinkState=4 };
struct CoreSecurityEvent {
    std::uint64_t boot_ms{};
    std::uint32_t kind{}, detail{}; // SSP Just Works byte or raw link subtype.
};
struct CoreSecurityWire {
    std::uint32_t magic{CoreSecurityMagic}, version{CoreSecurityVersion};
    std::uint32_t target_count{}, overflow{};
    std::array<CoreSecurityEvent,CoreSecurityCapacity> events{};
};
static_assert(sizeof(CoreSecurityEvent)==16 && sizeof(CoreSecurityWire)==144);
static_assert(std::is_trivially_copyable_v<CoreSecurityWire> && std::is_standard_layout_v<CoreSecurityWire>);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

inline bool ValidCoreSecurity(const CoreSecurityWire &wire) {
    if (wire.magic!=CoreSecurityMagic || wire.version!=CoreSecurityVersion ||
        wire.target_count>CoreSecurityCapacity ||
        (wire.overflow && wire.target_count!=CoreSecurityCapacity)) return false;
    for (std::size_t i=0;i<CoreSecurityCapacity;++i) {
        const auto &e=wire.events[i];
        if (i>=wire.target_count) {
            if (e.boot_ms || e.kind || e.detail) return false;
        } else if (!e.boot_ms || (i && e.boot_ms<wire.events[i-1].boot_ms) ||
            e.kind<2 || e.kind>4 || (e.kind==2 && e.detail) ||
            (e.kind==3 && e.detail>255)) return false;
    }
    return true;
}

// Configure once at Startup, before producers. Only the existing CORE event
// reader writes this snapshot (never the BLE reader). Atomic payload words and
// two-attempt readers reuse the connection-evidence publication pattern.
// Retain only metadata: no address/name/passkey/key/raw payload in the wire.
class CoreSecuritySnapshot {
public:
    explicit CoreSecuritySnapshot(std::uint32_t sequence=0):
        sequence_(sequence&~1u),writer_sequence_(sequence&~1u) {
        for (auto &word:words_) word.store(0,std::memory_order_seq_cst);
    }
    bool Configure(const SightingAddress &target) {
        if (configured_.load(std::memory_order_acquire) || observed_.load(std::memory_order_acquire) ||
            !sighting_detail::Usable(target)) return false;
        target_=target; configured_.store(true,std::memory_order_release); return true;
    }
    bool Observe(std::uint32_t type,const std::uint8_t *data,std::size_t size,
            std::uint64_t boot_ms,bool lease_active,bool layout_12_plus) {
        observed_.store(true,std::memory_order_relaxed);
        if (!configured_.load(std::memory_order_acquire) || !lease_active || !layout_12_plus ||
            !data || !boot_ms || type<2 || type>4) return false;
        // Require the complete documented subview, not merely enough bytes to
        // find an address. Never inspect a PIN/name/passkey or reserved bytes.
        const std::size_t required=type==2 ? 0x102 : 0x108;
        const std::size_t address_offset=type==4 ? 4 : 0;
        if (size<required || !std::equal(target_.begin(),target_.end(),data+address_offset) ||
            (writer_.target_count && boot_ms<writer_.events[writer_.target_count-1].boot_ms)) return false;
        if (writer_.target_count==CoreSecurityCapacity) {
            if (writer_.overflow!=std::numeric_limits<std::uint32_t>::max()) ++writer_.overflow;
        } else {
            auto &e=writer_.events[writer_.target_count++];
            e.boot_ms=boot_ms; e.kind=type;
            e.detail=type==3 ? data[0x102] : (type==4 ? sighting_detail::U32(data) : 0);
        }
        sequence_.store(writer_sequence_+1,std::memory_order_seq_cst);
        for (std::size_t i=0;i<Words;++i) {
            std::uint32_t word{};
            std::memcpy(&word,reinterpret_cast<const std::uint8_t *>(&writer_)+4*i,4);
            words_[i].store(word,std::memory_order_seq_cst);
        }
        writer_sequence_+=2; sequence_.store(writer_sequence_,std::memory_order_seq_cst);
        published_.store(true,std::memory_order_release); return true;
    }
    bool Read(CoreSecurityWire &out) const {
        if (!configured_.load(std::memory_order_acquire)) return false;
        if (!published_.load(std::memory_order_acquire)) { out=CoreSecurityWire{}; return true; }
        for (unsigned attempt=0;attempt<2;++attempt) {
            const auto before=sequence_.load(std::memory_order_seq_cst); if (before&1) continue;
            CoreSecurityWire value{};
            for (std::size_t i=0;i<Words;++i) {
                const auto word=words_[i].load(std::memory_order_seq_cst);
                std::memcpy(reinterpret_cast<std::uint8_t *>(&value)+4*i,&word,4);
            }
            if (before!=sequence_.load(std::memory_order_seq_cst) || !ValidCoreSecurity(value)) continue;
            out=value; return true;
        }
        return false;
    }
private:
    static constexpr std::size_t Words=sizeof(CoreSecurityWire)/4;
    SightingAddress target_{};
    CoreSecurityWire writer_{}; // Sole core-event producer only.
    std::atomic<bool> configured_{false},observed_{false},published_{false};
    std::array<std::atomic<std::uint32_t>,Words> words_{};
    std::atomic<std::uint32_t> sequence_{0};
    std::uint32_t writer_sequence_{};
};
}
