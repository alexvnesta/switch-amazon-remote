// SPDX-License-Identifier: GPL-2.0-only
#include "bond_record_fixture.hpp"
#include "../mc_mitm/source/amazon_remote/legacy_bond_reload.hpp"
#include <cstdio>
#include <string_view>
#include <type_traits>
namespace b=amazon_remote::bond;
namespace r=b::record;
namespace l=b::reload;
using ReloadResult=l::Outcome;
constexpr auto Layout=b::legacy::Layout::AuditedC91Legacy;
static_assert(!std::is_copy_constructible_v<l::SelectiveKeys> && !std::is_move_constructible_v<l::SelectiveKeys>);
struct Guard {
    r::Binding binding{bond_fixture::Binding()};
    std::uint64_t generation{1};
    unsigned calls{},veto{};
    bool Current(const r::Binding &actual,std::uint64_t gen) {
        return ++calls!=veto && actual==binding && gen==generation;
    }
};
struct Sink {
    r::Binding binding{bond_fixture::Binding()};
    std::array<std::uint8_t,5> order{};
    std::array<std::array<std::uint8_t,28>,5> unions{};
    unsigned calls{},failure{};
    bool error_after_copy{};
    bool Submit(const b::legacy::Address &peer,std::uint8_t kind,std::span<const std::uint8_t,28> key) {
        assert(peer==binding.peer && calls<5);
        const auto index=calls++;
        if(calls==failure && !error_after_copy)return false;
        order[index]=kind; std::copy(key.begin(),key.end(),unions[index].begin());
        return calls!=failure;
    }
};
bool HasSyntheticKey(const l::SelectiveKeys &keys) {
    std::array<std::uint8_t,16> pattern{};
    for(unsigned i=0;i<pattern.size();++i)pattern[i]=std::uint8_t(i+8);
    const auto *first=reinterpret_cast<const unsigned char *>(&keys);
    return std::search(first,first+sizeof(keys),pattern.begin(),pattern.end())!=first+sizeof(keys);
}
void Saved(std::uint32_t mask,r::Buffer &out,std::uint8_t bytes=16,bool mitm=false) {
    const auto binding=bond_fixture::Binding(); bond_fixture::Keys keys(binding,mask,bytes,mitm);
    assert(r::Build(binding,1,keys.pointers,mask,bytes,mitm,out));
}
void Bad(std::span<const std::uint8_t> record,const r::Binding &binding,std::uint64_t gen=1,
         std::uint8_t minimum=16,b::legacy::Layout layout=Layout) {
    l::SelectiveKeys keys; Guard g; Sink s;
    assert(!keys.Prepare(record,binding,gen,minimum,layout));
    assert(keys.GetState()==l::State::Rejected && !HasSyntheticKey(keys));
    assert(keys.Submit(g,s).outcome==ReloadResult::Rejected && !g.calls && !s.calls);
    assert(!keys.Prepare(record,binding,gen,minimum,layout));
}
int main(int argc,char **argv) {
    const bool emit=argc==2 && std::string_view(argv[1])=="--emit-synthetic";
    assert(argc==1 || emit);
    const auto binding=bond_fixture::Binding();
    unsigned combinations{};
    for(unsigned subset=0;subset<16;++subset) for(bool mitm:{false,true})
    for(std::uint8_t bytes=7;bytes<=16;++bytes) {
        std::uint32_t mask=1;
        for(unsigned i=0;i<4;++i)if(subset&(1u<<i))mask|=r::Kinds[i+1];
        r::Buffer record; Saved(mask,record,bytes,mitm);
        l::SelectiveKeys keys; Guard g; Sink s;
        assert(keys.Prepare(record.Read(),binding,1,bytes,Layout) && HasSyntheticKey(keys));
        assert(!keys.Prepare(record.Read(),binding,1,bytes,Layout));
        // Caller's original record is not the later native source.
        record.Clear();
        auto receipt=keys.Submit(g,s);
        assert(receipt.outcome==ReloadResult::Submitted && receipt.submitted_mask==mask && !HasSyntheticKey(keys));
        bond_fixture::Keys expected(binding,mask,bytes,mitm);
        unsigned at{};
        for(unsigned i=0;i<r::Kinds.size();++i) if(mask&r::Kinds[i]) {
            assert(s.order[at]==r::Kinds[i]);
            assert(std::equal(s.unions[at].begin(),s.unions[at].end(),expected.pointers[i]->NativeUnion().begin()));
            ++at;
        }
        assert(s.calls==at && g.calls==at+1);
        if(emit) {
            // Test fixture export only. This program has no file/device input;
            // every byte comes from bond_record_fixture.hpp's synthetic Keys.
            std::printf("{\"fixture\":\"synthetic-reload-v2\",\"mask\":%u,\"bytes\":%u,\"mitm\":%s,\"keys\":[",mask,bytes,mitm ? "true" : "false");
            for(unsigned index=0;index<s.calls;++index) {
                std::printf("%s{\"kind\":%u,\"union\":\"",index ? "," : "",s.order[index]);
                for(auto byte:s.unions[index])std::printf("%02x",byte);
                std::printf("\"}");
            }
            std::puts("]}");
        }
        keys.Cancel();
        assert(keys.Submit(g,s).outcome==ReloadResult::AlreadyConsumed && s.calls==at);
        ++combinations;
    }
    assert(combinations==320);
    if(emit)return 0;
    r::Buffer record; Saved(0x0b,record);
    // Actual observed selector set, SYNTHETIC material. Missing signing keys
    // must never become zero-valued native load requests.
    {
        l::SelectiveKeys keys; Guard g; Sink s;
        assert(keys.Prepare(record.Read(),binding,1,16,Layout));
        assert(keys.Submit(g,s).outcome==ReloadResult::Submitted && s.calls==3);
        assert((s.order==std::array<std::uint8_t,5>{1,2,8,0,0}));
    }
    for(unsigned pos=0;pos<record.Read().size();++pos) {
        r::Buffer damaged; std::copy(record.Read().begin(),record.Read().end(),damaged.Write().begin());
        damaged.Write()[pos]^=1; Bad(damaged.Read(),binding);
    }
    for(unsigned length=0;length<512;++length) Bad(record.Read().first(length),binding);
    std::array<std::uint8_t,513> too_long{}; std::copy(record.Read().begin(),record.Read().end(),too_long.begin());
    Bad(too_long,binding);
    for(unsigned mode=0;mode<11;++mode) {
        auto context=binding; auto generation=std::uint64_t(1); auto minimum=std::uint8_t(16); auto layout=Layout;
        switch(mode) {
            case 0: ++context.controller_context[0]; break;
            case 1: ++context.local[0]; break;
            case 2: ++context.peer[0]; break;
            case 3: ++context.local_type; break;
            case 4: ++context.peer_type; break;
            case 5: ++context.profile; break;
            case 6: generation=0; break;
            case 7: generation=2; break;
            case 8: minimum=6; break;
            case 9: minimum=17; break;
            case 10: layout=b::legacy::Layout::Unsupported; break;
        }
        Bad(record.Read(),context,generation,minimum,layout);
    }
    r::Buffer empty; assert(r::Empty(binding,empty)); Bad(empty.Read(),binding);
    // Rehashed cross-slot conflict must stop before any native submission.
    for(unsigned offset:{212u,213u,248u,249u}) {
        r::Buffer conflict; Saved(r::KnownMask,conflict);
        conflict.Write()[offset]^=1; r::Seal(conflict.Write()); Bad(conflict.Read(),binding);
    }
    // Corruption with a recomputed checksum is still rejected structurally.
    for(unsigned offset:{24u,32u,88u,92u,96u,97u,98u,99u,100u,112u,113u,114u,115u,178u,272u}) {
        r::Buffer damaged; std::copy(record.Read().begin(),record.Read().end(),damaged.Write().begin());
        damaged.Write()[offset]^=0x80; r::Seal(damaged.Write()); Bad(damaged.Read(),binding);
    }
    for(unsigned checkpoint=1;checkpoint<=4;++checkpoint) {
        l::SelectiveKeys keys; Guard g; Sink s; g.veto=checkpoint;
        assert(keys.Prepare(record.Read(),binding,1,16,Layout));
        auto receipt=keys.Submit(g,s);
        assert(receipt.outcome==ReloadResult::Interrupted && s.calls==checkpoint-1 && !HasSyntheticKey(keys));
        assert(keys.Submit(g,s).outcome==ReloadResult::AlreadyConsumed && s.calls==checkpoint-1);
    }
    for(unsigned failure=1;failure<=3;++failure) for(bool copied:{false,true}) {
        l::SelectiveKeys keys; Guard g; Sink s; s.failure=failure;s.error_after_copy=copied;
        assert(keys.Prepare(record.Read(),binding,1,16,Layout));
        auto receipt=keys.Submit(g,s);
        assert(receipt.outcome==ReloadResult::SubmissionUncertain && s.calls==failure && !HasSyntheticKey(keys));
        const std::array<std::uint32_t,3> prior_masks{0,1,3};
        assert(receipt.submitted_mask==prior_masks[failure-1]);
        assert(keys.Submit(g,s).outcome==ReloadResult::AlreadyConsumed && s.calls==failure);
    }
    for(bool prepared:{false,true}) {
        l::SelectiveKeys keys; Guard g; Sink s;
        if(prepared)assert(keys.Prepare(record.Read(),binding,1,16,Layout));
        keys.Cancel();assert(!HasSyntheticKey(keys));
        assert(keys.Submit(g,s).outcome==ReloadResult::Rejected && !g.calls && !s.calls);
    }
    for(unsigned loss=0;loss<7;++loss) {
        l::SelectiveKeys keys; Guard g; Sink s;
        assert(keys.Prepare(record.Read(),binding,1,16,Layout));
        if(loss==0)++g.binding.controller_context[0];
        if(loss==1)++g.binding.local[0];
        if(loss==2)++g.binding.peer[0];
        if(loss==3)++g.binding.local_type;
        if(loss==4)++g.binding.peer_type;
        if(loss==5)++g.binding.profile;
        if(loss==6)++g.generation;
        assert(keys.Submit(g,s).outcome==ReloadResult::Interrupted && !s.calls && !HasSyntheticKey(keys));
    }
    std::puts("PASS selective reload preparation:320 distribution/security/length combinations, observed1/2/8 set only,512 byte corruptions,513 invalid sizes,15 rehashed semantic corruptions, owner loss and six ambiguous submissions. SYNTHETIC ONLY: no native sender, loaded-state or reconnect proof.");
}
