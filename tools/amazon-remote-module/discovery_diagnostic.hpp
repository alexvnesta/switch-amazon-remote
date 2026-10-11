// SPDX-License-Identifier: GPL-2.0-only
// Observes existing getter calls only. Never grants discovery/link permission.
#pragma once
#include "../../mc_mitm/source/amazon_remote/target_sighting.hpp"
#include "../../mc_mitm/source/amazon_remote/capture_lease.hpp"
#include <array>
#include <cstdio>
#include <limits>

namespace armodule {
enum class DiscoveryStep : unsigned {
    NoService, LeaseReadError, LeaseInactiveBefore, SightingReadError,
    LeaseInactiveAfter, NoSnapshot, Invalid, Stale, Future, NotCandidate,
    FreshCandidate, Count
};
inline const char *DiscoveryName(DiscoveryStep step) {
    constexpr std::array names{"no_service","lease_read_error","lease_inactive_before",
        "sighting_read_error","lease_inactive_after","no_snapshot","invalid",
        "stale","future","not_candidate","fresh_candidate"};
    const auto i=unsigned(step);return i<names.size()?names[i]:"unknown";
}
struct DiscoverySample {
    DiscoveryStep step{};
    std::uint32_t result{}, lease_state{};
    std::uint64_t now{}, deadline{}, captured{}, scan_start{};
};
class DiscoveryDiagnostic {
public:
    using Logger=void (*)(const char *);
    static constexpr unsigned TransitionLimit=8;
    void Record(const DiscoverySample &sample,Logger log,const amazon_remote::SightingWire *wire=nullptr) {
        const auto index=unsigned(sample.step);
        if(index>=counts_.size() || finished_)return;
        if(counts_[index]!=std::numeric_limits<std::uint32_t>::max())++counts_[index];
        const bool changed=!have_ || last_.step!=sample.step || last_.result!=sample.result;
        last_=sample;have_=true;
        if(log && changed && transitions_<TransitionLimit) {
            ++transitions_;char line[352]{};
            std::snprintf(line,sizeof line,"DISCOVERY_STEP=%s Result=%08lx lease=%lu now=%llu deadline=%llu captured=%llu scan_start=%llu; getter_observation_only=1",
                DiscoveryName(sample.step),static_cast<unsigned long>(sample.result),static_cast<unsigned long>(sample.lease_state),
                static_cast<unsigned long long>(sample.now),static_cast<unsigned long long>(sample.deadline),
                static_cast<unsigned long long>(sample.captured),static_cast<unsigned long long>(sample.scan_start));log(line);
        }
        // Only the original classifier's fresh, structurally valid, exact-target
        // NotCandidate result admits this diagnostic dump. Not link admission.
        // One sample, at most10 existing AD slots, no address/foreign records.
        if(log && !dumped_ && sample.step==DiscoveryStep::NotCandidate && wire && wire->raw[323]<=10) {
            dumped_=true;log("DISCOVERY_AD_SAMPLE: first fresh exact-target noncandidate; declared SDK slots only; no pairing or identity proof");
            constexpr char hex[]="0123456789abcdef";
            for(unsigned slot=0;slot<wire->raw[323];++slot) {
                char bytes[63]{};const auto *ad=wire->raw.data()+13+slot*31;
                for(unsigned j=0;j<31;++j){bytes[j*2]=hex[ad[j]>>4];bytes[j*2+1]=hex[ad[j]&15];}
                char line[128]{};std::snprintf(line,sizeof line,"DISCOVERY_AD slot=%u raw31=%s",slot,bytes);log(line);
            }
        }
    }
    void Finish(Logger log) {
        if(finished_)return;
        finished_=true;
        if(!log)return;
        char line[352]{};
        std::snprintf(line,sizeof line,"DISCOVERY_SUMMARY no_service=%lu lease_error=%lu lease_before=%lu sighting_error=%lu lease_after=%lu no_snapshot=%lu invalid=%lu stale=%lu future=%lu noncandidate=%lu fresh=%lu",
            Count(DiscoveryStep::NoService),Count(DiscoveryStep::LeaseReadError),Count(DiscoveryStep::LeaseInactiveBefore),
            Count(DiscoveryStep::SightingReadError),Count(DiscoveryStep::LeaseInactiveAfter),Count(DiscoveryStep::NoSnapshot),
            Count(DiscoveryStep::Invalid),Count(DiscoveryStep::Stale),Count(DiscoveryStep::Future),
            Count(DiscoveryStep::NotCandidate),Count(DiscoveryStep::FreshCandidate));log(line);
        std::snprintf(line,sizeof line,"DISCOVERY_FINAL step=%s Result=%08lx lease=%lu now=%llu deadline=%llu captured=%llu scan_start=%llu transitions=%u ad_dump=%u; absence_not_proven=1",
            have_?DiscoveryName(last_.step):"not_sampled",static_cast<unsigned long>(last_.result),static_cast<unsigned long>(last_.lease_state),
            static_cast<unsigned long long>(last_.now),static_cast<unsigned long long>(last_.deadline),
            static_cast<unsigned long long>(last_.captured),static_cast<unsigned long long>(last_.scan_start),transitions_,dumped_);log(line);
    }
    unsigned long Count(DiscoveryStep step)const{return counts_[unsigned(step)];}
private:
    std::array<std::uint32_t,unsigned(DiscoveryStep::Count)> counts_{};
    DiscoverySample last_{};unsigned transitions_{};bool have_{},dumped_{},finished_{};
};

// Exact old short-circuit order: lease getter, clock/live check, sighting
// getter, second clock/live check, third clock/original AssessSighting.
// Reader/Live are adapters to existing calls, not new clients or callbacks.
template<class Reader,class Live>
bool ObserveDiscovery(Reader &reader,Live live,const amazon_remote::SightingAddress &target,
        std::uint64_t scan_start,amazon_remote::CaptureLeaseWire &lease,
        amazon_remote::SightingWire &wire,DiscoveryDiagnostic &diagnostic,DiscoveryDiagnostic::Logger log) {
    DiscoverySample sample{};sample.scan_start=scan_start;
    sample.result=reader.Lease(lease);sample.lease_state=lease.state;sample.deadline=lease.deadline_boot_ms;
    if(sample.result){sample.step=DiscoveryStep::LeaseReadError;diagnostic.Record(sample,log);return false;}
    sample.now=reader.Clock();
    if(!live(lease,sample.now)){sample.step=DiscoveryStep::LeaseInactiveBefore;diagnostic.Record(sample,log);return false;}
    sample.result=reader.Sighting(wire);
    if(sample.result){sample.step=DiscoveryStep::SightingReadError;diagnostic.Record(sample,log);return false;}
    sample.captured=wire.captured_boot_ms;sample.now=reader.Clock();
    if(!live(lease,sample.now)){sample.step=DiscoveryStep::LeaseInactiveAfter;diagnostic.Record(sample,log);return false;}
    sample.now=reader.Clock();
    const auto assessment=amazon_remote::AssessSighting(wire,target,scan_start,sample.now);
    switch(assessment) {
        case amazon_remote::SightingAssessment::Invalid:
            sample.step=(!wire.magic && !wire.version && !wire.captured_boot_ms &&
                std::all_of(wire.raw.begin(),wire.raw.end(),[](auto v){return v==0;}))?DiscoveryStep::NoSnapshot:DiscoveryStep::Invalid;break;
        case amazon_remote::SightingAssessment::Stale:sample.step=DiscoveryStep::Stale;break;
        case amazon_remote::SightingAssessment::Future:sample.step=DiscoveryStep::Future;break;
        case amazon_remote::SightingAssessment::NotCandidate:sample.step=DiscoveryStep::NotCandidate;break;
        case amazon_remote::SightingAssessment::FreshCandidate:sample.step=DiscoveryStep::FreshCandidate;break;
    }
    diagnostic.Record(sample,log,assessment==amazon_remote::SightingAssessment::NotCandidate?&wire:nullptr);
    return assessment==amazon_remote::SightingAssessment::FreshCandidate;
}
}
