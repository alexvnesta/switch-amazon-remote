// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "bond_record.hpp"
#include "bond_transaction.hpp"

namespace amazon_remote::bond {
// Private, one-enrollment bridge to native transactional savedata. No runtime
// caller yet. The trusted driver must hold one writer/owner lock for Run(),
// validate negotiated distribution/security, and bind these exact Materials
// and resolved identities to Completion.candidate before constructing this.
// This is NOT an untrusted IPC interface or an automatic provisioning path.
// Guard.Current(r,c) checks the live owned driver epoch/candidate without side
// effects; Guard.Now() is the same monotonic millisecond clock as Request.
// The controller-context/root lifetime token remains a driver integration gate.
template<class Guard, class Io=save_io::DeviceSlot> class PrivateStore {
    friend class Transaction;
    const Request request_;
    const Completion completion_;
    const record::Binding binding_;
    const record::Materials materials_;
    const std::uint32_t mask_;
    Guard &guard_; Io &io_;
    Transaction transaction_;
    record::Buffer previous_,next_;
    bool opened_{},staged_{},verified_{},committed_{};
    bool Current() {
        return guard_.Current(request_,completion_) && Eligible(request_,completion_,guard_.Now());
    }
    OpenResult Begin(const Request &r) {
        if(opened_ || r!=request_ || r.profile_version!=binding_.profile ||
           !record::Valid(binding_) || !Current()) return OpenResult::RejectedWithoutWrites;
        if(!io_.ReadCommitted(previous_.Write())) return OpenResult::Unknown;
        record::Metadata metadata;
        if(!record::Inspect(previous_.Read(),binding_,request_.minimum_key_bytes,metadata))
            return OpenResult::Unknown;
        if(metadata.generation!=r.previous_generation) return OpenResult::RejectedWithoutWrites;
        if(metadata.occupied && ((metadata.mitm && !completion_.mitm_protected) ||
            metadata.key_bytes>completion_.key_bytes)) return OpenResult::RejectedWithoutWrites;
        opened_=true; return OpenResult::Opened;
    }
    bool Stage(const Completion &c,std::uint64_t generation) {
        if(!opened_ || staged_ || c!=completion_ || generation!=request_.previous_generation+1 ||
           !Current()) return false;
        staged_=record::Build(binding_,generation,materials_,mask_,c.key_bytes,c.mitm_protected,next_);
        return staged_;
    }
    bool VerifyStaged(const Request &r,const Completion &c,std::uint64_t generation) {
        if(!staged_ || r!=request_ || c!=completion_ || !Current()) return false;
        record::Buffer rebuilt; record::Metadata metadata;
        verified_=record::Build(binding_,generation,materials_,mask_,c.key_bytes,c.mitm_protected,rebuilt) &&
            std::equal(rebuilt.Read().begin(),rebuilt.Read().end(),next_.Read().begin()) &&
            record::Inspect(next_.Read(),binding_,r.minimum_key_bytes,metadata) &&
            metadata.occupied && metadata.generation==generation && metadata.mask==mask_;
        return verified_;
    }
    CommitResult Commit() {
        if(!verified_ || committed_) return CommitResult::Unknown;
        // Stage was private RAM only. Replace performs the actual native
        // write/flush/verify/commit/reopen transaction, rechecking live context
        // immediately before writing and immediately before committing.
        const auto receipt=io_.Replace(previous_.Read(),next_.Read(),[this]{ return Current(); });
        switch(receipt.outcome) {
            case save_io::Outcome::CommittedAndReadBack:
                committed_=true; return CommitResult::Committed;
            case save_io::Outcome::Refused:
            case save_io::Outcome::PreviousRecordReadBack: return CommitResult::NotCommitted;
            case save_io::Outcome::RecoveryRequired:
            case save_io::Outcome::AlreadyConsumed: return CommitResult::Unknown;
        }
        return CommitResult::Unknown;
    }
    bool ReadEquals(record::Buffer &expected) {
        record::Buffer actual; record::Metadata metadata;
        return io_.ReadCommitted(actual.Write()) &&
            std::equal(actual.Read().begin(),actual.Read().end(),expected.Read().begin()) &&
            record::Inspect(actual.Read(),binding_,request_.minimum_key_bytes,metadata);
    }
    bool Abort() {
        // Never rewrite an old bond, delete a bond, or retry an uncertain commit.
        return opened_ && !committed_ && ReadEquals(previous_);
    }
    bool VerifyCommitted(const Request &r,const Completion &c,std::uint64_t generation) {
        return committed_ && r==request_ && c==completion_ &&
            generation==request_.previous_generation+1 && ReadEquals(next_);
    }
public:
    PrivateStore(const Request &r,const Completion &c,const record::Binding &b,
                 const record::Materials &keys,std::uint32_t expected_mask,Guard &guard,Io &io)
        : request_(r),completion_(c),binding_(b),materials_(keys),mask_(expected_mask),guard_(guard),io_(io) {}
    PrivateStore(const PrivateStore &)=delete;
    PrivateStore &operator=(const PrivateStore &)=delete;
    Receipt Run(std::uint64_t now) {
        auto result=transaction_.Run(request_,completion_,now,*this);
        previous_.Clear(); next_.Clear();
        return result;
    }
};
}
