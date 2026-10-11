// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace amazon_remote::bond::encryption {
using Address=std::array<std::uint8_t,6>;
using Callback=void (*)(const std::uint8_t *,std::uint8_t,void *,std::uint8_t);
// These are trusted driver-local references, NOT an IPC/SD command format.
// HCI handle0 is valid and is not a GATT connection ID or virtual-pad handle.
struct Target {
    Address peer{};
    std::uint64_t driver_instance{},connection_epoch{},record_generation{};
    std::uint16_t hci_handle{0xffff};
    std::uint8_t minimum_key_bytes{16};
    bool require_mitm{};
};
enum class State { Unused, Waiting, Complete, Rejected, Expired, Cancelled, Consumed };
inline bool Valid(const Target &t) {
    bool nonzero=false,not_broadcast=false;
    for(auto byte:t.peer){nonzero|=byte!=0;not_broadcast|=byte!=255;}
    return nonzero && not_broadcast && t.driver_instance && t.connection_epoch &&
        t.record_generation && t.hci_handle<=0x0eff &&
        t.minimum_key_bytes>=7 && t.minimum_key_bytes<=16;
}

// Private, serialized native-driver helper. No production backend/caller is
// installed yet. Backend supplies the AUDITED native SetEncryption ABI plus:
//   Now(): monotonic milliseconds;
//   Admit(t): live central owner, exact verified restore/generation/security,
//             valid LE ACL/entry/handle, idle security and free callback slot;
//   Owns(t): same live driver/connection epoch/identity/handle/record generation;
//   Encrypted(t): current encrypted flag, effective length and required MITM.
// The backend must independently establish these facts. They are not trusted
// merely because this class received booleans, an address or stored metadata.
//
// IMPORTANT LIFETIME: request AND backend must have driver-lifetime storage.
// Timeout/Cancel suppress authorization; they do NOT remove native references.
// There is no reset/rearm, native pointer overwrite, retry, pairing fallback,
// storage I/O, key access or global event listener. Hold native serialization
// through admission/start and each callback/poll, never a filesystem lock.
template<class Backend> class Request {
    struct Context {
        // The legacy API interprets its reference's FIRST byte as BTM action1
        // (existing-key encryption), then returns that same reference in its
        // completion callback. This is NOT public GATT Authentication::NoMitm.
        std::uint8_t action{1};
        Request *owner{};
    } context_{1,this};
    static_assert(offsetof(Context,action)==0);
    Backend &backend_;
    Target target_{};
    State state_{State::Unused};
    std::uint64_t deadline_{},last_{};
    bool callback_seen_{};
    std::uint8_t native_result_{255},completion_result_{255};

    bool CurrentTime() {
        const auto now=backend_.Now();
        if(now<last_ || now>=deadline_){state_=State::Expired;return false;}
        last_=now;return true;
    }
    bool CurrentOwner() {
        if(!backend_.Owns(target_)){state_=State::Rejected;return false;}
        return true;
    }
    static void Completed(const std::uint8_t *peer,std::uint8_t transport,
                          void *reference,std::uint8_t result) {
        // Called only through the native private callback, not a public
        // untrusted-pointer entry point. Native owns the reference lifetime.
        auto &self=*static_cast<Context*>(reference)->owner;
        if(self.callback_seen_)return;
        self.callback_seen_=true;self.completion_result_=result;
        if(self.state_!=State::Waiting)return; // Late callback cannot revive it.
        bool matches=peer && transport==2;
        if(peer)for(unsigned i=0;i<6;++i)matches&=peer[i]==self.target_.peer[i];
        if(!matches || result){self.state_=State::Rejected;return;}
        if(!self.CurrentTime() || !self.CurrentOwner())return;
        self.state_=self.backend_.Encrypted(self.target_) ? State::Complete : State::Rejected;
    }
public:
    explicit Request(Backend &backend):backend_(backend) {}
    Request(const Request &)=delete;
    Request &operator=(const Request &)=delete;
    Request(Request &&)=delete;
    Request &operator=(Request &&)=delete;
    State Begin(const Target &target,std::uint64_t timeout_ms=15000) {
        if(state_!=State::Unused)return state_;
        state_=State::Rejected;
        const auto now=backend_.Now();
        if(!Valid(target) || !now || !timeout_ms || timeout_ms>30000 ||
           now>std::numeric_limits<std::uint64_t>::max()-timeout_ms ||
           !backend_.Admit(target))return state_;
        target_=target;last_=now;deadline_=now+timeout_ms;
        // Immediate success/errors may invoke Completed BEFORE this returns.
        state_=State::Waiting;
        native_result_=backend_.SetEncryption(target_.peer.data(),2,&Completed,&context_);
        if(native_result_>1 || (native_result_==0 && !callback_seen_))state_=State::Rejected;
        if(state_==State::Waiting || state_==State::Complete)Poll();
        return state_;
    }
    State Poll() {
        if(state_==State::Waiting || state_==State::Complete) {
            if(CurrentTime() && CurrentOwner() && state_==State::Complete &&
               !backend_.Encrypted(target_))state_=State::Rejected;
        }
        return state_;
    }
    // One-time authorization to continue authenticated GATT, only after both
    // callback success AND fresh trusted link checks. Not durable bond proof.
    bool TakeReady() {
        if(Poll()!=State::Complete)return false;
        state_=State::Consumed;return true;
    }
    void Cancel() {
        if(state_==State::Waiting || state_==State::Complete)state_=State::Cancelled;
    }
    State GetState() const {return state_;}
    std::uint8_t NativeResult() const {return native_result_;}
    std::uint8_t CompletionResult() const {return completion_result_;}
};
}
