// SPDX-License-Identifier: GPL-2.0-only
#include "bond_record_fixture.hpp"
#include "../mc_mitm/source/amazon_remote/legacy_native_confirmation.hpp"
#include <cstdio>
#include <string_view>
namespace c=amazon_remote::bond::native_confirmation;
using namespace bond_fixture;
using Entry=std::array<std::uint8_t,c::EntryBytes>;
struct Guard {
    rec::Binding binding{Binding()};
    std::uint64_t generation{1};
    unsigned calls{},veto{};
    bool Current(const rec::Binding &b,std::uint64_t g) {
        return ++calls!=veto && b==binding && g==generation;
    }
};
Entry SyntheticNative(const rec::Binding &b,const Keys &keys,std::uint32_t mask,bool mitm) {
    Entry e;e.fill(0x55);
    std::copy(b.peer.begin(),b.peer.end(),e.begin()+0x2c);
    e[0x46]=0x80;e[0x47]=mitm?0x30:0x10;e[0x16c]=b.peer_type;e[0x1c4]=2;e[0x17b]=mask;
    for(unsigned slot=0;slot<5;++slot) if(keys.pointers[slot]) {
        const auto u=keys.pointers[slot]->NativeUnion();
        auto put=[&](unsigned native,unsigned source,unsigned length) {
            std::copy_n(u.begin()+source,length,e.begin()+native);
        };
        switch(rec::Kinds[slot]) {
            case 1:put(0x18c,0,16);put(0x1ac,16,8);put(0x1b4,24,2);put(0x1b8,26,2);break;
            case 2:put(0x17c,0,16);put(0x16d,16,7);break;
            case 4:put(0x19c,4,16);put(0x1ba,20,1);put(0x1bc,0,4);break;
            case 8:put(0x1b6,0,2);put(0x1b9,2,1);put(0x1b8,3,1);break;
            case 32:put(0x1b6,4,2);put(0x1bb,6,1);put(0x1c0,0,4);break;
        }
    }
    return e;
}
bool Check(const rec::Buffer &record,const rec::Binding &b,std::span<const std::uint8_t> native,
           std::uint64_t generation=1,std::uint8_t minimum=16,
           legacy::Layout layout=legacy::Layout::AuditedC91Legacy) {
    Guard g;g.binding=b;
    return c::Check(record.Read(),b,generation,minimum,layout,native,g);
}
int main(int argc,char **argv) {
    const bool emit=argc==2 && std::string_view(argv[1])=="--emit-synthetic";
    assert(argc==1 || emit);
    const auto b=Binding();unsigned combinations{},mutations{};
    for(unsigned subset=0;subset<16;++subset) for(bool mitm:{false,true})
    for(std::uint8_t length=7;length<=16;++length) {
        unsigned mask=1;for(unsigned i=0;i<4;++i)if(subset&(1u<<i))mask|=rec::Kinds[i+1];
        Keys keys(b,mask,length,mitm);rec::Buffer record;
        assert(rec::Build(b,1,keys.pointers,mask,length,mitm,record));
        const auto native=SyntheticNative(b,keys,mask,mitm);
        assert(Check(record,b,native,1,length));++combinations;
        if(emit) {
            // No file/device input. Synthetic bytes only, never a key exporter.
            std::printf("{\"fixture\":\"synthetic-native-confirmation-v1\",\"mask\":%u,\"bytes\":%u,\"mitm\":%s,\"entry\":\"",mask,length,mitm?"true":"false");
            for(auto byte:native)std::printf("%02x",byte);
            std::puts("\",\"matched\":true}");continue;
        }
        // Every meaningful native key byte, identity/type and key mask.
        std::array<bool,c::EntryBytes> relevant{};
        auto mark=[&](unsigned start,unsigned count){std::fill_n(relevant.begin()+start,count,true);};
        mark(0x2c,6);mark(0x16c,1);mark(0x17b,1);mark(0x1c4,1);
        mark(0x18c,16);mark(0x1ac,14);
        if(mask&2){mark(0x17c,16);mark(0x16d,7);}
        if(mask&4){mark(0x19c,16);mark(0x1ba,1);mark(0x1bc,4);}
        if(!(mask&(8|32)))relevant[0x1b6]=relevant[0x1b7]=false;
        if(mask&32){mark(0x1bb,1);mark(0x1c0,4);}
        for(unsigned p=0;p<native.size();++p) if(relevant[p]) {
            auto changed=native;changed[p]^=1;
            assert(!Check(record,b,changed,1,length));++mutations;
        }
        for(auto [offset,bit]:{std::pair{0x46,0x80},std::pair{0x47,0x10},std::pair{0x47,0x20}}) {
            auto changed=native;changed[offset]^=bit;assert(!Check(record,b,changed,1,length));++mutations;
        }
        // Unrelated native fields do not become part of key equality.
        auto unrelated=native;unrelated[0x48]^=1;unrelated[0x15a]=2;unrelated[0x47]|=4;
        assert(Check(record,b,unrelated,1,length));
    }
    assert(combinations==320);if(emit)return 0;
    Keys keys(b);rec::Buffer record;assert(rec::Build(b,1,keys.pointers,rec::KnownMask,16,false,record));
    const auto native=SyntheticNative(b,keys,rec::KnownMask,false);
    for(unsigned n=0;n<native.size();++n)assert(!Check(record,b,std::span(native).first(n)));
    std::array<std::uint8_t,c::EntryBytes+1> long_entry{};assert(!Check(record,b,long_entry));
    assert(!Check(record,b,native,0) && !Check(record,b,native,2) && !Check(record,b,native,1,17));
    assert(!Check(record,b,native,1,16,legacy::Layout::Unsupported));
    for(unsigned point:{1u,2u}) {
        Guard g;g.veto=point;
        assert(!c::Check(record.Read(),b,1,16,legacy::Layout::AuditedC91Legacy,native,g) && g.calls==point);
    }
    for(unsigned pos=0;pos<512;++pos) {
        rec::Buffer bad;std::copy(record.Read().begin(),record.Read().end(),bad.Write().begin());bad.Write()[pos]^=1;
        assert(!Check(bad,b,native));
    }
    for(unsigned mode=0;mode<6;++mode) {
        auto foreign=b;
        if(mode==0)foreign.controller_context[0]^=1;if(mode==1)foreign.local[0]^=1;
        if(mode==2)foreign.peer[0]^=1;if(mode==3)foreign.local_type=1;
        if(mode==4)foreign.peer_type=1;if(mode==5)++foreign.profile;
        assert(!Check(record,foreign,native));
    }
    rec::Buffer empty;assert(rec::Empty(b,empty) && !Check(empty,b,native));
    auto random_identity=b;random_identity.peer_type=1;
    Keys random_keys(random_identity);rec::Buffer random_record;
    assert(rec::Build(random_identity,1,random_keys.pointers,rec::KnownMask,16,false,random_record));
    assert(Check(random_record,random_identity,SyntheticNative(random_identity,random_keys,rec::KnownMask,false)));
    std::printf("PASS native confirmation:320 synthetic key sets, %u native-field mutations,481 invalid native sizes,512 record corruptions,stale/foreign/empty/layout and ownership checks. Private comparator only, no native lookup or hardware proof.\n",mutations);
}
