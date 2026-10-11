// SPDX-License-Identifier: GPL-2.0-only
#define BOND_SAVE_FIXTURE_ONLY
#include "test_bond_save_io.cpp"
#include "bond_record_fixture.hpp"
#include "../mc_mitm/source/amazon_remote/bond_candidate.hpp"
#include <type_traits>
namespace b=amazon_remote::bond;
namespace c=b::candidate;
namespace rec=b::record;
using ER=c::EventResult;
using BO=b::Outcome;
using Frame=std::array<std::uint8_t,b::legacy::EventBytes>;
static_assert(!std::is_copy_constructible_v<c::Collector> && !std::is_move_constructible_v<c::Collector>);
c::Plan Plan(std::uint32_t mask=0x0b) {
    return {{{1,2,3,4,5},6,0,100,500,1,16,true,true},bond_fixture::Binding(),
            b::legacy::Layout::AuditedC91Legacy,mask,16,false,true,7};
}
c::NativeCompletion Done(const c::Plan &p) {
    return {p.request.owner,p.request.enrollment,p.negotiated_mask,p.key_bytes,0,true,true,true,true,false,p.mitm};
}
struct Guard {
    c::Plan expected;
    std::uint64_t now{150};
    unsigned captures{},checks{},capture_veto{},store_veto{};
    bool live{true},candidate_live{true};
    int clock_failure{}; // At store_veto: 1 deadline, 2 backwards clock.
    bool CaptureCurrent(const b::Request &r,const rec::Binding &binding) {
        return ++captures!=capture_veto && live && r==expected.request && binding==expected.binding;
    }
    bool Current(const b::Request &r,const b::Completion &completion) {
        ++checks;
        if(checks==store_veto && clock_failure) now=clock_failure==1 ? 500 : 149;
        return candidate_live && (checks!=store_veto || clock_failure) && r==expected.request &&
            completion.owner==r.owner && completion.enrollment==r.enrollment && completion.candidate==expected.candidate;
    }
    std::uint64_t Now() const { return now; }
};
Frame Event(const c::Plan &p,std::uint8_t kind) {
    Frame frame{};
    std::copy(p.binding.peer.begin(),p.binding.peer.end(),frame.begin()); frame[6]=kind;
    // Entirely synthetic key bytes; never a console capture or a real credential.
    for(std::size_t j=8;j<frame.size();++j) frame[j]=std::uint8_t(0xa0+j);
    const auto sec=std::uint8_t(p.mitm ? 4 : 1);
    switch(kind) {
        case 1: frame[34]=sec; frame[35]=p.key_bytes; break;
        case 2: frame[24]=p.binding.peer_type;
            std::copy(p.binding.peer.begin(),p.binding.peer.end(),frame.begin()+25); break;
        case 4: frame[28]=sec; break;
        case 8: frame[10]=p.key_bytes; frame[11]=sec; break;
        case 32: frame[12]=0xa8; frame[13]=0xa9; frame[14]=sec; break;
    }
    return frame;
}
bool ContainsSyntheticKey(const c::Collector &collector) {
    const auto pattern=Event(Plan(),1);
    const auto *first=reinterpret_cast<const unsigned char *>(&collector);
    // Inspect object representation only in this test. No production key getter.
    return std::search(first,first+sizeof(collector),pattern.begin()+8,pattern.begin()+24)!=first+sizeof(collector);
}
void Wiped(const c::Collector &collector) {
    assert(!collector.RetainedMask() && !ContainsSyntheticKey(collector));
}
void Seed(const c::Plan &p) {
    rec::Buffer empty; assert(rec::Empty(p.binding,empty)); Reset();
    std::copy(empty.Read().begin(),empty.Read().end(),f.committed.begin()); f.pending=f.committed;
}
void Ready(c::Collector &collector,const c::Plan &p,Guard &g) {
    assert(collector.Begin(p,g));
    for(auto kind:rec::Kinds) if(p.negotiated_mask&kind)
        assert(collector.Key(p.request.owner,Event(p,kind),g)==ER::Accepted);
    assert(collector.Complete(Done(p),g)==ER::Accepted);
    assert(collector.GetState()==c::State::Complete && ContainsSyntheticKey(collector));
}
void RejectPlan(c::Plan p) {
    Reset(); Guard g{p}; c::Collector collector;
    assert(!collector.Begin(p,g) && collector.GetState()==c::State::Rejected);
    assert(!collector.Begin(Plan(),g) && !f.calls); Wiped(collector);
}
int main() {
    auto p=Plan(); Seed(p); const auto empty=f.committed;
    Guard guard{p}; c::Collector collector; DeviceSlot io;
    // M3 observed order/scalars, synthetic material. M3 alone does not prove
    // negotiated distribution or stable root provenance supplied by this mock.
    assert(collector.Begin(p,guard) && !collector.Begin(p,guard));
    assert(collector.Key(p.request.owner,Event(p,1),guard)==ER::Accepted);
    assert(ContainsSyntheticKey(collector));
    assert(collector.Key(p.request.owner,Event(p,2),guard)==ER::Accepted);
    assert(collector.Key(p.request.owner,Event(p,8),guard)==ER::Accepted && !f.calls);
    assert(collector.Complete(Done(p),guard)==ER::Accepted && !f.calls);
    auto result=collector.Persist(guard,io);
    assert(result.outcome==BO::Saved && result.generation==1 && f.writes==1 && f.commits==1);
    Wiped(collector); Closed();
    const auto trace=f.trace; const auto call_count=f.calls; const auto store_checks=guard.checks;
    const auto capture_checks=guard.captures;
    assert(collector.Persist(guard,io).outcome==BO::AlreadyConsumed && f.calls==call_count);
    assert(collector.Key(p.request.owner,Event(p,1),guard)==ER::Ignored);
    rec::Metadata metadata; assert(rec::Inspect(f.committed,p.binding,16,metadata));
    assert(metadata.occupied && metadata.mask==p.negotiated_mask && metadata.generation==1);

    // All16 legal distribution masks, all261 key-event permutations. Optional
    // signing keys and MITM are supported as contracts, not device claims.
    unsigned permutations{};
    for(unsigned subset=0;subset<16;++subset) {
        auto plan=Plan(1);
        for(unsigned i=0;i<4;++i) if(subset&(1u<<i)) plan.negotiated_mask|=rec::Kinds[i+1];
        std::vector<std::uint8_t> order;
        for(auto kind:rec::Kinds) if(plan.negotiated_mask&kind) order.push_back(kind);
        do {
            ++permutations;
            for(bool mitm:{false,true}) {
                plan.mitm=mitm; Seed(plan); Guard g{plan}; c::Collector candidate; DeviceSlot native;
                assert(candidate.Begin(plan,g));
                for(auto kind:order) assert(candidate.Key(plan.request.owner,Event(plan,kind),g)==ER::Accepted);
                assert(candidate.Complete(Done(plan),g)==ER::Accepted && !f.calls);
                assert(candidate.Persist(g,native).outcome==BO::Saved); Wiped(candidate); Closed();
                assert(rec::Inspect(f.committed,plan.binding,16,metadata) && metadata.mask==plan.negotiated_mask && metadata.mitm==mitm);
            }
        } while(std::next_permutation(order.begin(),order.end()));
    }
    assert(permutations==261);
    // Individually well-framed records may still contradict a shared native
    // field. Reject the candidate at storage staging without writing anything.
    for(unsigned byte=0;byte<2;++byte) {
        const auto plan=Plan(rec::KnownMask); Seed(plan);
        Guard g{plan}; c::Collector candidate; DeviceSlot native;
        assert(candidate.Begin(plan,g));
        for(auto kind:rec::Kinds) {
            auto event=Event(plan,kind);
            if(kind==32)event[12+byte]^=1;
            assert(candidate.Key(plan.request.owner,event,g)==ER::Accepted);
        }
        assert(candidate.Complete(Done(plan),g)==ER::Accepted);
        assert(candidate.Persist(g,native).outcome==BO::PreviousRecordRetained && !f.writes && !f.commits);
        Wiped(candidate); Closed();
    }
    for(std::uint8_t length=7;length<=16;++length) {
        auto plan=Plan(); plan.key_bytes=length; plan.request.minimum_key_bytes=length;
        Seed(plan); Guard g{plan}; c::Collector candidate; DeviceSlot native; Ready(candidate,plan,g);
        assert(candidate.Persist(g,native).outcome==BO::Saved &&
               rec::Inspect(f.committed,plan.binding,length,metadata) && metadata.key_bytes==length);
        Closed(); Wiped(candidate);
    }
    for(unsigned mode=0;mode<26;++mode) {
        auto plan=Plan();
        switch(mode) {
            case 0: plan.layout=b::legacy::Layout::Unsupported; break;
            case 1: plan.binding.controller_context={}; break;
            case 2: plan.binding.local={}; break;
            case 3: plan.binding.peer={}; break;
            case 4: plan.binding.peer_type=2; break;
            case 5: ++plan.binding.profile; break;
            case 6: plan.negotiated_mask=0; break;
            case 7: plan.negotiated_mask=2; break;
            case 8: plan.negotiated_mask|=16; break;
            case 9: plan.key_bytes=15; break;
            case 10: plan.key_bytes=17; break;
            case 11: plan.bonding_negotiated=false; break;
            case 12: plan.candidate=0; break;
            case 13: plan.request.owner.driver_instance=0; break;
            case 14: plan.request.owner.handle=UINT32_MAX; break;
            case 15: plan.request.enrollment=0; break;
            case 16: plan.request.started_ms=0; break;
            case 17: plan.request.started_ms=151; break;
            case 18: plan.request.deadline_ms=150; break;
            case 19: plan.request.user_approved=false; break;
            case 20: plan.request.fingerprint_validated=false; break;
            case 21: plan.request.previous_generation=UINT64_MAX; break;
            case 22: plan.request.minimum_key_bytes=6; break;
            case 23: plan.request.minimum_key_bytes=17; break;
            case 24: plan.request.profile_version=0; break;
            case 25: plan.binding.local_type=2; break;
        }
        RejectPlan(plan);
    }
    // Foreign owner/epoch/peer/enrollment events do not contaminate a candidate.
    for(unsigned mode=0;mode<8;++mode) {
        Seed(p); Guard g{p}; c::Collector candidate; assert(candidate.Begin(p,g));
        auto owner=p.request.owner; auto frame=Event(p,1); auto done=Done(p);
        switch(mode) {
            case 0: ++owner.driver_instance; break;
            case 1: ++owner.connection_epoch; break;
            case 2: ++owner.handle; break;
            case 3: ++owner.local_identity; break;
            case 4: ++owner.peer_identity; break;
            case 5: ++frame[0]; break;
            case 6: ++done.enrollment; break;
            case 7: ++done.owner.connection_epoch; break;
        }
        assert((mode<6 ? candidate.Key(owner,frame,g) : candidate.Complete(done,g))==ER::Ignored);
        assert(candidate.GetState()==c::State::Collecting && !candidate.RetainedMask() && !f.calls);
        candidate.Cancel(); Wiped(candidate);
    }
    // Every malformed frame length through two full payloads is bounded.
    for(unsigned length=0;length<=72;++length) if(length!=36) {
        Reset(); Guard g{p}; c::Collector candidate; assert(candidate.Begin(p,g));
        std::array<std::uint8_t,72> frame{}; const auto valid=Event(p,1);
        std::copy(valid.begin(),valid.end(),frame.begin());
        assert(candidate.Key(p.request.owner,{frame.data(),length},g)==ER::Rejected);
        assert(!f.calls); Wiped(candidate);
    }
    for(unsigned mode=0;mode<9;++mode) {
        Reset(); Guard g{p}; c::Collector candidate; assert(candidate.Begin(p,g));
        auto frame=Event(p,1);
        switch(mode) {
            case 0: frame[6]=0; break;
            case 1: frame[6]=3; break;
            case 2: frame[6]=16; break;
            case 3: frame=Event(p,4); break; // Not negotiated.
            case 4: frame[34]=4; break;
            case 5: frame[35]=15; break;
            case 6: frame=Event(p,2); ++frame[25]; break;
            case 7: frame=Event(p,2); ++frame[24]; break;
            case 8: assert(candidate.Key(p.request.owner,frame,g)==ER::Accepted); break;
        }
        assert(candidate.Key(p.request.owner,frame,g)==ER::Rejected && !f.calls); Wiped(candidate);
    }
    for(unsigned mode=0;mode<12;++mode) {
        Seed(p); Guard g{p}; c::Collector candidate; assert(candidate.Begin(p,g));
        assert(candidate.Key(p.request.owner,Event(p,1),g)==ER::Accepted);
        if(mode!=0) for(auto kind:{2,8}) assert(candidate.Key(p.request.owner,Event(p,kind),g)==ER::Accepted);
        auto done=Done(p);
        switch(mode) {
            case 1: done.failure_reason=1; break;
            case 2: done.pairing_succeeded=false; break;
            case 3: done.bonding_negotiated=false; break;
            case 4: done.distribution_complete=false; break;
            case 5: done.link_encrypted=false; break;
            case 6: done.debug_key=true; break;
            case 7: done.mitm=true; break;
            case 8: --done.key_bytes; break;
            case 9: done.negotiated_mask=1; break;
            case 10: assert(candidate.Complete(done,g)==ER::Accepted); break;
            case 11: assert(candidate.Complete(done,g)==ER::Accepted);
                assert(candidate.Key(p.request.owner,Event(p,8),g)==ER::Rejected); break;
        }
        assert(candidate.Complete(done,g)==(mode==11 ? ER::Ignored : ER::Rejected));
        DeviceSlot native; assert(candidate.Persist(g,native).outcome==BO::RejectedEvidence && !f.calls);
        Wiped(candidate); Closed();
    }
    // Explicit polling/cancellation, not waiting forever for a missing callback.
    for(unsigned point=0;point<4;++point) for(unsigned loss=0;loss<4;++loss) {
        Seed(p); Guard g{p}; c::Collector candidate; Ready(candidate,p,g);
        if(loss==0) g.now=500;
        if(loss==1) g.now=149;
        if(loss==2) g.live=false;
        if(loss==3) ++g.expected.binding.controller_context[0];
        DeviceSlot native;
        if(point==0) assert(!candidate.Poll(g));
        if(point==1) assert(candidate.Key(p.request.owner,Event(p,1),g)==ER::Rejected);
        if(point==2) assert(candidate.Complete(Done(p),g)==ER::Rejected);
        if(point==3) assert(candidate.Persist(g,native).outcome==BO::RejectedEvidence);
        assert(!f.calls); Wiped(candidate);
    }
    for(bool complete:{false,true}) {
        Seed(p); Guard g{p}; c::Collector candidate;
        if(complete) Ready(candidate,p,g);
        else { assert(candidate.Begin(p,g)); assert(candidate.Key(p.request.owner,Event(p,1),g)==ER::Accepted); }
        candidate.Cancel(); candidate.Cancel(); Wiped(candidate);
        DeviceSlot native; assert(candidate.Persist(g,native).outcome==BO::RejectedEvidence && !f.calls);
    }
    // Revoked context is checked at admission, each callback and every storage
    // boundary, not just once before collecting. Test the real call sequence.
    for(unsigned checkpoint=1;checkpoint<=capture_checks;++checkpoint) {
        Seed(p); Guard g{p}; g.capture_veto=checkpoint; c::Collector candidate; DeviceSlot native;
        candidate.Begin(p,g);
        for(auto kind:{1,2,8}) candidate.Key(p.request.owner,Event(p,kind),g);
        candidate.Complete(Done(p),g);
        auto receipt=candidate.Persist(g,native);
        assert(receipt.outcome!=BO::Saved && !receipt.generation && !f.commits && f.committed==empty);
        Wiped(candidate); Closed();
    }
    // Premature Persist is terminal and wipes even a valid partial candidate.
    for(unsigned count=0;count<4;++count) {
        Seed(p); Guard g{p}; c::Collector candidate; DeviceSlot native;
        if(count) assert(candidate.Begin(p,g));
        if(count>1) assert(candidate.Key(p.request.owner,Event(p,1),g)==ER::Accepted);
        if(count>2) for(auto kind:{2,8}) assert(candidate.Key(p.request.owner,Event(p,kind),g)==ER::Accepted);
        assert(candidate.Persist(g,native).outcome==BO::RejectedEvidence && !f.calls);
        assert(!candidate.Begin(p,g)); Wiped(candidate);
    }
    // Exercise the actual SDK-backed private store, including commit ambiguity.
    for(unsigned failure=1;failure<=call_count;++failure) {
        Seed(p); f.fail_call=failure; Guard g{p}; c::Collector candidate; DeviceSlot native; Ready(candidate,p,g);
        const auto receipt=candidate.Persist(g,native);
        assert(receipt.outcome!=BO::Saved && !receipt.generation && f.writes<=1 && f.commits<=1);
        if(f.committed_once || trace[failure-1]=="commit") assert(receipt.outcome==BO::RecoveryRequired);
        else assert(f.committed==empty);
        const auto calls=f.calls; assert(candidate.Persist(g,native).outcome==BO::AlreadyConsumed && f.calls==calls);
        Wiped(candidate); Closed();
    }
    const auto commit=std::find(trace.begin(),trace.end(),"commit"); assert(commit!=trace.end());
    const auto commit_call=unsigned(commit-trace.begin()+1);
    for(bool persists:{false,true}) {
        Seed(p); f.fail_call=commit_call; f.commit_failure_persists=persists;
        Guard g{p}; c::Collector candidate; DeviceSlot native; Ready(candidate,p,g);
        assert(candidate.Persist(g,native).outcome==BO::RecoveryRequired);
        assert(f.calls==commit_call && (f.committed==empty)==!persists); Wiped(candidate); Closed();
    }
    assert(store_checks==6);
    for(unsigned loss=0;loss<3;++loss) for(unsigned checkpoint=1;checkpoint<=store_checks;++checkpoint) {
        Seed(p); Guard g{p}; c::Collector candidate; DeviceSlot native; Ready(candidate,p,g);
        g.store_veto=checkpoint; g.clock_failure=int(loss);
        const auto receipt=candidate.Persist(g,native);
        assert(receipt.outcome!=BO::Saved && !receipt.generation && !f.commits && f.committed==empty);
        Wiped(candidate); Closed();
    }
    // Reject implicit key-length/MITM downgrade or stale generation.
    for(unsigned mode=0;mode<3;++mode) {
        auto plan=Plan(); plan.request.previous_generation=1;
        bond_fixture::Keys keys(plan.binding,plan.negotiated_mask,16,mode==0);
        rec::Buffer previous; assert(rec::Build(plan.binding,1,keys.pointers,plan.negotiated_mask,16,mode==0,previous));
        if(mode==1) { plan.key_bytes=15; plan.request.minimum_key_bytes=15; }
        if(mode==2) plan.request.previous_generation=0;
        Seed(plan); std::copy(previous.Read().begin(),previous.Read().end(),f.committed.begin()); f.pending=f.committed;
        Guard g{plan}; c::Collector candidate; DeviceSlot native; Ready(candidate,plan,g);
        assert(candidate.Persist(g,native).outcome==BO::StoreNotOpened && !f.writes && !f.commits);
        Wiped(candidate); Closed();
    }
    std::printf("PASS private candidate: %u distribution permutations x2 security modes, 10 key lengths, malformed/stale/duplicate/incomplete events, %u SDK faults, %u storage ownership/clock vetoes, %u capture-context vetoes, wipe/no-retry. SYNTHETIC ONLY, not deployed pairing or reconnect proof.\n",permutations,call_count,store_checks*3,capture_checks);
}
