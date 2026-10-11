// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "observation_checkpoint.hpp"
#include "../../driver-capability-p1/deferred_read_state.hpp"

namespace ardeferredreader {
namespace d=amazon_remote::deferred_read;
namespace t=amazon_remote::deferred_transport;
namespace j=arobservation;
struct Record {
    std::uint32_t magic{0x31524441},version{1};
    std::uint64_t run{},reader_context{};
    d::Snapshot snapshot{};
    std::uint32_t checksum{},reserved{};
};
static_assert(sizeof(Record)==176 && offsetof(Record,checksum)==168);
inline std::uint32_t Checksum(const Record &r) {
    const auto *bytes=reinterpret_cast<const std::uint8_t*>(&r);std::uint32_t hash=2166136261;
    for(std::size_t i=0;i<offsetof(Record,checksum);++i)hash=(hash^bytes[i])*16777619;
    return hash;
}
inline bool Valid(const Record &r) {
    return r.magic==0x31524441 && r.version==1 && r.run && r.reader_context && !r.reserved &&
        d::Valid(r.snapshot) && r.snapshot.nonce==r.run && r.checksum==Checksum(r);
}
enum class Outcome {Saved,ReadErrorSaved,InvalidReplySaved,PersistenceFailed};
inline constexpr std::uint32_t Invalid=0xffff0001,IdentityMismatch=0xffff0002,PollLimit=0xffff0003;

// Explicit foreground action only. The nonce is this reader run ID. There is
// one Begin, no retries, and <=120 getter polls. Sleeps/SDK are in the backend.
// Calls themselves are synchronous: this is not a hard IPC cancellation bound.
template<class Store,class Backend,class Clock> Outcome Collect(Store &store,Backend &backend,Clock &clock,
        std::uint64_t run,std::uint64_t context,const std::array<std::uint8_t,32> &manifest,Record &record) {
    j::Journal journal;
    if(!journal.Start(store,run,context,clock.Now(),manifest) ||
       !journal.Advance(store,j::Stage::BeforeRead,clock.Now()))return Outcome::PersistenceFailed;
    std::uint32_t result=backend.CheckIdentity();
    if(!result) {
        // Persist the successful precondition before any native read request.
        if(!store.SaveIdentityMatch(run,context))return Outcome::PersistenceFailed;
        result=backend.Begin(run);
    }
    d::Snapshot snapshot{};bool got=false,invalid=false;
    if(!result) {
        for(unsigned poll=0;poll<120;++poll) {
            d::Snapshot candidate{};
            result=backend.Read(candidate);if(result)break;
            if(!d::Valid(candidate) || candidate.nonce!=run){invalid=true;result=Invalid;break;}
            snapshot=candidate;
            got=true;if(d::Terminal(snapshot.state))break;
            if(poll==119){result=PollLimit;break;}
            backend.Wait();
        }
    }
    if(!journal.Advance(store,j::Stage::ReadReturned,clock.Now(),result))return Outcome::PersistenceFailed;
    auto outcome=invalid?Outcome::InvalidReplySaved:Outcome::ReadErrorSaved;
    j::cap::Reply summary{};
    if(got && !invalid) {
        record={};record.run=run;record.reader_context=context;record.snapshot=snapshot;record.checksum=Checksum(record);
        if(!store.SaveSnapshot(record))return Outcome::PersistenceFailed;
        if(!result) {
            outcome=Outcome::Saved;
            if(snapshot.state==d::State::Complete && !snapshot.rejected_events) {
                t::Event event{};amazon_remote::deferred_capability::Read state{};
                if(t::Decode(snapshot.event.data(),snapshot.event.size(),run,event))
                    amazon_remote::deferred_capability::DecodeRecord(&event.record,sizeof event.record,state,summary);
            }
        }
    }
    if(!journal.Advance(store,j::Stage::Finished,clock.Now(),result,summary))return Outcome::PersistenceFailed;
    return outcome;
}
}
