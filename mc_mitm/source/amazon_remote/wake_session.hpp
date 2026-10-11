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
        if(!Ready(absent) || serial!=serial_ || nonce!=nonce_ || connection!=connection_ || epoch!=epoch_)return false;
        sleeping_=awake_=complete_=false;return true;
    }
    void Stop(){terminal_=true;awake_=false;}
    bool Ready(bool absent)const{return !terminal_ && complete_ && sleeping_ && awake_ && absent && attempts_<Capacity;}
    bool Sleeping()const{return !terminal_ && sleeping_;}
    unsigned Serial()const{return serial_;}
    unsigned Remaining()const{return Capacity-attempts_;}
private:
    unsigned attempts_{},serial_{};
    std::uint32_t nonce_{},connection_{};
    std::uint64_t epoch_{};
    bool complete_{},sleeping_{},awake_{},terminal_{};
};
}
