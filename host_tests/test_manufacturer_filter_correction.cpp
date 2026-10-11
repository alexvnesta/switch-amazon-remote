// SPDX-License-Identifier: GPL-2.0-only
#include "../mc_mitm/source/amazon_remote/manufacturer_filter_correction.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace amazon_remote;
using Policy=ManufacturerFilterCorrection;
bool Matches(const ManufacturerFilterWire &filter,const std::uint8_t *payload,std::size_t size) {
    if (!payload || size<filter[1] || filter[61]>29) return false;
    for (std::size_t i=0;i<filter[61];++i) if ((payload[i]&filter[32+i])!=filter[3+i]) return false;
    return true;
}
int main() {
    const auto original=OriginalAmazonGeneralFilter(),corrected=CorrectedAmazonGeneralFilter();
    assert(original[0]==corrected[0] && original[2]==corrected[2]);
    constexpr std::uint8_t captured[][9]{
        {0x71,1,4,0x27,2,0x7d,0,0,0},
        {0x71,1,4,0x27,2,0x7d,0x19,0xe6,0x31},
        {0x71,1,4,0x27,2,0x7d,0x9f,0x60,0x2f}};
    for (const auto &ad:captured) {assert(!Matches(original,ad,9));assert(Matches(corrected,ad,9));}
    for (unsigned tail=0;tail<256;++tail) {
        std::uint8_t ad[9]{0x71,1,4,0x27,2,0x7d,std::uint8_t(tail),std::uint8_t(tail^0x55),std::uint8_t(tail^0xaa)};
        assert(Matches(corrected,ad,9));
        for (unsigned i=0;i<6;++i) {ad[i]^=1;assert(!Matches(corrected,ad,9));ad[i]^=1;}
    }
    assert(!Matches(corrected,captured[0],5));
    // Every byte of the incoming62-byte frame must match. No partial profile,
    // variant index, mask, padding, payload or unknown future ABI is admitted.
    for (unsigned i=0;i<62;++i) for (unsigned bit=0;bit<8;++bit) {
        auto bad=original;bad[i]^=1<<bit;
        Policy p;const auto plan=p.Add(FilterBtmProgram,42,true,true,bad.data(),bad.size());
        assert(plan.operation==Policy::Operation::PassOriginal && p.Current()==Policy::State::Unused);
    }
    for (unsigned size=0;size<65;++size) if (size!=62) {
        Policy p;assert(p.Add(FilterBtmProgram,42,true,true,original.data(),size).operation==Policy::Operation::PassOriginal);
    }
    for (unsigned gates=0;gates<16;++gates) {
        Policy p;auto input=original;
        const auto plan=p.Add(gates&1?FilterBtmProgram:0,gates&2?42:0,gates&4,gates&8,input.data(),input.size());
        assert(input==original);assert((plan.operation==Policy::Operation::CorrectAdd)==(gates==15));
    }
    Policy p;
    assert(p.Add(FilterBtmProgram,42,true,true,nullptr,62).operation==Policy::Operation::PassOriginal);
    auto add=p.Add(FilterBtmProgram,42,true,true,original.data(),62);
    assert(add.operation==Policy::Operation::CorrectAdd && add.forwarded==corrected);
    assert(p.Current()==Policy::State::Adding);
    assert(!p.Commit(Policy::Operation::CorrectDelete,0));
    assert(p.Delete(FilterBtmProgram,42,original.data(),62).operation==Policy::Operation::PassOriginal);
    assert(p.Commit(add.operation,0) && p.Current()==Policy::State::Installed);
    assert(!p.Commit(add.operation,0));
    assert(p.Add(FilterBtmProgram,42,true,true,original.data(),62).operation==Policy::Operation::PassOriginal);
    assert(p.Delete(FilterBtmProgram,43,original.data(),62).operation==Policy::Operation::PassOriginal);
    assert(p.Delete(0,42,original.data(),62).operation==Policy::Operation::PassOriginal);
    auto remove=p.Delete(FilterBtmProgram,42,original.data(),62);
    assert(remove.operation==Policy::Operation::CorrectDelete && remove.forwarded==add.forwarded);
    assert(p.Commit(remove.operation,0) && p.Current()==Policy::State::Removed);
    assert(p.Add(FilterBtmProgram,42,true,true,original.data(),62).operation==Policy::Operation::PassOriginal);
    for (const auto failure_on:{Policy::Operation::CorrectAdd,Policy::Operation::CorrectDelete}) {
        Policy failure;auto a=failure.Add(FilterBtmProgram,42,true,true,original.data(),62);
        assert(failure.Commit(a.operation,failure_on==a.operation?0x1759:0));
        if (failure_on==Policy::Operation::CorrectDelete) {
            auto d=failure.Delete(FilterBtmProgram,42,original.data(),62);assert(failure.Commit(d.operation,0x1759));
        }
        assert(failure.Current()==(failure_on==Policy::Operation::CorrectAdd?Policy::State::Failed:Policy::State::RemovalFailed) && failure.Result()==0x1759);
        assert(failure.Add(FilterBtmProgram,42,true,true,original.data(),62).operation==Policy::Operation::PassOriginal);
        if(failure_on==Policy::Operation::CorrectAdd)
            assert(failure.Delete(FilterBtmProgram,42,original.data(),62).operation==Policy::Operation::PassOriginal);
        else {
            // Only a later ORIGINAL-owner native cleanup request may repeat
            // removal. The policy itself never calls, retries or rearms.
            auto retry=failure.Delete(FilterBtmProgram,42,original.data(),62);
            assert(retry.operation==Policy::Operation::CorrectDelete && retry.forwarded==corrected);
            assert(failure.Commit(retry.operation,0) && failure.Current()==Policy::State::Removed);
        }
    }
    std::cout<<"PASS captured3 variants;256 varying tails;1536 prefix rejections;496 frame mutations;64 size rejects;16 gates; exact-owner add/delete/result/no-rearm policy. Pure off-device policy only.\n";
}
