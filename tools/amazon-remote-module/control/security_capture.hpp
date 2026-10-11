// SPDX-License-Identifier: GPL-2.0-only
// M2 observes the existing P3 connection. It never requests bonding or keys.
#pragma once
#include "driver_identity.hpp"
#include "../../../mc_mitm/source/amazon_remote/native_metadata.hpp"
#ifdef ARMODULE_BOND_PROPERTIES
#include "../../../mc_mitm/source/amazon_remote/bond_properties.hpp"
#endif
#include <cstring>

namespace arsecuritycapture {
#ifdef ARMODULE_BOND_PROPERTIES
namespace metadata=amazon_remote::bond_properties;
inline constexpr std::uint32_t ReportMagic=0x33535241;
#else
namespace metadata=amazon_remote::native_metadata;
inline constexpr std::uint32_t ReportMagic=0x32535241;
#endif
using Wire=metadata::Wire;
inline constexpr std::uint32_t Unknown=0xffffffff, Invalid=0x4a01;
enum class Outcome : std::uint32_t { Pending, EventsObserved, NoEvents, Refused,
    ReadFailed, Cancelled, CommandNotPublished };
struct Report {
    std::uint32_t magic{ReportMagic},version{1};
    std::uint64_t begin_ms{},end_ms{};
    ardriverid::Record identity{};
    std::uint64_t instance{},link_sequence{},link_published_ms{};
    std::uint32_t probe_result{Unknown},read_result{Unknown},nonce{},outcome{};
    Wire evidence{};
    std::uint32_t checksum{},reserved{};
};
#ifdef ARMODULE_BOND_PROPERTIES
static_assert(sizeof(Report)==976 && offsetof(Report,checksum)==968);
#else
static_assert(sizeof(Report)==848 && offsetof(Report,checksum)==840);
#endif
inline std::uint32_t Checksum(const Report&r) {
    const auto*p=reinterpret_cast<const unsigned char*>(&r);std::uint32_t h=2166136261;
    for(std::size_t i=0;i<offsetof(Report,checksum);++i)h=(h^p[i])*16777619;
    return h;
}
inline bool Valid(const Report&r) {
    if(r.magic!=ReportMagic || r.version!=1 || !r.begin_ms || r.end_ms<r.begin_ms ||
       r.reserved || r.checksum!=Checksum(r) || !r.outcome || r.outcome>6 ||
       !metadata::Valid(r.evidence))return false;
    if(r.outcome==unsigned(Outcome::EventsObserved) || r.outcome==unsigned(Outcome::NoEvents)) {
        if(!ardriverid::Valid(r.identity) || r.identity.verdict!=unsigned(ardriverid::Verdict::KnownBuildMetadata) ||
           !r.instance || !r.link_sequence || r.probe_result || r.read_result || !r.nonce ||
           r.evidence.request_nonce!=r.nonce || r.evidence.probe_nonce!=r.nonce ||
           r.evidence.start_ms<r.begin_ms || r.link_published_ms<r.evidence.probe_ms ||
           r.link_published_ms>r.evidence.deadline_ms || r.end_ms<r.evidence.deadline_ms ||
           bool(r.evidence.count)!=(r.outcome==unsigned(Outcome::EventsObserved)))return false;
    }
    return true;
}
// Backend exposes only loader metadata, fixed M1 probe/getters, time and an
// exclusive report writer. No connection/pairing/disconnect API is available.
class Capture {
public:
    explicit Capture(bool report_absent):blocked_(!report_absent) {}
    template<class B> bool Prepare(B&b) {
        if(blocked_ || finished_)return false;
        if(prepared_)return true;
        report_.begin_ms=b.Now();report_.identity=b.Identity();
        if(!ardriverid::Valid(report_.identity) ||
           report_.identity.verdict!=unsigned(ardriverid::Verdict::KnownBuildMetadata)) {
            Finish(b,Outcome::Refused);return false;
        }
        Wire w{};report_.read_result=b.Read(w);
        if(report_.read_result || !metadata::Valid(w) || w.request_nonce) {
            Finish(b,Outcome::Refused);return false;
        }
        prepared_=true;return true;
    }
    template<class B> bool BeforeLink(B&b,std::uint64_t instance,std::uint64_t sequence) {
        if(!CanStart() || attempted_ || !instance || !sequence)return false;
        attempted_=true;report_.instance=instance;report_.link_sequence=sequence;
        Wire empty{};report_.read_result=b.Read(empty);
        if(report_.read_result || !metadata::Valid(empty) || empty.request_nonce) {
            Finish(b,Outcome::Refused);return false;
        }
        const auto start=b.Now();
        report_.nonce=b.Nonce();
        if(!report_.nonce || !start || start>UINT64_MAX-200) {Finish(b,Outcome::Refused);return false;}
        report_.probe_result=b.Probe(report_.nonce);
        if(report_.probe_result) {Finish(b,Outcome::Refused);return false;}
        // Keep the proven P3 sighting gate. Never publish a delayed Link or
        // queue a retry if the diagnostic bridge misses this bounded check.
        for(unsigned tries=0;tries<21;++tries) {
            const auto now=b.Now();
            if(now<start || now-start>200)break;
            Wire w{};report_.read_result=b.Read(w);
            if(report_.read_result || !metadata::Valid(w))break;
            if(w.request_nonce!=report_.nonce || w.start_ms<start)break;
            if(w.probe_nonce==report_.nonce && w.probe_ms<=b.Now() && !w.count && !w.overflow &&
               b.Now()>=start && b.Now()-start<=200) {
                report_.evidence=w;running_=true;return true;
            }
            b.Wait();
        }
        Finish(b,Outcome::Refused);return false;
    }
    template<class B> void Published(B&b,bool ok) {
        if(!running_ || report_.link_published_ms)return;
        if(!ok) {Finish(b,Outcome::CommandNotPublished);return;}
        report_.link_published_ms=b.Now();
    }
    template<class B> void Poll(B&b) {
        if(!running_)return;
        const auto now=b.Now();
        if(now<report_.evidence.probe_ms || (last_poll_ && now<last_poll_)) {
            Finish(b,Outcome::ReadFailed);return;
        }
        if(last_poll_ && now-last_poll_<100 && now<report_.evidence.deadline_ms)return;
        last_poll_=now;Wire w{};report_.read_result=b.Read(w);
        if(report_.read_result || !metadata::Valid(w) ||
           w.request_nonce!=report_.nonce || w.probe_nonce!=report_.nonce ||
           w.start_ms!=report_.evidence.start_ms || w.probe_ms!=report_.evidence.probe_ms ||
           w.probe_ms>now || (w.count && w.events[w.count-1].boot_ms>now) ||
           w.count<report_.evidence.count || w.overflow<report_.evidence.overflow ||
           std::memcmp(w.events.data(),report_.evidence.events.data(),report_.evidence.count*sizeof(w.events[0]))) {
            Finish(b,Outcome::ReadFailed);return;
        }
        report_.evidence=w;
        if(b.Now()>=w.deadline_ms)Finish(b,w.count?Outcome::EventsObserved:Outcome::NoEvents);
    }
    template<class B> void Exit(B&b) {
        if(running_) {Poll(b);if(running_)Finish(b,Outcome::Cancelled);}
        else if(prepared_ && !finished_)Finish(b,Outcome::Cancelled);
    }
    bool CanStart()const{return prepared_&&!blocked_&&!finished_;}
    bool Prepared()const{return prepared_;}
    bool Running()const{return running_;}
    bool Finished()const{return finished_;}
    bool Saved()const{return saved_;}
    bool Blocked()const{return blocked_;}
    const Report&Get()const{return report_;}
private:
    template<class B> void Finish(B&b,Outcome outcome) {
        running_=false;finished_=true;report_.end_ms=b.Now();report_.outcome=unsigned(outcome);
        report_.checksum=Checksum(report_);saved_=Valid(report_)&&b.Save(report_);
    }
    Report report_{};
    bool blocked_{},prepared_{},attempted_{},running_{},finished_{},saved_{};
    std::uint64_t last_poll_{};
};
}
