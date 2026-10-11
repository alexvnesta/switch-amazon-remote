// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstdint>
namespace amazon_remote {
// A3 preview: only a completed saved-key session may resume after sleep.
// Fixed budget bounds retained native request contexts. No timeout/error retry.
class WakeSession {
public:
    static constexpr unsigned Capacity=8;
    bool Complete(std::uint32_t nonce,std::uint32_t connection,std::uint64_t epoch) {
        if(terminal_ || sleeping_ || complete_ || !nonce || !connection || !epoch || attempts_>=Capacity)return false;
        nonce_=nonce;connection_=connection;epoch_=epoch;++attempts_;complete_=true;return true;
    }
    bool Sleep() {
        if(terminal_ || !complete_ || attempts_>=Capacity)return false;
        if(!sleeping_){++serial_;sleeping_=true;}
        awake_=false;
        return true;
    }
    bool Awake() { if(terminal_ || !sleeping_)return false;awake_=true;return true; }
    bool Consume(std::uint32_t serial,std::uint32_t nonce,std::uint32_t connection,std::uint64_t epoch,bool absent) {
#ifdef ARMODULE_SETUP_SLEEP_R3
        if(pending_)return false;
#endif
        if(!Ready(absent) || serial!=serial_ || nonce!=nonce_ || connection!=connection_ || epoch!=epoch_)return false;
        sleeping_=awake_=complete_=false;return true;
    }
#ifdef ARMODULE_SETUP_SLEEP_R3
    // R3: a prepared saved-peer attempt that never completed may also retire at
    // sleep. Its identity is the C1 peer nonce; it takes one budget slot when it
    // first sleeps. Repeated sleep-side power events for that nonce are idempotent.
    bool SleepPending(std::uint32_t peer_nonce) {
        if(terminal_ || complete_ || !peer_nonce)return false;
        if(sleeping_){
            if(!pending_ || peer_nonce!=pending_nonce_)return false;
            awake_=false;return true;
        }
        // Only pause when a slot remains to resume into; otherwise stop cleanly.
        if(attempts_+1>=Capacity)return false;
        pending_nonce_=peer_nonce;pending_=true;++attempts_;++serial_;sleeping_=true;awake_=false;return true;
    }
    bool ConsumePending(std::uint32_t serial,std::uint32_t peer_nonce,bool absent) {
        if(!pending_ || !Ready(absent) || serial!=serial_ || peer_nonce!=pending_nonce_)return false;
        sleeping_=awake_=pending_=false;pending_nonce_=0;return true;
    }
    bool Pending()const{return pending_;}
    std::uint32_t PendingNonce()const{return pending_nonce_;}
#endif
    void Stop(){terminal_=true;awake_=false;}
#ifdef ARMODULE_SETUP_SLEEP_R3
    bool Ready(bool absent)const{return !terminal_ && (complete_ || pending_) && sleeping_ && awake_ && absent && attempts_<Capacity;}
#else
    bool Ready(bool absent)const{return !terminal_ && complete_ && sleeping_ && awake_ && absent && attempts_<Capacity;}
#endif
    bool Sleeping()const{return !terminal_ && sleeping_;}
    unsigned Serial()const{return serial_;}
    unsigned Remaining()const{return Capacity-attempts_;}
private:
    unsigned attempts_{},serial_{};
    std::uint32_t nonce_{},connection_{};
    std::uint64_t epoch_{};
    bool complete_{},sleeping_{},awake_{},terminal_{};
#ifdef ARMODULE_SETUP_SLEEP_R3
    std::uint32_t pending_nonce_{};
    bool pending_{};
#endif
};
}
