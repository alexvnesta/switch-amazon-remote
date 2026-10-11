// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "bond_record.hpp"

namespace amazon_remote::bond::reload {
// PRIVATE per-key bridge. No production caller or native sender is installed.
// The audited bulk BSA add-device consumer submits ALL five key types once its
// single BLE-key-present flag is set, regardless of the negotiated key set.
// Do not pass a partially populated record to that bulk helper. Its reused
// scratch union also retains bytes from earlier slots. This bridge instead
// supplies only present kinds, each with its own fully initialized 28 bytes.
enum class State { Unused, Prepared, Rejected, Consumed };
enum class Outcome { Rejected, Interrupted, SubmissionUncertain, Submitted, AlreadyConsumed };
struct Receipt {
    Outcome outcome{Outcome::Rejected};
    std::uint32_t submitted_mask{}; // Not native acceptance or load verification.
};

// One immutable snapshot from a private committed record. The caller provides
// independently trusted binding/generation and a native compatibility verdict,
// not values copied from SD or from this same untrusted record. All calls must
// be serialized under its owner/writer lock. No storage or radio calls occur.
// Guard.Current(binding,generation) must recheck the live driver epoch, stable
// root context, current generation and target-only native entry ownership.
// Sink.Submit(peer,kind,union) must synchronously COPY the exact 28-byte union
// into a private, ordered, target-bound adapter, never retain this span or log
// it. There is no generic public IPC key-import endpoint in this component.
class SelectiveKeys {
    record::Buffer record_;
    record::Binding binding_{};
    record::Metadata metadata_{};
    State state_{State::Unused};
    void Clear() { record_.Clear();metadata_={}; }
    bool Reject() { Clear();state_=State::Rejected;return false; }
public:
    SelectiveKeys()=default;
    ~SelectiveKeys() { Clear(); }
    SelectiveKeys(const SelectiveKeys &)=delete;
    SelectiveKeys &operator=(const SelectiveKeys &)=delete;
    SelectiveKeys(SelectiveKeys &&)=delete;
    SelectiveKeys &operator=(SelectiveKeys &&)=delete;
    State GetState() const { return state_; }
    bool Prepare(std::span<const std::uint8_t> committed,const record::Binding &binding,
                 std::uint64_t generation,std::uint8_t minimum_key_bytes,legacy::Layout layout) {
        if(state_!=State::Unused)return false;
        if(layout!=legacy::Layout::AuditedC91Legacy || committed.size()!=save_io::RecordBytes ||
           !generation)return Reject();
        // Validate the private snapshot, not a caller-owned buffer subsequently
        // used without another check. The caller's writer lock covers this copy.
        std::copy(committed.begin(),committed.end(),record_.Write().begin());
        if(!record::Inspect(record_.Read(),binding,minimum_key_bytes,metadata_) ||
           !metadata_.occupied || metadata_.generation!=generation)return Reject();
        binding_=binding;state_=State::Prepared;return true;
    }
    void Cancel() { if(state_!=State::Consumed)Reject(); }
    template<class Guard,class Sink> Receipt Submit(Guard &guard,Sink &sink) {
        if(state_==State::Consumed)return {Outcome::AlreadyConsumed,0};
        if(state_!=State::Prepared) { Reject();return {}; }
        state_=State::Consumed;
        // Always clear the immutable private snapshot, including early returns.
        struct Wipe { SelectiveKeys &owner; ~Wipe() { owner.Clear(); } } wipe{*this};
        Receipt receipt{Outcome::Interrupted,0};
        for(std::size_t i=0;i<record::Kinds.size();++i) {
            const auto kind=record::Kinds[i];
            if(!(metadata_.mask&kind))continue;
            if(!guard.Current(binding_,metadata_.generation))return receipt;
            const auto p=record::SlotsOffset+i*record::SlotBytes+4;
            const std::span<const std::uint8_t,legacy::UnionBytes> key(record_.Read().data()+p,legacy::UnionBytes);
            if(!sink.Submit(binding_.peer,kind,key)) {
                // A sender error can occur after copying/queueing. Do not retry,
                // delete, roll back or mark the rejected item as submitted.
                receipt.outcome=Outcome::SubmissionUncertain;return receipt;
            }
            receipt.submitted_mask|=kind;
        }
        if(!guard.Current(binding_,metadata_.generation))return receipt;
        receipt.outcome=Outcome::Submitted;return receipt;
    }
};
// The audited native per-key wrapper queues work and can silently fail its
// allocation. Submitted is deliberately NOT Loaded or ReadyToConnect. A future
// native adapter needs a private completion/readback barrier before publishing
// loaded state or reconnecting. Partial/uncertain submission requires explicit
// recovery; it never permits automatic pairing, deletion or replay here.
}
