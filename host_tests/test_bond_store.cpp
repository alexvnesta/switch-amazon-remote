// SPDX-License-Identifier: GPL-2.0-only
#define BOND_SAVE_FIXTURE_ONLY
#include "test_bond_save_io.cpp"
#include "bond_record_fixture.hpp"
#include "../mc_mitm/source/amazon_remote/bond_store.hpp"
namespace bond=amazon_remote::bond;
namespace rec=bond::record;
using BOutcome=bond::Outcome;
struct Guard {
    unsigned checks{},veto{};
    bool expired{};
    bool Current(const bond::Request &,const bond::Completion &) { return ++checks!=veto || expired; }
    std::uint64_t Now() { return expired && checks>=veto ? 500 : 150; }
};
bond::Request Request() { return {{1,2,3,4,5},6,0,100,500,1,16,true,true}; }
bond::Completion Completion() { return {Request().owner,6,7,16,true,true,true,true,true,false,false}; }
void Seed(const rec::Buffer &record) {
    Reset(); std::copy(record.Read().begin(),record.Read().end(),f.committed.begin()); f.pending=f.committed;
}
int main() {
    auto binding=bond_fixture::Binding(); bond_fixture::Keys keys(binding);
    rec::Buffer empty; assert(rec::Empty(binding,empty));
    Seed(empty); const auto before=f.committed;
    Guard guard; DeviceSlot io;
    bond::PrivateStore store(Request(),Completion(),binding,keys.pointers,rec::KnownMask,guard,io);
    auto saved=store.Run(150);
    assert(saved.outcome==BOutcome::Saved && saved.generation==1 && f.commits==1 && f.writes==1);
    Closed(); rec::Metadata metadata;
    assert(rec::Inspect(f.committed,binding,16,metadata) && metadata.occupied && metadata.generation==1);
    const auto trace=f.trace; const auto count=f.calls; const auto checks=guard.checks;
    assert(store.Run(150).outcome==BOutcome::AlreadyConsumed && f.calls==count);
    // Failure at EVERY syscall in the actual integrated successful path, not
    // only a model of the store. The mock never uses host/console storage.
    for(unsigned failure=1;failure<=count;++failure) {
        Seed(empty); f.fail_call=failure; Guard g; DeviceSlot native;
        bond::PrivateStore s(Request(),Completion(),binding,keys.pointers,rec::KnownMask,g,native);
        auto r=s.Run(150);
        assert(r.outcome!=BOutcome::Saved && !r.generation && f.writes<=1 && f.commits<=1); Closed();
        if(f.committed_once || trace[failure-1]=="commit") assert(r.outcome==BOutcome::RecoveryRequired);
        else assert(f.committed==before);
        const auto calls=f.calls; assert(s.Run(150).outcome==BOutcome::AlreadyConsumed && f.calls==calls);
    }
    auto commit=std::find(trace.begin(),trace.end(),"commit"); assert(commit!=trace.end());
    const auto commit_call=unsigned(commit-trace.begin()+1);
    for(bool persists:{false,true}) {
        Seed(empty); f.fail_call=commit_call; f.commit_failure_persists=persists; Guard g; DeviceSlot native;
        bond::PrivateStore s(Request(),Completion(),binding,keys.pointers,rec::KnownMask,g,native);
        assert(s.Run(150).outcome==BOutcome::RecoveryRequired && f.commits==1 && f.writes==1);
        // No Abort read or automatic rewrite follows an ambiguous commit.
        assert(f.calls==commit_call && (f.committed==before)==!persists); Closed();
    }
    // Live owner loss AND deadline expiry at each guard checkpoint. This
    // includes the gap after old-record read and after staged-byte verification.
    assert(checks==6);
    for(bool expiry:{false,true}) for(unsigned at=1;at<=checks;++at) {
        Seed(empty); Guard g{0,at,expiry}; DeviceSlot native;
        bond::PrivateStore s(Request(),Completion(),binding,keys.pointers,rec::KnownMask,g,native);
        const auto r=s.Run(150);
        assert(r.outcome==(at==1 ? BOutcome::StoreNotOpened : BOutcome::PreviousRecordRetained));
        assert(!r.generation && !f.commits && f.committed==before);
        assert(f.writes==(at==6 ? 1u : 0u)); Closed();
    }
    // Ineligible requests never even open the private save.
    for(unsigned mode=0;mode<5;++mode) {
        Seed(empty); auto request=Request(); auto completion=Completion();
        if(mode==0) request.previous_generation=UINT64_MAX;
        if(mode==1) completion.distribution_complete=false;
        if(mode==2) completion.debug_key=true;
        if(mode==3) ++completion.owner.connection_epoch;
        if(mode==4) completion.key_bytes=7;
        Guard g; DeviceSlot native;
        bond::PrivateStore s(request,completion,binding,keys.pointers,rec::KnownMask,g,native);
        assert(s.Run(150).outcome==BOutcome::RejectedEvidence && !f.calls); Closed();
    }
    for(unsigned mode=0;mode<7;++mode) {
        Seed(empty); auto request=Request(); auto context=binding; auto materials=keys.pointers;
        BOutcome expected=BOutcome::RecoveryRequired;
        if(mode==0) f.committed[35]^=1; // Corrupt is not fresh/empty.
        if(mode==1) f.committed={};
        if(mode==2) { request.previous_generation=1; expected=BOutcome::StoreNotOpened; }
        if(mode==3) ++context.controller_context[0];
        if(mode==4) { ++context.profile; expected=BOutcome::StoreNotOpened; }
        if(mode==5) { materials[0]=nullptr; expected=BOutcome::PreviousRecordRetained; }
        if(mode==6) { materials[1]=materials[0]; expected=BOutcome::PreviousRecordRetained; }
        Guard g; DeviceSlot native;
        bond::PrivateStore s(request,Completion(),context,materials,rec::KnownMask,g,native);
        assert(s.Run(150).outcome==expected && !f.writes && !f.commits); Closed();
    }
    // Renew the exact prior generation; no implicit loss of prior security.
    rec::Buffer existing; assert(rec::Build(binding,1,keys.pointers,rec::KnownMask,16,false,existing));
    Seed(existing); auto request=Request(); request.previous_generation=1;
    Guard renewal_guard; DeviceSlot renewal_io;
    bond::PrivateStore renewal(request,Completion(),binding,keys.pointers,rec::KnownMask,renewal_guard,renewal_io);
    assert(renewal.Run(150).outcome==BOutcome::Saved);
    assert(rec::Inspect(f.committed,binding,16,metadata) && metadata.generation==2); Closed();
    bond_fixture::Keys stronger(binding,rec::KnownMask,16,true);
    assert(rec::Build(binding,1,stronger.pointers,rec::KnownMask,16,true,existing)); Seed(existing);
    Guard weaker_guard; DeviceSlot weaker_io;
    bond::PrivateStore weaker(request,Completion(),binding,keys.pointers,rec::KnownMask,weaker_guard,weaker_io);
    assert(weaker.Run(150).outcome==BOutcome::StoreNotOpened && !f.writes); Closed();
    for(auto fault:{Fault::StagedMismatch,Fault::PostMismatch,Fault::PostOwnerChange,Fault::ClosePersistsPartial}) {
        Seed(empty); f.fault=fault; Guard g; DeviceSlot native;
        bond::PrivateStore s(Request(),Completion(),binding,keys.pointers,rec::KnownMask,g,native);
        auto expected=fault==Fault::StagedMismatch ? BOutcome::PreviousRecordRetained : BOutcome::RecoveryRequired;
        assert(s.Run(150).outcome==expected); Closed();
    }
    std::printf("PASS integrated private bond store: %u native-call failures, %u owner/deadline guards, ambiguous commit before/after durability, generation/security checks. SDK MOCK, NOT device persistence or reconnect proof.\n",count,checks*2);
}
