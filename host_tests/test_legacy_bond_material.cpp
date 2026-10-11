// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/legacy_bond_material.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <vector>
using namespace amazon_remote::bond::legacy;

// Synthetic bytes only. No firmware, real addresses, keys or radio calls.
constexpr Address Target{1,2,3,4,5,6};
using Event=std::array<std::uint8_t, EventBytes>;
Event Frame(std::uint8_t kind) {
    Event e{};
    std::copy(Target.begin(),Target.end(),e.begin());
    e[6]=kind; e[7]=0xa5;
    for (std::size_t i=8; i<e.size(); ++i) e[i]=std::uint8_t(0x80+i);
    return e;
}
void Empty(const Material &m) {
    assert(!m.Valid() && m.KeyKind()==0);
    for (auto byte:m.NativeUnion()) assert(byte==0);
}
int main() {
    static_assert(!std::is_copy_constructible_v<Material>);
    static_assert(!std::is_move_constructible_v<Material>);
    static_assert(!std::is_copy_assignable_v<Material>);
    Material out;
    Empty(out);
    unsigned supported{};
    for (unsigned kind=0; kind<256; ++kind) {
        // Refill first to exercise clearing of stale data on every failure.
        assert(Decode(Layout::AuditedC91Legacy,Target,Frame(1),out));
        const auto e=Frame(std::uint8_t(kind));
        const bool accepted=Decode(Layout::AuditedC91Legacy,Target,e,out);
        std::size_t expected{};
        switch (kind) {
            case 1: expected=28; break;
            case 2: expected=23; break;
            case 4: expected=21; break;
            case 8: expected=4; break;
            case 0x20: expected=7; break;
        }
        assert(accepted==(expected!=0));
        if (!accepted) { Empty(out); continue; }
        ++supported;
        assert(out.Valid() && out.KeyKind()==kind);
        for (std::size_t i=0; i<UnionBytes; ++i)
            assert(out.NativeUnion()[i]==(i<expected ? e[8+i] : 0));
        // Poisoning padding/tail must not influence normalized material.
        auto other=e; other[7]^=0xff;
        for (std::size_t i=8+expected; i<other.size(); ++i) other[i]^=0xff;
        Material normalized;
        assert(Decode(Layout::AuditedC91Legacy,Target,other,normalized));
        assert(std::equal(out.NativeUnion().begin(),out.NativeUnion().end(),
                          normalized.NativeUnion().begin()));
        out.Clear(); Empty(out);
    }
    assert(supported==5);
    const auto valid=Frame(1);
    // Every truncated length, oversize frame, unknown layout and target mismatch.
    for (std::size_t n=0; n<EventBytes; ++n) {
        assert(Decode(Layout::AuditedC91Legacy,Target,valid,out));
        assert(!Decode(Layout::AuditedC91Legacy,Target,{valid.data(),n},out));
        Empty(out);
    }
    std::vector<std::uint8_t> extra(valid.begin(),valid.end()); extra.push_back(0);
    assert(!Decode(Layout::AuditedC91Legacy,Target,extra,out)); Empty(out);
    for (unsigned layout=0; layout<8; ++layout) {
        if (layout==unsigned(Layout::AuditedC91Legacy)) continue;
        assert(Decode(Layout::AuditedC91Legacy,Target,valid,out));
        assert(!Decode(static_cast<Layout>(layout),Target,valid,out)); Empty(out);
    }
    for (std::size_t i=0; i<Target.size(); ++i) {
        auto wrong=Target; wrong[i]^=1;
        assert(!Decode(Layout::AuditedC91Legacy,wrong,valid,out)); Empty(out);
    }
    auto zero=valid; std::fill_n(zero.begin(),6,0);
    assert(!Decode(Layout::AuditedC91Legacy,Address{},zero,out)); Empty(out);
    // No accidental dependence on alignment of a callback buffer.
    std::array<std::uint8_t, EventBytes+1> unaligned{};
    std::copy(valid.begin(),valid.end(),unaligned.begin()+1);
    assert(Decode(Layout::AuditedC91Legacy,Target,{unaligned.data()+1,EventBytes},out));
    out.Clear(); Empty(out);
    std::puts("PASS legacy private material: all256 kinds, padding, length, target and stale-output checks");
}
