// SPDX-License-Identifier: GPL-2.0-only
#include "bond_record_fixture.hpp"
#include <cstdio>
using namespace bond_fixture;
using Bytes=amazon_remote::bond::save_io::Bytes;
Bytes Copy(const rec::Buffer &b) { Bytes out{}; std::copy(b.Read().begin(),b.Read().end(),out.begin()); return out; }
int main() {
    // Known-answer test for the host crypto shim.
    std::array<std::uint8_t,32> hash{};
    sha256CalculateHash(hash.data(),"abc",3);
    constexpr std::array<std::uint8_t,32> answer{0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    assert(hash==answer);
    auto b=Binding(); Keys keys(b); rec::Buffer out; rec::Metadata metadata;
    assert(rec::Empty(b,out) && rec::Inspect(out.Read(),b,16,metadata) && !metadata.occupied && !metadata.generation);
    const auto empty=Copy(out);
    assert(!rec::Inspect(Bytes{},b,16,metadata));
    assert(rec::Build(b,1,keys.pointers,rec::KnownMask,16,false,out));
    assert(rec::Inspect(out.Read(),b,16,metadata) && metadata.occupied && metadata.generation==1 && metadata.mask==rec::KnownMask);
    const auto good=Copy(out);
    // A single-byte corruption at every position is rejected (digest included).
    for(std::size_t i=0;i<good.size();++i) {
        auto bad=good; bad[i]^=0x80; metadata={99,99,16,true,true};
        assert(!rec::Inspect(bad,b,16,metadata) && !metadata.occupied && !metadata.generation && !metadata.mask);
    }
    for(std::size_t n=0;n<good.size();++n) assert(!rec::Inspect(std::span(good).first(n),b,16,metadata));
    std::array<std::uint8_t,513> long_record{};
    std::copy(good.begin(),good.end(),long_record.begin());
    assert(!rec::Inspect(long_record,b,16,metadata));
    // Recompute SHA after corrupting structural/semantic fields. Integrity alone
    // is insufficient. Excludes opaque secret/counter bytes and valid generation.
    unsigned semantic_cases{};
    auto reject=[&](std::size_t offset,std::uint8_t value) {
        auto bad=good; bad[offset]=value; rec::Seal(bad);
        assert(!rec::Inspect(bad,b,16,metadata)); ++semantic_cases;
    };
    for(auto p:{0,8,12,16,32,36,40,72,78,79,80,86,87,88,92,96,97,98,99}) reject(p,0xff);
    for(std::size_t p=100;p<112;++p) reject(p,1);
    for(std::size_t p=272;p<480;++p) reject(p,1);
    for(std::size_t i=0;i<rec::Kinds.size();++i) {
        const auto p=rec::SlotsOffset+i*rec::SlotBytes;
        reject(p,0xff); reject(p+1,0xff); reject(p+2,1); reject(p+3,1);
        for(std::size_t j=legacy::MeaningfulBytes(rec::Kinds[i]);j<28;++j) reject(p+4+j,1);
    }
    for(auto p:{112+4+26,112+4+27,144+4+16,144+4+17,176+4+20,208+4+2,208+4+3,240+4+6}) reject(p,0xff);
    // Each byte of the shared local DIV must agree, even with a valid digest.
    for(auto p:{208+4,208+4+1,240+4+4,240+4+5}) reject(p,0xff);
    for(unsigned byte=0;byte<2;++byte) {
        std::array<std::uint8_t,36> event{};
        std::copy(b.peer.begin(),b.peer.end(),event.begin()); event[6]=32;
        std::copy(keys.pointers[4]->NativeUnion().begin(),keys.pointers[4]->NativeUnion().end(),event.begin()+8);
        event[8+4+byte]^=1;
        legacy::Material inconsistent;
        assert(legacy::Decode(legacy::Layout::AuditedC91Legacy,b.peer,event,inconsistent));
        auto bad_keys=keys.pointers; bad_keys[4]=&inconsistent;
        assert(!rec::Build(b,2,bad_keys,rec::KnownMask,16,false,out) && rec::Zero(out.Read()));
        // With LENC absent there is no second local DIV to contradict.
        bad_keys[3]=nullptr;
        assert(rec::Build(b,2,bad_keys,rec::KnownMask&~8u,16,false,out) && rec::Inspect(out.Read(),b,16,metadata));
    }
    auto zero_generation=good; rec::Put(zero_generation,24,0,8); rec::Seal(zero_generation);
    assert(!rec::Inspect(zero_generation,b,16,metadata));
    // Explicit empty has no peer, key slot, generation, security or layout data.
    for(auto p:{24,32,79,80,88,92,96,97,98,99,112,271,479}) {
        auto bad=empty; bad[p]=1; rec::Seal(bad);
        assert(!rec::Inspect(bad,b,16,metadata));
    }
    auto other=b; other.controller_context[0]^=1; assert(!rec::Inspect(good,other,16,metadata));
    other=b; other.local[0]^=1; assert(!rec::Inspect(good,other,16,metadata));
    other=b; other.peer[0]^=1; assert(!rec::Inspect(good,other,16,metadata));
    other=b; ++other.profile; assert(!rec::Inspect(good,other,16,metadata));
    // All supported optional distributions, and explicit stronger security.
    unsigned distributions{};
    for(std::uint32_t mask=0;mask<64;++mask) if((mask&1) && !(mask&~rec::KnownMask)) {
        Keys subset(b,mask); assert(rec::Build(b,2,subset.pointers,mask,16,false,out));
        assert(rec::Inspect(out.Read(),b,16,metadata) && metadata.mask==mask); ++distributions;
        for(std::size_t i=0;i<rec::Kinds.size();++i) if(!(mask&rec::Kinds[i])) {
            auto bad=Copy(out); bad[rec::SlotsOffset+i*rec::SlotBytes+4]=1; rec::Seal(bad);
            assert(!rec::Inspect(bad,b,16,metadata));
        }
    }
    Keys secure(b,rec::KnownMask,16,true);
    assert(rec::Build(b,2,secure.pointers,rec::KnownMask,16,true,out) && rec::Inspect(out.Read(),b,16,metadata) && metadata.mitm);
    Keys small(b,1,7); assert(rec::Build(b,2,small.pointers,1,7,false,out));
    assert(!rec::Inspect(out.Read(),b,16,metadata) && rec::Inspect(out.Read(),b,7,metadata));
    assert(!rec::Build(b,2,keys.pointers,rec::KnownMask,16,true,out) && rec::Zero(out.Read()));
    for(auto mask:{0u,2u,16u,64u,1u}) assert(!rec::Build(b,2,keys.pointers,mask,16,false,out) && rec::Zero(out.Read()));
    for(auto size:{0,6,17,255}) assert(!rec::Build(b,2,keys.pointers,rec::KnownMask,size,false,out));
    auto duplicate=keys.pointers; duplicate[1]=duplicate[0];
    assert(!rec::Build(b,2,duplicate,rec::KnownMask,16,false,out));
    assert(!rec::Build(b,0,keys.pointers,rec::KnownMask,16,false,out));
    other=b; other.peer_type=1; assert(!rec::Build(other,2,keys.pointers,rec::KnownMask,16,false,out));
    other=b; other.peer[0]^=1; assert(!rec::Build(other,2,keys.pointers,rec::KnownMask,16,false,out));
    other=b; other.controller_context={}; assert(!rec::Empty(other,out) && rec::Zero(out.Read()));
    std::printf("PASS private bond record: SHA256 known answer, 512 byte corruptions, 513 length cases, %u rehashed semantic mutations, %u negotiated distributions. Synthetic records only.\n",semantic_cases,distributions);
}
