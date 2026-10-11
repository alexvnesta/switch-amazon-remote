// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/bond_transaction.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace amazon_remote::bond;

namespace {
// Synthetic references only. This is an in-memory transactional store model,
// NOT a Switch filesystem, power-loss test, private key store or driver adapter.
Request RequestForTest() {
    return {{11,12,13,14,4},15,3,1000,2000,1,16,true,true};
}
Completion CompleteForTest() {
    return {RequestForTest().owner,15,16,16,true,true,true,true,true,false,false};
}
enum Fault { None, OpenRejected, OpenUnknown, StageFailure, VerifyFailure,
             CommitNoWrite, CommitUnknownOld, CommitUnknownNew, ReadbackFailure, AbortFailure,
             OwnerChanged };
enum Step { Begin, Stage, VerifyStaged, Commit, VerifyCommitted, Abort };
struct Store {
    Fault fault{};
    std::uint64_t durable{3}, pending{}, durable_candidate{99}, candidate{};
    std::vector<Step> calls;
    explicit Store(Fault f=None):fault(f) {}
    OpenResult Begin(const Request &r) {
        calls.push_back(::Begin);
        if (fault==OpenRejected || r.previous_generation!=durable ||
            r.owner.local_identity!=13 || r.owner.peer_identity!=14 || r.profile_version!=1)
            return OpenResult::RejectedWithoutWrites;
        if (fault==OpenUnknown) return OpenResult::Unknown;
        return OpenResult::Opened;
    }
    bool Stage(const Completion &c,std::uint64_t generation) {
        calls.push_back(::Stage);pending=generation;candidate=c.candidate;
        return fault!=StageFailure && fault!=AbortFailure && c.candidate==16;
    }
    bool VerifyStaged(const Request &r,const Completion &c,std::uint64_t generation) {
        calls.push_back(::VerifyStaged);
        return fault!=VerifyFailure && fault!=OwnerChanged && pending==generation && generation==r.previous_generation+1 && candidate==c.candidate;
    }
    CommitResult Commit() {
        calls.push_back(::Commit);
        if (fault==CommitNoWrite) return CommitResult::NotCommitted;
        if (fault==CommitUnknownOld) return CommitResult::Unknown;
        durable=pending;durable_candidate=candidate;
        if (fault==CommitUnknownNew) return CommitResult::Unknown;
        return CommitResult::Committed;
    }
    bool VerifyCommitted(const Request &r,const Completion &c,std::uint64_t generation) {
        calls.push_back(::VerifyCommitted);
        return fault!=ReadbackFailure && durable==generation && generation==r.previous_generation+1 && durable_candidate==c.candidate;
    }
    bool Abort() {
        calls.push_back(::Abort);
        if (fault==AbortFailure) return false;
        pending=candidate=0;return true;
    }
};
void Refuse(Request r,Completion c,std::uint64_t now=1500) {
    Store store;Transaction tx;
    assert(!Eligible(r,c,now));
    auto result=tx.Run(r,c,now,store);
    assert(result.outcome==Outcome::RejectedEvidence && !result.generation);
    assert(store.calls.empty() && store.durable==3 && store.durable_candidate==99);
    assert(tx.Run(RequestForTest(),CompleteForTest(),1500,store).outcome==Outcome::AlreadyConsumed);
    assert(store.calls.empty());
}
}
int main() {
    const auto original=RequestForTest();const auto complete=CompleteForTest();
    assert(Eligible(original,complete,1000) && Eligible(original,complete,1999));
    Refuse(original,complete,999);Refuse(original,complete,2000);
    for (unsigned i=0;i<15;++i) {
        auto r=original;
        switch(i) {
            case 0:r.user_approved=false;break;case 1:r.fingerprint_validated=false;break;
            case 2:r.owner.driver_instance=0;break;case 3:r.owner.connection_epoch=0;break;
            case 4:r.owner.local_identity=0;break;case 5:r.owner.peer_identity=0;break;
            case 6:r.owner.handle=0;break;case 7:r.owner.handle=0xffffffff;break;
            case 8:r.enrollment=0;break;case 9:r.started_ms=0;break;
            case 10:r.deadline_ms=r.started_ms;break;case 11:r.profile_version=0;break;
            case 12:r.minimum_key_bytes=6;break;case 13:r.minimum_key_bytes=17;break;
            case 14:r.previous_generation=std::numeric_limits<std::uint64_t>::max();break;
        }
        Refuse(r,complete);
    }
    for (unsigned i=0;i<16;++i) {
        auto c=complete;
        switch(i) {
            case 0:++c.owner.driver_instance;break;case 1:++c.owner.connection_epoch;break;
            case 2:++c.owner.local_identity;break;case 3:++c.owner.peer_identity;break;
            case 4:++c.owner.handle;break;case 5:++c.enrollment;break;case 6:c.candidate=0;break;
            case 7:c.pairing_succeeded=false;break;case 8:c.bonding_negotiated=false;break;
            case 9:c.distribution_complete=false;break;case 10:c.reusable_encryption_key=false;break;
            case 11:c.link_encrypted=false;break;case 12:c.debug_key=true;break;
            case 13:c.key_bytes=15;break;case 14:c.key_bytes=17;break;case 15:c.key_bytes=0;break;
        }
        Refuse(original,c);
    }
    for(unsigned key_size=7;key_size<=16;++key_size) {
        auto r=original;auto c=complete;r.minimum_key_bytes=c.key_bytes=key_size;
        assert(Eligible(r,c,1500)); // A lower policy must be explicit, never automatic.
    }
    for(bool mitm:{false,true}) {
        auto c=complete;c.mitm_protected=mitm;Store store;Transaction tx;
        const auto result=tx.Run(original,c,1500,store);
        assert(result.outcome==Outcome::Saved && result.generation==4);
        assert(store.durable==4 && store.durable_candidate==16);
        assert((store.calls==std::vector<Step>{Begin,Stage,VerifyStaged,Commit,VerifyCommitted}));
        assert(tx.Run(original,c,1500,store).outcome==Outcome::AlreadyConsumed);
        Transaction replay;
        assert(replay.Run(original,c,1500,store).outcome==Outcome::StoreNotOpened);
        assert(store.durable==4); // Persisted generation protects across new tx object.
    }
    { // First enrollment, with no previous bond, is also supported.
        auto r=original;r.previous_generation=0;Store store;store.durable=store.durable_candidate=0;
        Transaction tx;const auto result=tx.Run(r,complete,1500,store);
        assert(result.outcome==Outcome::Saved && result.generation==1 && store.durable==1);
    }
    for(unsigned i=0;i<3;++i) { // The store independently binds persistent identity/profile.
        auto r=original;auto c=complete;
        if(i==0) ++r.owner.local_identity;
        if(i==1) ++r.owner.peer_identity;
        if(i==2) ++r.profile_version;
        c.owner=r.owner;assert(Eligible(r,c,1500));Store store;Transaction tx;
        assert(tx.Run(r,c,1500,store).outcome==Outcome::StoreNotOpened);
        assert(store.durable==3 && store.calls.size()==1);
    }
    { // Nonzero opaque token alone is not a valid private candidate.
        auto c=complete;++c.candidate;Store store;Transaction tx;
        assert(tx.Run(original,c,1500,store).outcome==Outcome::PreviousRecordRetained);
        assert(store.durable==3 && store.durable_candidate==99);
    }
    for(Fault f:{OpenRejected,OpenUnknown,StageFailure,VerifyFailure,CommitNoWrite,
                 CommitUnknownOld,CommitUnknownNew,ReadbackFailure,AbortFailure,OwnerChanged}) {
        Store store(f);Transaction tx;auto result=tx.Run(original,complete,1500,store);
        const auto expected=f==OpenRejected ? Outcome::StoreNotOpened :
            f==StageFailure || f==VerifyFailure || f==CommitNoWrite || f==OwnerChanged ? Outcome::PreviousRecordRetained : Outcome::RecoveryRequired;
        assert(result.outcome==expected && !result.generation);
        assert(store.durable==(f==CommitUnknownNew || f==ReadbackFailure ? 4 : 3));
        assert(store.durable_candidate==(store.durable==4 ? 16 : 99));
        const bool aborted=f==StageFailure || f==VerifyFailure || f==CommitNoWrite || f==AbortFailure || f==OwnerChanged;
        assert((store.calls.back()==Abort)==aborted);
        const auto count=store.calls.size();
        assert(tx.Run(original,complete,1500,store).outcome==Outcome::AlreadyConsumed);
        assert(store.calls.size()==count);
    }
    std::puts("PASS bond transaction: exact owner/attempt, negotiated complete security, explicit key policy, generation replay, transaction failure/ambiguous commit and readback rules. SYNTHETIC STORE ONLY; no driver or durability proof.");
}
