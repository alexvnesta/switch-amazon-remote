// SPDX-License-Identifier: GPL-2.0-only
// Passive, exact-target evidence only. Never authorizes a connection or input.
#pragma once
#include "target_sighting.hpp"
#include <limits>

namespace amazon_remote {
inline constexpr std::uint32_t ConnectionEvidenceMagic=0x41524345, ConnectionEvidenceVersion=1;
inline constexpr std::size_t ConnectionEvidenceCapacity=8;
struct ConnectionEvidenceEvent {
    std::uint64_t boot_ms{};
    std::uint32_t result{}, connection_id{};
    std::uint8_t status{}, client_if{};
    std::uint16_t reason{};
    std::uint32_t reserved{};
};
struct ConnectionEvidenceWire {
    std::uint32_t magic{ConnectionEvidenceMagic}, version{ConnectionEvidenceVersion};
    std::uint32_t target_count{}, overflow{}, reserved0{}, reserved1{};
    std::array<ConnectionEvidenceEvent,ConnectionEvidenceCapacity> events{};
};
static_assert(sizeof(ConnectionEvidenceEvent)==24 && sizeof(ConnectionEvidenceWire)==216);
static_assert(std::is_trivially_copyable_v<ConnectionEvidenceWire>);

inline bool ValidConnectionEvidence(const ConnectionEvidenceWire &wire) {
    if (wire.magic!=ConnectionEvidenceMagic || wire.version!=ConnectionEvidenceVersion ||
        wire.target_count>ConnectionEvidenceCapacity || wire.reserved0 || wire.reserved1 ||
        (wire.overflow && wire.target_count!=ConnectionEvidenceCapacity)) return false;
    for (std::size_t i=0;i<ConnectionEvidenceCapacity;++i) {
        const auto &e=wire.events[i];
        if (e.reserved || (i<wire.target_count && (!e.boot_ms || (i && e.boot_ms<wire.events[i-1].boot_ms)))) return false;
        if (i>=wire.target_count && (e.boot_ms || e.result || e.connection_id || e.status || e.client_if || e.reason)) return false;
    }
    return true;
}

// Configure before the sole existing BLE producer starts. The runtime admits
// Observe only during the explicit one-shot passive lease. Retained records
// remain readable after End/Suspend; no rearm, queue drain or radio call here.
// All shared payload words are atomic. Reader is fixed to two finite attempts.
class ConnectionEvidenceSnapshot {
public:
    explicit ConnectionEvidenceSnapshot(std::uint32_t sequence=0):sequence_(sequence&~1u),writer_sequence_(sequence&~1u) {
        for (auto &word:words_) word.store(0,std::memory_order_seq_cst);
    }
    bool Configure(const SightingAddress &target) {
        if (configured_.load(std::memory_order_acquire) || observed_.load(std::memory_order_acquire) || !sighting_detail::Usable(target)) return false;
        target_=target; configured_.store(true,std::memory_order_release); return true;
    }
    bool Observe(std::uint32_t type,const std::uint8_t *data,std::size_t size,std::uint64_t boot_ms) {
        observed_.store(true,std::memory_order_relaxed);
        if (!configured_.load(std::memory_order_acquire) || type!=4 || !data || size<20 || !boot_ms ||
            !std::equal(target_.begin(),target_.end(),data+12) ||
            (writer_.target_count && boot_ms<writer_.events[writer_.target_count-1].boot_ms)) return false;
        // Do NOT reject nonzero result, unfamiliar status, or nonzero reason:
        // those are exactly the failure evidence this diagnostic is preserving.
        if (writer_.target_count==ConnectionEvidenceCapacity) {
            if (writer_.overflow!=std::numeric_limits<std::uint32_t>::max()) ++writer_.overflow;
        } else {
            auto &e=writer_.events[writer_.target_count++];
            e.boot_ms=boot_ms; e.result=sighting_detail::U32(data);
            e.connection_id=sighting_detail::U32(data+8); e.status=data[4]; e.client_if=data[5];
            e.reason=std::uint16_t(data[18]) | (std::uint16_t(data[19])<<8);
        }
        sequence_.store(writer_sequence_+1,std::memory_order_seq_cst);
        for (std::size_t i=0;i<Words;++i) {
            std::uint32_t word{};std::memcpy(&word,reinterpret_cast<const std::uint8_t *>(&writer_)+4*i,4);
            words_[i].store(word,std::memory_order_seq_cst);
        }
        writer_sequence_+=2;sequence_.store(writer_sequence_,std::memory_order_seq_cst);
        published_.store(true,std::memory_order_release);return true;
    }
    bool Read(ConnectionEvidenceWire &out) const {
        if (!configured_.load(std::memory_order_acquire)) return false;
        if (!published_.load(std::memory_order_acquire)) { out=ConnectionEvidenceWire{};return true; }
        for (unsigned attempt=0;attempt<2;++attempt) {
            const auto before=sequence_.load(std::memory_order_seq_cst);if (before&1) continue;
            ConnectionEvidenceWire value{};
            for (std::size_t i=0;i<Words;++i) {
                const auto word=words_[i].load(std::memory_order_seq_cst);
                std::memcpy(reinterpret_cast<std::uint8_t *>(&value)+4*i,&word,4);
            }
            if (before!=sequence_.load(std::memory_order_seq_cst) || !ValidConnectionEvidence(value)) continue;
            out=value;return true;
        }
        return false;
    }
private:
    static constexpr std::size_t Words=sizeof(ConnectionEvidenceWire)/4;
    SightingAddress target_{};
    ConnectionEvidenceWire writer_{}; // Sole producer only.
    std::atomic<bool> configured_{false},observed_{false},published_{false};
    std::array<std::atomic<std::uint32_t>,Words> words_{};
    std::atomic<std::uint32_t> sequence_{0};
    std::uint32_t writer_sequence_{};
};
}
