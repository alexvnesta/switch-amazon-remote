// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "bond_store.hpp"

namespace amazon_remote::bond::candidate {
// PRIVATE driver-side contract, not an IPC/SD format or deployed hook. No
// caller exists yet. The native adapter must independently establish the mask,
// completion, live ownership and stable local-root binding. M3 does not supply
// those proofs. Never construct this plan by OR-ing received key selectors.
struct Plan {
    Request request{};
    record::Binding binding{};
    legacy::Layout layout{legacy::Layout::Unsupported};
    std::uint32_t negotiated_mask{};
    std::uint8_t key_bytes{};
    bool mitm{},bonding_negotiated{};
    std::uint64_t candidate{}; // Trusted enrollment-local identity, never a key.
};
struct NativeCompletion {
    Owner owner{};
    std::uint64_t enrollment{};
    std::uint32_t negotiated_mask{};
    std::uint8_t key_bytes{},failure_reason{};
    bool pairing_succeeded{},bonding_negotiated{},distribution_complete{},
         link_encrypted{},debug_key{},mitm{};
};
enum class State { Unused, Collecting, Complete, Rejected, Consumed };
enum class EventResult { Ignored, Accepted, Rejected };

// One serialized enrollment, no heap, native calls, timers, logging or public
// key getter. The caller holds its driver/enrollment lock across every method,
// including Persist, and closes admission before releasing that lock. It must
// call Poll/Cancel on timeout/disconnect/driver reset, not wait for another key.
// Guard.CaptureCurrent(request,binding) checks the trusted live owner/root
// context. Guard.Current(request,completion) additionally checks candidate
// ownership at storage checkpoints. Guard.Now() uses Request's monotonic clock.
// This class does not make untrusted guard booleans into authentic evidence.
class Collector {
    Plan plan_{};
    Completion completion_{};
    std::array<legacy::Material,5> keys_{};
    State state_{State::Unused};
    std::uint32_t present_{};
    std::uint64_t last_ms_{};
    void Erase() {
        for(auto &key:keys_)key.Clear();
        present_=0; completion_={};
    }
    EventResult Reject() { Erase();state_=State::Rejected;return EventResult::Rejected; }
    template<class Guard> bool Live(Guard &guard) {
        const auto now=guard.Now();
        if(now<last_ms_ || now<plan_.request.started_ms || now>=plan_.request.deadline_ms ||
           !guard.CaptureCurrent(plan_.request,plan_.binding))return false;
        last_ms_=now;return true;
    }
    template<class Guard> struct StoreGuard {
        Collector &owner; Guard &guard;
        bool Current(const Request &r,const Completion &c) {
            // Re-read the monotonic clock AFTER the candidate ownership check.
            // A clock discontinuity during that check must not reach commit.
            return r==owner.plan_.request && c==owner.completion_ && guard.Current(r,c) && owner.Live(guard);
        }
        std::uint64_t Now() { return guard.Now(); }
    };
public:
    Collector()=default;
    ~Collector() { Erase(); }
    Collector(const Collector &)=delete;
    Collector &operator=(const Collector &)=delete;
    Collector(Collector &&)=delete;
    Collector &operator=(Collector &&)=delete;
    State GetState() const { return state_; }
    std::uint32_t RetainedMask() const { return present_; } // Metadata only.
    template<class Guard> bool Begin(const Plan &p,Guard &guard) {
        if(state_!=State::Unused)return false;
        // Reuse B1's request/security admission; no store operation is invoked.
        const Completion eligibility{p.request.owner,p.request.enrollment,p.candidate,p.key_bytes,
                                     true,p.bonding_negotiated,true,true,true,false,p.mitm};
        if(p.layout!=legacy::Layout::AuditedC91Legacy || !record::Valid(p.binding) ||
           p.binding.profile!=p.request.profile_version || !(p.negotiated_mask&1) ||
           (p.negotiated_mask&~record::KnownMask) || !Eligible(p.request,eligibility,guard.Now())) {
            Reject();return false;
        }
        plan_=p;
        if(!Live(guard)) { Reject();return false; }
        state_=State::Collecting;return true;
    }
    template<class Guard> bool Poll(Guard &guard) {
        if(state_!=State::Collecting && state_!=State::Complete)return false;
        if(!Live(guard)) { Reject();return false; }
        return true;
    }
    void Cancel() { if(state_!=State::Consumed)Reject(); }
    template<class Guard> EventResult Key(const Owner &origin,std::span<const std::uint8_t> frame,Guard &guard) {
        if(state_!=State::Collecting && state_!=State::Complete)return EventResult::Ignored;
        if(!Live(guard))return Reject();
        if(origin!=plan_.request.owner)return EventResult::Ignored;
        // Ignore other peers without touching their state. Short owned frames
        // cannot be safely classified and invalidate this candidate only.
        if(frame.size()<6)return Reject();
        if(!std::equal(plan_.binding.peer.begin(),plan_.binding.peer.end(),frame.begin()))return EventResult::Ignored;
        if(state_!=State::Collecting || frame.size()!=legacy::EventBytes)return Reject();
        const auto kind=frame[6];
        auto slot=std::find(record::Kinds.begin(),record::Kinds.end(),kind);
        if(slot==record::Kinds.end() || !(plan_.negotiated_mask&kind) || (present_&kind))return Reject();
        const auto index=std::size_t(slot-record::Kinds.begin());
        if(!legacy::Decode(plan_.layout,plan_.binding.peer,frame,keys_[index]) ||
           !record::SlotValid(keys_[index].NativeUnion(),kind,plan_.binding,plan_.key_bytes,plan_.mitm))return Reject();
        present_|=kind;return EventResult::Accepted;
    }
    template<class Guard> EventResult Complete(const NativeCompletion &e,Guard &guard) {
        if(state_!=State::Collecting && state_!=State::Complete)return EventResult::Ignored;
        if(!Live(guard))return Reject();
        if(e.owner!=plan_.request.owner || e.enrollment!=plan_.request.enrollment)return EventResult::Ignored;
        if(state_!=State::Collecting || e.failure_reason || !e.pairing_succeeded || !e.bonding_negotiated ||
           !e.distribution_complete || !e.link_encrypted || e.debug_key || e.mitm!=plan_.mitm ||
           e.key_bytes!=plan_.key_bytes || e.negotiated_mask!=plan_.negotiated_mask ||
           present_!=plan_.negotiated_mask)return Reject();
        completion_={e.owner,e.enrollment,plan_.candidate,e.key_bytes,true,true,true,true,true,false,e.mitm};
        if(!Eligible(plan_.request,completion_,last_ms_))return Reject();
        state_=State::Complete;return EventResult::Accepted;
    }
    template<class Guard,class Io> Receipt Persist(Guard &guard,Io &io) {
        if(state_==State::Consumed)return {Outcome::AlreadyConsumed,0};
        if(state_!=State::Complete || !Live(guard)) { Reject();return {}; }
        // Freeze before entering storage. No subsequent callback can modify
        // this candidate; no private material/reference escapes this method.
        state_=State::Consumed;
        record::Materials materials{};
        for(std::size_t i=0;i<keys_.size();++i)if(present_&record::Kinds[i])materials[i]=&keys_[i];
        StoreGuard<Guard> checked{*this,guard};
        PrivateStore store(plan_.request,completion_,plan_.binding,materials,plan_.negotiated_mask,checked,io);
        auto receipt=store.Run(last_ms_);Erase();return receipt;
    }
};
// Persist success is verified local record storage only. It does not prove
// native reload, peer retention, reboot reconnect, or stable-root provenance.
}
