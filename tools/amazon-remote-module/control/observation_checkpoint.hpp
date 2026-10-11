// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../../mc_mitm/source/amazon_remote/controller_capabilities.hpp"

namespace arobservation {
namespace cap = amazon_remote::controller_capabilities;
// Evidence only, not permission to use a transport. The future integration
// must separately validate its exact transport and save BEFORE invoking it.
enum class Stage : std::uint32_t { Prepared, BeforeRead, ReadReturned, Finished };
struct Checkpoint {
    std::uint32_t magic{0x334a4341}, version{2};
    Stage stage{};
    std::uint32_t sequence{};
    std::uint64_t run{}, reader_context{}, time_ms{};
    std::uint32_t previous_checksum{}, transport_result{};
    std::array<std::uint8_t,32> reviewed_manifest{};
    cap::Reply reply{};
    std::uint32_t reserved{}, checksum{};
};
static_assert(sizeof(Checkpoint)==136 && offsetof(Checkpoint,checksum)==132);
inline std::uint32_t Checksum(const Checkpoint &value) {
    const auto *bytes=reinterpret_cast<const std::uint8_t *>(&value);
    std::uint32_t hash=2166136261;
    for(std::size_t i=0;i<offsetof(Checkpoint,checksum);++i) hash=(hash^bytes[i])*16777619;
    return hash;
}
inline bool EmptyReply(const cap::Reply &reply) { return reply==cap::Reply{}; }
inline bool ValidReply(const cap::Reply &r) {
    if(r.outcome==cap::Outcome::NotObserved) return EmptyReply(r);
    if(r.outcome!=cap::Outcome::Reported && r.outcome!=cap::Outcome::ControllerError &&
       r.outcome!=cap::Outcome::Malformed) return false;
    if(r.hci_version || r.hci_revision || r.lmp_version || r.manufacturer || r.lmp_subversion) return false;
    if(r.length>252 || r.status>255) return false;
    if(r.outcome==cap::Outcome::ControllerError)
        return r.status && r.length && !r.filtering && !r.max_filters &&
               !r.scan_storage_bytes && !r.uninterpreted_extension_bytes;
    if(r.outcome==cap::Outcome::Malformed)
        return !r.status && !r.filtering && !r.max_filters &&
               !r.scan_storage_bytes && !r.uninterpreted_extension_bytes;
    return !r.status && (r.length==9 || (r.length>=11 && r.length<=252)) &&
           r.filtering<=1 && r.max_filters<=255 && r.scan_storage_bytes<=65535 &&
           r.uninterpreted_extension_bytes==r.length-9;
}
inline bool Valid(const Checkpoint &r) {
    bool has_manifest=false;
    for(auto byte:r.reviewed_manifest) has_manifest|=byte!=0;
    if(r.magic!=0x334a4341 || r.version!=2 || unsigned(r.stage)>3 ||
       r.sequence!=unsigned(r.stage) || !r.run || !r.reader_context || !r.time_ms || !has_manifest ||
       r.reserved || r.checksum!=Checksum(r) || !ValidReply(r.reply)) return false;
    if(r.stage==Stage::Prepared && r.previous_checksum) return false;
    if(r.stage!=Stage::Finished && !EmptyReply(r.reply)) return false;
    if(unsigned(r.stage)<unsigned(Stage::ReadReturned) && r.transport_result) return false;
    if(r.transport_result && !EmptyReply(r.reply)) return false;
    return true;
}
inline bool Follows(const Checkpoint &next,const Checkpoint &previous) {
    return Valid(previous) && Valid(next) && next.sequence==previous.sequence+1 &&
           next.run==previous.run && next.reader_context==previous.reader_context && next.time_ms>=previous.time_ms &&
           next.reviewed_manifest==previous.reviewed_manifest &&
           next.previous_checksum==previous.checksum &&
           (next.stage!=Stage::Finished || next.transport_result==previous.transport_result);
}
enum class SlotState { Missing, Present, Invalid };
struct Evidence {
    // valid includes the empty journal. complete means all four records were
    // verified, NOT a successful controller query or hardware capability.
    bool valid{}, complete{};
    unsigned verified_records{}, invalid_slot{4};
    Checkpoint last{};
};
template<class Store> Evidence Inspect(const Store &store) {
    Evidence evidence{};
    bool gap=false;
    for(unsigned slot=0;slot<4;++slot) {
        Checkpoint record{};
        const auto state=store.ReadSlot(slot,record);
        if(state==SlotState::Missing) { gap=true;continue; }
        if(state!=SlotState::Present || gap || !Valid(record) || record.sequence!=slot ||
           (slot && !Follows(record,evidence.last))) {
            evidence.invalid_slot=slot;
            return evidence; // The earlier prefix is evidence only, never restart permission.
        }
        evidence.last=record;
        ++evidence.verified_records;
    }
    evidence.valid=true;
    evidence.complete=evidence.verified_records==4;
    return evidence;
}
// A new process cannot resume an old journal. Existing/partial files block a
// fresh run. Any failed write latches failure for this in-memory owner too.
// Store::Empty checks ALL slots, Store::Save exclusively creates one slot,
// flushes/syncs/closes and reads it back exactly. Neither method may truncate.
class Journal {
public:
    template<class Store> bool Start(Store &store,std::uint64_t run,std::uint64_t reader_context,
            std::uint64_t time,const std::array<std::uint8_t,32> &manifest) {
        if(attempted_) return false;
        attempted_=true;
        Checkpoint first{};first.run=run;first.reader_context=reader_context;first.time_ms=time;
        first.reviewed_manifest=manifest;first.checksum=Checksum(first);
        if(!Valid(first) || !store.Empty() || !store.Save(first)) { failed_=true;return false; }
        latest_=first;started_=true;return true;
    }
    template<class Store> bool Advance(Store &store,Stage stage,std::uint64_t time,
                                      std::uint32_t result=0,const cap::Reply &reply={}) {
        if(!started_ || failed_ || latest_.stage==Stage::Finished) return false;
        Checkpoint next=latest_;next.stage=stage;next.sequence=unsigned(stage);next.time_ms=time;
        next.previous_checksum=latest_.checksum;next.transport_result=result;next.reply=reply;
        next.checksum=Checksum(next);
        if(!Follows(next,latest_) || !store.Matches(latest_) || !store.Save(next)) {
            failed_=true;return false;
        }
        latest_=next;return true;
    }
    const Checkpoint &Latest() const { return latest_; }
    bool Failed() const { return failed_; }
private:
    Checkpoint latest_{};
    bool attempted_{},started_{},failed_{};
};
}
