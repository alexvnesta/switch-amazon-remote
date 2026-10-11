// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "observation_checkpoint.hpp"
#include "../../../mc_mitm/source/amazon_remote/controller_passive_cache.hpp"
namespace arpassiveread {
namespace p=amazon_remote::controller_passive;
namespace j=arobservation;
struct Record {
    std::uint32_t magic{0x31525041},version{1};
    std::uint64_t run{},reader_context{};
    p::Snapshot snapshot{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(Record)==144 && offsetof(Record,checksum)==136);
inline std::uint32_t Checksum(const Record &r) {
    auto bytes=reinterpret_cast<const std::uint8_t *>(&r);
    std::uint32_t hash=2166136261;
    for(std::size_t i=0;i<offsetof(Record,checksum);++i)hash=(hash^bytes[i])*16777619;
    return hash;
}
inline bool Valid(const Record &r) {
    return r.magic==0x31525041 && r.version==1 && r.run && r.reader_context &&
        !r.reserved && p::Valid(r.snapshot) && r.checksum==Checksum(r);
}
enum class Outcome { Saved, ReadErrorSaved, InvalidReplySaved, PersistenceFailed };
// Backend::Read is strictly the cached getter, not a Bluetooth operation.
// All filesystem calls remain in the foreground reader, outside callbacks.
template<class Store,class Backend,class Clock> Outcome Collect(Store &store,Backend &backend,Clock &clock,
        std::uint64_t run,std::uint64_t context,const std::array<std::uint8_t,32> &manifest,Record &record) {
    j::Journal journal;
    if(!journal.Start(store,run,context,clock.Now(),manifest) ||
       !journal.Advance(store,j::Stage::BeforeRead,clock.Now()))return Outcome::PersistenceFailed;
    p::Snapshot snapshot{};
    const auto result=backend.Read(snapshot);
    if(!journal.Advance(store,j::Stage::ReadReturned,clock.Now(),result))return Outcome::PersistenceFailed;
    j::cap::Reply summary{};
    Outcome outcome=Outcome::ReadErrorSaved;
    if(!result) {
        if(!p::Valid(snapshot)) {
            summary.outcome=j::cap::Outcome::Malformed;
            outcome=Outcome::InvalidReplySaved; // Do not persist unknown reply bytes.
        } else {
            record={};record.run=run;record.reader_context=context;record.snapshot=snapshot;
            record.checksum=Checksum(record);
            if(!store.SaveSnapshot(record))return Outcome::PersistenceFailed;
            summary=p::SummaryReply(snapshot);outcome=Outcome::Saved;
        }
    }
    if(!journal.Advance(store,j::Stage::Finished,clock.Now(),result,summary))return Outcome::PersistenceFailed;
    return outcome;
}
}
