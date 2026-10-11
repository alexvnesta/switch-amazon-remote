// SPDX-License-Identifier: GPL-2.0-only
// Passive lease-scoped counters. No raw addresses, advertisements, or keys.
#pragma once
#include "target_sighting.hpp"
#include "capture_lease.hpp"
#include <limits>

namespace amazon_remote {
enum class ReceiverCount : std::size_t {
    Events, Scan, OtherType, ShortInput, ResultError, StatusRejected,
    DeviceRejected, AddressTypeRejected, AddressMismatch, CountRejected,
    Eligible, Count
};
inline constexpr std::size_t ReceiverCountSize = std::size_t(ReceiverCount::Count);
inline constexpr std::uint32_t ReceiverMagic=0x41525244, ReceiverVersion=1;
using ReceiverCounts=std::array<std::uint32_t,ReceiverCountSize>;
struct ReceiverRecord {
    std::uint32_t magic{ReceiverMagic}, version{ReceiverVersion};
    std::uint64_t begin_ms{}, end_ms{}, deadline_ms{};
    std::uint32_t lease_state{}, reserved{};
    ReceiverCounts counts{};
    std::uint32_t checksum{};
};
static_assert(sizeof(ReceiverRecord)==88 && offsetof(ReceiverRecord,checksum)==84);
static_assert(std::is_standard_layout_v<ReceiverRecord> && std::is_trivially_copyable_v<ReceiverRecord>);
inline std::uint32_t ReceiverChecksum(const ReceiverRecord &r) {
    const auto *p=reinterpret_cast<const std::uint8_t *>(&r);
    std::uint32_t value=2166136261u;
    for(std::size_t i=0;i<offsetof(ReceiverRecord,checksum);++i)value=(value^p[i])*16777619u;
    return value;
}
inline bool ValidReceiverRecord(const ReceiverRecord &r) {
    return r.magic==ReceiverMagic && r.version==ReceiverVersion && !r.reserved &&
        r.begin_ms && r.begin_ms<=std::numeric_limits<std::uint64_t>::max()-60000 &&
        r.deadline_ms==r.begin_ms+60000 && r.end_ms>=r.begin_ms &&
        (r.lease_state==unsigned(CaptureLeaseState::Expired) || r.lease_state==unsigned(CaptureLeaseState::Ended)) &&
        (r.lease_state!=unsigned(CaptureLeaseState::Expired) || r.end_ms>=r.deadline_ms) &&
        r.checksum==ReceiverChecksum(r);
}

class ReceiverDiagnostic {
public:
    // Single existing BLE producer, before the original snapshot's checks.
    // First-rejection counters mirror TargetView but do not admit anything.
    void Observe(std::uint32_t type,const std::uint8_t *data,std::size_t size,
            const SightingAddress &target,bool active) {
        if(!active)return;
        Increment(ReceiverCount::Events);
        if(type!=6){Increment(ReceiverCount::OtherType);return;}
        Increment(ReceiverCount::Scan);
        if(!data || size<328){Increment(ReceiverCount::ShortInput);return;}
        if(sighting_detail::U32(data)){Increment(ReceiverCount::ResultError);return;}
        if(data[4]!=2){Increment(ReceiverCount::StatusRejected);return;}
        if(data[5]>2){Increment(ReceiverCount::DeviceRejected);return;}
        if(data[6]>1){Increment(ReceiverCount::AddressTypeRejected);return;}
        if(!sighting_detail::Usable(target) || !std::equal(target.begin(),target.end(),data+7)){
            Increment(ReceiverCount::AddressMismatch);return;
        }
        if(data[323]>10){Increment(ReceiverCount::CountRejected);return;}
        Increment(ReceiverCount::Eligible);
    }
    ReceiverCounts Read()const {
        ReceiverCounts result{};
        // Approximate multi-counter snapshot: fields are atomic, not a claim
        // of callback identity, ownership, security, or sum coherence.
        for(std::size_t i=0;i<result.size();++i)result[i]=counts_[i].load(std::memory_order_relaxed);
        return result;
    }
private:
    void Increment(ReceiverCount index) {
        auto &field=counts_[std::size_t(index)];
        // Sole writer: bounded, saturating load/store, no CAS retry loop.
        const auto value=field.load(std::memory_order_relaxed);
        if(value!=std::numeric_limits<std::uint32_t>::max())field.store(value+1,std::memory_order_relaxed);
    }
    std::array<std::atomic<std::uint32_t>,ReceiverCountSize> counts_{};
};

// Main-thread storage gate; one exclusive attempt only. A failure never
// deletes/replaces/retries a partial, nor changes capture or radio permission.
class ReceiverRecordWriter {
public:
    bool Attempted()const{return attempted_;}
    template<class Storage>
    bool Save(std::uint64_t begin,std::uint64_t end,std::uint32_t state,
            const ReceiverCounts &counts,Storage &storage) {
        if(attempted_ || !begin || begin>std::numeric_limits<std::uint64_t>::max()-60000)return false;
        ReceiverRecord r{};r.begin_ms=begin;r.end_ms=end;r.deadline_ms=begin+60000;r.lease_state=state;r.counts=counts;
        r.checksum=ReceiverChecksum(r);
        if(!ValidReceiverRecord(r))return false;
        attempted_=true;
        if(!storage.Create(sizeof r) || !storage.Write(r))return false;
        ReceiverRecord readback{};
        return storage.Read(readback) && ValidReceiverRecord(readback) && !std::memcmp(&r,&readback,sizeof r);
    }
private:
    bool attempted_{};
};
}
