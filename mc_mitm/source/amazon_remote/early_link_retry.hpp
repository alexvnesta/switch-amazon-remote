// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "connection_evidence.hpp"
#include "central_bond_wire.hpp"

namespace amazon_remote {
// Narrow recovery of an observed Connected -> Disconnected/0x3e sequence
// BEFORE adopting a link. These are target-matched snapshots, not ownership
// tokens. Never authorize disconnect/cancel, authentication or input from them.
// One accepted target HCI command, unchanged history, matching callback tuple,
// and two absent user/system views are all required for another public connect.
class EarlyLinkRetry {
public:
    enum class Decision { Waiting, Settle, Retry, Refuse };
    static constexpr unsigned MaxAttempts=3;
    bool Configure(std::uint32_t nonce,std::uint64_t start,std::uint64_t deadline) {
        if(nonce_ || !nonce || !start || deadline<=start || deadline-start>60000)return false;
        nonce_=nonce;scope_start_=start;deadline_=deadline;return true;
    }
    bool Begin(const ConnectionEvidenceWire &events,const central_bond::HciTrace &hci,std::uint64_t now) {
        if(!nonce_ || attempts_>=MaxAttempts || !now || now>=deadline_ ||
           (attempts_ && (!authorized_ || deadline_-now<30000 ||
             events.target_count!=previous_events_.target_count || hci.count!=previous_hci_.count)) ||
           !Snapshot(events,hci,now))return false;
        baseline_events_=events.target_count;baseline_hci_=hci.count;
        requested_=now;failure_=absence_=0;authorized_=absent_confirmed_=false;
        ++attempts_;return true;
    }
    Decision Observe(const ConnectionEvidenceWire &events,const central_bond::HciTrace &hci,std::uint64_t now) {
        if(!attempts_ || !Snapshot(events,hci,now) || now<requested_ || now>=deadline_)return Decision::Refuse;
        const auto n=events.target_count-baseline_events_,hn=hci.count-baseline_hci_;
        if(n>2 || hn>1)return Decision::Refuse; // competing/ambiguous outcomes
        if(!n)return Decision::Waiting;
        const auto &up=events.events[baseline_events_];
        if(up.boot_ms<requested_ || up.status!=0 || up.result || up.reason ||
           !up.client_if || !up.connection_id || up.connection_id==0xffffffff)return Decision::Refuse;
        if(n<2)return Decision::Waiting;
        const auto &down=events.events[baseline_events_+1];
        if(down.boot_ms<up.boot_ms || down.boot_ms-up.boot_ms>1000 || down.status!=2 ||
           down.result!=0x1a || down.reason!=0x3e || down.client_if!=up.client_if ||
           down.connection_id!=up.connection_id)return Decision::Refuse;
        if(!hn)return Decision::Waiting; // a missing receipt never permits a retry
        const auto &command=hci.entries[baseline_hci_];
        if(command.status || command.own_address_type || command.boot_ms<requested_ ||
           command.boot_ms>up.boot_ms)return Decision::Refuse;
        failure_=down.boot_ms;return Decision::Settle;
    }
    Decision Absent(std::uint64_t now,bool both_absent) {
        if(!failure_ || !Clock(now) || now<failure_ || now>=deadline_ || !both_absent)return Decision::Refuse;
        if(!absence_)absence_=now;
        if(now-absence_<250)return Decision::Settle;
        absent_confirmed_=true;
        if(attempts_>=MaxAttempts || deadline_-now<30000 ||
           previous_events_.target_count>ConnectionEvidenceCapacity-2 || previous_hci_.count>=4)return Decision::Refuse;
        if(now-failure_<1000)return Decision::Settle;
        authorized_=true;return Decision::Retry;
    }
    bool CompletedAbsent()const{return absent_confirmed_;}
    unsigned Attempts()const{return attempts_;}
    std::uint64_t FailureTime()const{return failure_;}
private:
    bool Clock(std::uint64_t now){if(!now || now<last_)return false;last_=now;return true;}
    bool Snapshot(const ConnectionEvidenceWire &events,const central_bond::HciTrace &hci,std::uint64_t now) {
        if(!Clock(now) || !ValidConnectionEvidence(events) || events.overflow ||
           events.target_count<previous_events_.target_count ||
           hci.magic!=central_bond::HciTraceMagic || hci.version!=1 || hci.supported!=1 ||
           hci.dropped || hci.count>4 || hci.count<previous_hci_.count)return false;
        for(unsigned i=0;i<events.target_count;++i) {
            if(events.events[i].boot_ms>now)return false;
            if(i<previous_events_.target_count && std::memcmp(&events.events[i],&previous_events_.events[i],sizeof events.events[i]))return false;
        }
        if(hci.count) {
            if(hci.window_nonce!=nonce_ || hci.capture_start_ms<scope_start_ || hci.capture_start_ms>now)return false;
            if(previous_hci_.count && hci.capture_start_ms!=previous_hci_.capture_start_ms)return false;
        } else if(hci.window_nonce || hci.capture_start_ms)return false;
        for(unsigned i=0;i<4;++i) {
            const auto &e=hci.entries[i];
            if(i<hci.count) {
                if(!e.boot_ms || e.boot_ms>now || e.boot_ms<hci.capture_start_ms || e.status>255 || e.own_address_type>3 ||
                   (i && e.boot_ms<hci.entries[i-1].boot_ms))return false;
            } else if(e.boot_ms || e.status || e.own_address_type)return false;
            if(i<previous_hci_.count && std::memcmp(&e,&previous_hci_.entries[i],sizeof e))return false;
        }
        previous_events_=events;previous_hci_=hci;return true;
    }
    ConnectionEvidenceWire previous_events_{};
    central_bond::HciTrace previous_hci_{};
    std::uint32_t nonce_{},baseline_events_{},baseline_hci_{};
    std::uint64_t scope_start_{},deadline_{},requested_{},last_{},failure_{},absence_{};
    unsigned attempts_{};
    bool authorized_{},absent_confirmed_{};
};
}
